//
//  kern_cpu.hpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#ifndef kern_cpu_h
#define kern_cpu_h

#include <Headers/kern_config.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_util.hpp>

#include <Library/LegacyIOService.h>

namespace CPUInfo {
	/**
	 *  Contents of CPUID(1) eax register contents describing model version
	 */
	struct CpuVersion {
		uint32_t stepping       : 4;
		uint32_t model          : 4;
		uint32_t family         : 4;
		uint32_t type           : 2;
		uint32_t reserved1      : 2;
		uint32_t extendedModel  : 4;
		uint32_t extendedFamily : 8;
		uint32_t reserved2      : 4;
	};

	/**
	 *  Intel CPU models as returned by CPUID
	 *  The list is synchronised and updated with XNU source code (osfmk/i386/cpuid.h).
	 *  Names are altered to avoid conflicts just in case.
	 *  Last update: xnu-4570.41.2
	 *  Some details could be found on http://instlatx64.atw.hu and https://en.wikichip.org/wiki/64-bit_architecture#x86
	 */
	enum CpuModel {
		CPU_MODEL_UNKNOWN        =  0x00,
		CPU_MODEL_PENRYN         =  0x17,
		CPU_MODEL_NEHALEM        =  0x1A,
		CPU_MODEL_FIELDS         =  0x1E, /* Lynnfield, Clarksfield */
		CPU_MODEL_DALES          =  0x1F, /* Havendale, Auburndale */
		CPU_MODEL_NEHALEM_EX     =  0x2E,
		CPU_MODEL_DALES_32NM     =  0x25, /* Clarkdale, Arrandale */
		CPU_MODEL_WESTMERE       =  0x2C, /* Gulftown, Westmere-EP/-WS */
		CPU_MODEL_WESTMERE_EX    =  0x2F,
		CPU_MODEL_SANDYBRIDGE    =  0x2A,
		CPU_MODEL_JAKETOWN       =  0x2D,
		CPU_MODEL_IVYBRIDGE      =  0x3A,
		CPU_MODEL_IVYBRIDGE_EP   =  0x3E,
		CPU_MODEL_CRYSTALWELL    =  0x46,
		CPU_MODEL_HASWELL        =  0x3C,
		CPU_MODEL_HASWELL_EP     =  0x3F,
		CPU_MODEL_HASWELL_ULT    =  0x45,
		CPU_MODEL_BROADWELL      =  0x3D,
		CPU_MODEL_BROADWELL_ULX  =  0x3D,
		CPU_MODEL_BROADWELL_ULT  =  0x3D,
		CPU_MODEL_BRYSTALWELL    =  0x47,
		CPU_MODEL_SKYLAKE        =  0x4E,
		CPU_MODEL_SKYLAKE_ULT    =  0x4E,
		CPU_MODEL_SKYLAKE_ULX    =  0x4E,
		CPU_MODEL_SKYLAKE_DT     =  0x5E,
		CPU_MODEL_SKYLAKE_W      =  0x55,
		CPU_MODEL_KABYLAKE       =  0x8E,
		CPU_MODEL_KABYLAKE_ULT   =  0x8E,
		CPU_MODEL_KABYLAKE_ULX   =  0x8E,
		CPU_MODEL_KABYLAKE_DT    =  0x9E,
		// The latter are for information reasons only.
		// First Coffee Lake CPUs      = 0x9E
		// Subsequent Coffee Lake CPUs = 0x9C
		// Cannon Lake CPUs            = 0x66
		// Ice Lake CPUs               = 0x7E
	};

	/**
	 *  Intel CPU generations
	 */
	enum class CpuGeneration {
		Unknown,
		Penryn,
		Nehalem,
		Westmere,
		SandyBridge,
		IvyBridge,
		Haswell,
		Broadwell,
		Skylake,
		KabyLake,
		// The latter are for information reasons only.
		// CoffeeLake,
		// CannonLake,
		// IceLake
	};

	/**
	 *  Get running CPU generation.
	 *
	 *  @param ofamily a pointer to store CPU family in
	 *  @param omodel  a pointer to store CPU model in
	 *
	 *  @return detected Intel CPU generation
	 */
	inline CpuGeneration getGeneration(uint32_t *ofamily=nullptr, uint32_t *omodel=nullptr) {
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

	/**
	 *  Known platform ids used by Intel GPU kexts
	 */
	static constexpr uint32_t DefaultInvalidPlatformId {0xFFFFFFFF};
	static constexpr uint32_t DefaultSkylakePlatformId {0x19120000};
	static constexpr uint32_t DefaultKabyLakePlatformId {0x59160000};

	/**
	 *  Framebuffers without any ports used for hardware acceleration only
	 *  Note: Broadwell framebuffers all have connectors added.
	 */
	static constexpr uint32_t ConnectorLessSandyBridgePlatformId1 {0x00030030};
	static constexpr uint32_t ConnectorLessSandyBridgePlatformId2 {0x00050000};
	static constexpr uint32_t ConnectorLessIvyBridgePlatformId1 {0x01620006};
	static constexpr uint32_t ConnectorLessIvyBridgePlatformId2 {0x01620007};
	static constexpr uint32_t ConnectorLessHaswellPlatformId1 {0x04120004};
	static constexpr uint32_t ConnectorLessHaswellPlatformId2 {0x0412000B};
	static constexpr uint32_t ConnectorLessSkylakePlatformId1 {0x19020001};
	static constexpr uint32_t ConnectorLessSkylakePlatformId2 {0x19170001};
	static constexpr uint32_t ConnectorLessSkylakePlatformId3 {0x19120001};
	static constexpr uint32_t ConnectorLessSkylakePlatformId4 {0x19320001};
	static constexpr uint32_t ConnectorLessKabyLakePlatformId1 {0x59180002};
	static constexpr uint32_t ConnectorLessKabyLakePlatformId2 {0x59120003};

	/**
	 *  Check whether IGPU platform id contains any connectors
	 *
	 *  @param id  IGPU platform id
	 *
	 *  @return true if the specified platform id has no connectors
	 */
	inline bool isConnectorLessPlatformId(uint32_t id) {
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

	/**
	 *  Return Sandy Bridge default platform id.
	 *
	 *  @return valid platform id or DefaultInvalidPlatformId
	 */
	inline uint32_t getSandyGpuPlatformId() {
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

	/**
	 *  Return running IGPU platform id.
	 *
	 *  @param sect      known IGPU entry.
	 *  @param specified set to true if the value was directly read from ioreg
	 *
	 *  @return valid platform id or DefaultInvalidPlatformId
	 */
	inline uint32_t getGpuPlatformId(IORegistryEntry *sect=nullptr, bool *specified=nullptr) {
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
			DBGLOG("cpu", "found %s with frame id %08x via %s", safeString(sect->getName()), platform, source);
		} else {
			DBGLOG("cpu", "failed to detect built-in GPU");
		}

		return platform;
	}
}

#endif /* kern_cpu_h */
