//
//  kern_cpu.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_cpu.hpp>
#include <Headers/kern_devinfo.hpp>

CPUInfo::CpuGeneration CPUInfo::getGeneration(uint32_t *ofamily, uint32_t *omodel) {
	union {
		CpuVersion fmt;
		uint32_t raw;
	} ver {};
	getCpuid(1, 0, &ver.raw);

	uint32_t family = ver.fmt.family;
	if (family == 15) family += ver.fmt.extendedFamily;
	if (ofamily) *ofamily = family;

	uint32_t model = ver.fmt.model;
	if (family == 15 || family == 6)
		model |= ver.fmt.extendedModel << 4;
	if (omodel) *omodel = model;

	if (ver.fmt.family == 6) {
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

void CPUInfo::getCpuid(uint32_t no, uint32_t count, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
	uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
	asm ("xchgq %%rbx, %q1\n"
		 "cpuid\n"
		 "xchgq %%rbx, %q1"
		 : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
		 : "0" (no), "2" (count));

	if (a) *a = eax;
	if (b) *b = ebx;
	if (c) *c = ecx;
	if (d) *d = edx;
}
