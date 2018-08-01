//
//  kern_cpu.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_cpu.hpp>
#include <Headers/kern_devinfo.hpp>

static CPUInfo::CpuGeneration currentGeneration = CPUInfo::CpuGeneration::Unknown;
static uint32_t currentFamily = 0;
static uint32_t currentModel = 0;
static uint32_t currentStepping = 0;

void CPUInfo::loadCpuGeneration() {
	union {
		CpuVersion fmt;
		uint32_t raw;
	} ver {};
	getCpuid(1, 0, &ver.raw);

	currentFamily = ver.fmt.family;
	if (currentFamily == 15) currentFamily += ver.fmt.extendedFamily;

	currentModel = ver.fmt.model;
	if (currentFamily == 15 || currentFamily == 6)
		currentModel |= ver.fmt.extendedModel << 4;
	currentStepping = ver.fmt.stepping;

	uint32_t generation = 0;
	if (PE_parse_boot_argn("lilucpu", &generation, sizeof(generation))) {
		DBGLOG("cpu", "found CPU generation override %u", generation);
		if (generation < static_cast<uint32_t>(CPUInfo::CpuGeneration::MaxGeneration)) {
			currentGeneration = static_cast<CPUInfo::CpuGeneration>(generation);
			return;
		} else {
			SYSLOG("cpu", "found invalid CPU generation override %u, falling back...", generation);
		}
	}

	if (ver.fmt.family == 6) {
		switch (currentModel) {
			case CPU_MODEL_PENRYN:
				currentGeneration = CpuGeneration::Penryn;
				break;
			case CPU_MODEL_NEHALEM:
			case CPU_MODEL_FIELDS:
			case CPU_MODEL_NEHALEM_EX:
				currentGeneration = CpuGeneration::Nehalem;
				break;
			case CPU_MODEL_DALES:
			case CPU_MODEL_DALES_32NM:
				currentGeneration = CpuGeneration::Westmere;
				break;
			case CPU_MODEL_SANDYBRIDGE:
			case CPU_MODEL_JAKETOWN:
				currentGeneration = CpuGeneration::SandyBridge;
				break;
			case CPU_MODEL_IVYBRIDGE:
			case CPU_MODEL_IVYBRIDGE_EP:
				currentGeneration = CpuGeneration::IvyBridge;
				break;
			case CPU_MODEL_CRYSTALWELL:
			case CPU_MODEL_HASWELL:
			case CPU_MODEL_HASWELL_EP:
			case CPU_MODEL_HASWELL_ULT:
				currentGeneration = CpuGeneration::Haswell;
				break;
			case CPU_MODEL_BROADWELL:
				// Here and below commented out due to equivalent values.
				// case CPU_MODEL_BROADWELL_ULX:
				// case CPU_MODEL_BROADWELL_ULT:
			case CPU_MODEL_BRYSTALWELL:
				currentGeneration = CpuGeneration::Broadwell;
				break;
			case CPU_MODEL_SKYLAKE:
				// case CPU_MODEL_SKYLAKE_ULT:
				// case CPU_MODEL_SKYLAKE_ULX:
			case CPU_MODEL_SKYLAKE_DT:
			case CPU_MODEL_SKYLAKE_W:
				currentGeneration = CpuGeneration::Skylake;
				break;
			case CPU_MODEL_KABYLAKE:
				// case CPU_MODEL_KABYLAKE_ULT:
				// case CPU_MODEL_KABYLAKE_ULX:
				currentGeneration = CpuGeneration::KabyLake;
				break;
			case CPU_MODEL_KABYLAKE_DT:
				// Kaby has 0x9 stepping, and Coffee use 0xA / 0xB stepping.
				if (ver.fmt.stepping == 9)
					currentGeneration = CpuGeneration::KabyLake;
				else
					currentGeneration = CpuGeneration::CoffeeLake;
				break;
			case CPU_MODEL_CANNONLAKE:
				currentGeneration = CpuGeneration::CannonLake;
				break;
			case CPU_MODEL_ICELAKE:
				currentGeneration = CpuGeneration::IceLake;
				break;
			default:
				currentGeneration = CpuGeneration::Unknown;
				break;
		}
	}
}

CPUInfo::CpuGeneration CPUInfo::getGeneration(uint32_t *ofamily, uint32_t *omodel, uint32_t *ostepping) {
	if (ofamily) *ofamily = currentFamily;
	if (omodel) *omodel = currentModel;
	if (ostepping) *ostepping = currentStepping;

	return currentGeneration;
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
