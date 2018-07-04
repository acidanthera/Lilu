//
//  kern_cpu.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_cpu.hpp>

CPUInfo::CpuGeneration CPUInfo::getGeneration(uint32_t *ofamily, uint32_t *omodel) {
	CpuVersion ver {};
	uint32_t ebx = 0, ecx = 0, edx = 0;
	asm ("cpuid"
		 : "=a" (ver), "=b" (ebx), "=c" (ecx), "=d" (edx)
		 : "0" (1));

	uint32_t family = ver.family;
	if (family == 15) family += ver.extendedFamily;
	if (ofamily) *ofamily = family;

	uint32_t model = ver.model;
	if (family == 15 || family == 6)
		model |= ver.extendedModel << 4;
	if (omodel) *omodel = model;

	if (ver.family == 6) {
		switch (model) {
			case CPU_MODEL_PENRYN:
				return CpuGeneration::Penryn;
			case CPU_MODEL_NEHALEM:
			case CPU_MODEL_FIELDS:
			case CPU_MODEL_NEHALEM_EX:
				return CpuGeneration::Nehalem;
			case CPU_MODEL_DALES:
			case CPU_MODEL_DALES_32NM:
				return CpuGeneration::Westmere;
			case CPU_MODEL_SANDYBRIDGE:
			case CPU_MODEL_JAKETOWN:
				return CpuGeneration::SandyBridge;
			case CPU_MODEL_IVYBRIDGE:
			case CPU_MODEL_IVYBRIDGE_EP:
				return CpuGeneration::IvyBridge;
			case CPU_MODEL_CRYSTALWELL:
			case CPU_MODEL_HASWELL:
			case CPU_MODEL_HASWELL_EP:
			case CPU_MODEL_HASWELL_ULT:
				return CpuGeneration::Haswell;
			case CPU_MODEL_BROADWELL:
				// Here and below commented out due to equivalent values.
				// case CPU_MODEL_BROADWELL_ULX:
				// case CPU_MODEL_BROADWELL_ULT:
			case CPU_MODEL_BRYSTALWELL:
				return CpuGeneration::Broadwell;
			case CPU_MODEL_SKYLAKE:
				// case CPU_MODEL_SKYLAKE_ULT:
				// case CPU_MODEL_SKYLAKE_ULX:
			case CPU_MODEL_SKYLAKE_DT:
			case CPU_MODEL_SKYLAKE_W:
				return CpuGeneration::Skylake;
			case CPU_MODEL_KABYLAKE:
				// case CPU_MODEL_KABYLAKE_ULT:
				// case CPU_MODEL_KABYLAKE_ULX:
			case CPU_MODEL_KABYLAKE_DT:
				return CpuGeneration::KabyLake;
			default:
				return CpuGeneration::Unknown;
		}
	}
	return CpuGeneration::Unknown;
}

bool CPUInfo::isConnectorLessPlatformId(uint32_t id) {
	return
	id == ConnectorLessSandyBridgePlatformId1 ||
	id == ConnectorLessSandyBridgePlatformId2 ||
	id == ConnectorLessIvyBridgePlatformId1 ||
	id == ConnectorLessIvyBridgePlatformId2 ||
	id == ConnectorLessHaswellPlatformId1 ||
	id == ConnectorLessHaswellPlatformId2 ||
	id == ConnectorLessSkylakePlatformId1 ||
	id == ConnectorLessSkylakePlatformId2 ||
	id == ConnectorLessSkylakePlatformId3 ||
	id == ConnectorLessSkylakePlatformId4 ||
	id == ConnectorLessKabyLakePlatformId1 ||
	id == ConnectorLessKabyLakePlatformId2;
}

uint32_t CPUInfo::getSandyGpuPlatformId() {
	char boardIdentifier[64] {};
	if (WIOKit::getComputerInfo(nullptr, 0, boardIdentifier, sizeof(boardIdentifier))) {
		struct {
			const char *boardId;
			uint32_t platformId;
		} sandyBoards[] = {
			{"Mac-94245B3640C91C81", 0x10000},
			{"Mac-94245AF5819B141B", 0x10000},
			{"Mac-94245A3940C91C80", 0x10000},
			{"Mac-942459F5819B171B", 0x10000},
			{"Mac-8ED6AF5B48C039E1", 0x30010}, // or 0x30020
			{"Mac-7BA5B2794B2CDB12", 0x30010}, // or 0x30020
			{"Mac-4BC72D62AD45599E", 0x30030},
			{"Mac-742912EFDBEE19B3", 0x40000},
			{"Mac-C08A6BB70A942AC2", 0x40000},
			{"Mac-942B5BF58194151B", 0x50000},
			{"Mac-942B5B3A40C91381", 0x50000},
			{"Mac-942B59F58194171B", 0x50000}
		};
		for (size_t i = 0; i < arrsize(sandyBoards); i++)
			if (!strcmp(sandyBoards[i].boardId, boardIdentifier))
				return sandyBoards[i].platformId;
	} else {
		SYSLOG("cpu", "failed to obtain board-id");
	}
	return DefaultInvalidPlatformId;
}

uint32_t CPUInfo::getGpuPlatformId(IORegistryEntry *sect, bool *specified) {
	if (!sect) {
		sect = WIOKit::findEntryByPrefix("/AppleACPIPlatformExpert", "PCI", gIOServicePlane);
		if (sect) sect = WIOKit::findEntryByPrefix(sect, "AppleACPIPCI", gIOServicePlane);
		if (sect) {
			auto igpu = WIOKit::findEntryByPrefix(sect, "IGPU", gIOServicePlane);
			// Try GFX0, since recent IntelGraphicsFixup may rename it later.
			if (!igpu) sect = WIOKit::findEntryByPrefix(sect, "GFX0", gIOServicePlane);
			else sect = igpu;
		}
	}

	if (specified) *specified = false;
	uint32_t platform = DefaultInvalidPlatformId;
	if (sect) {
		const char *source = "CPU gen";
		if (WIOKit::getOSDataValue(sect, "AAPL,ig-platform-id", platform)) {
			source = "AAPL,ig-platform-id";
			if (specified) *specified = true;
		} else if (WIOKit::getOSDataValue(sect, "AAPL,snb-platform-id", platform)) {
			source = "AAPL,snb-platform-id";
			if (specified) *specified = true;
		} else {
			// Intel drivers for Skylake and higher provide default AAPL,ig-platform-id.
			auto generation = getGeneration();
			if (generation == CpuGeneration::Skylake)
				platform = DefaultSkylakePlatformId;
			else if (generation == CpuGeneration::KabyLake)
				platform = DefaultKabyLakePlatformId;
			else if (generation == CpuGeneration::SandyBridge)
				platform = getSandyGpuPlatformId();
			else
				source = "(not found)";
		}
		(void)source;
		DBGLOG("cpu", "found %s with frame id %08x via %s", safeString(sect->getName()), platform, source);
	} else {
		DBGLOG("cpu", "failed to detect built-in GPU");
	}

	return platform;
}
