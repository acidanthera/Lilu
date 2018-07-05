//
//  kern_cpu.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_cpu.hpp>
#include <Headers/kern_devinfo.hpp>

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
	return DeviceInfo::isConnectorLessPlatformId(id);
}

uint32_t CPUInfo::getSandyGpuPlatformId() {
	return DeviceInfo::getLegacyFramebufferId();
}

uint32_t CPUInfo::getGpuPlatformId(IORegistryEntry *sect, bool *specified) {
	uint32_t platform = DeviceInfo::DefaultInvalidPlatformId;

	auto devinfo = DeviceInfo::create();
	if (devinfo) {
		platform = devinfo->reportedFramebufferId;
		DeviceInfo::deleter(devinfo);
	}

	if (specified) *specified = false;

	return platform;
}
