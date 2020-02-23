//
//  kern_cpu.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_cpu.hpp>
#include <Headers/kern_devinfo.hpp>
#include <PrivateHeaders/kern_config.hpp>
#include <i386/proc_reg.h>

extern "C" {
#include <Library/osfmk/i386/pmCPU.h>
}

static CPUInfo::CpuVendor currentVendor = CPUInfo::CpuVendor::Unknown;
static CPUInfo::CpuGeneration currentGeneration = CPUInfo::CpuGeneration::Unknown;
static uint32_t currentFamily = 0;
static uint32_t currentModel = 0;
static uint32_t currentStepping = 0;
static uint32_t currentMaxLevel = 0;
static uint32_t currentMaxLevelExt = 0x80000000;

void CPUInfo::loadCpuInformation() {
	// Start with detecting CPU vendor
	uint32_t b = 0, c = 0, d = 0;
	getCpuid(0, 0, &currentMaxLevel, &b, &c, &d);
	if (b == signature_INTEL_ebx && c == signature_INTEL_ecx && d == signature_INTEL_edx)
		currentVendor = CpuVendor::Intel;
	else if (b == signature_AMD_ebx && c == signature_AMD_ecx && d == signature_AMD_edx)
		currentVendor = CpuVendor::AMD;

	getCpuid(0x80000000, 0, &currentMaxLevelExt);

	// Only do extended model checking on Intel or when unsupported.
	if (currentVendor != CpuVendor::Intel || currentMaxLevel < 1)
		return;

	// Detect CPU family and model
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

	// Last but not least detect CPU generation
	uint32_t generation = 0;
	if (PE_parse_boot_argn(Configuration::bootargCpu, &generation, sizeof(generation))) {
		DBGLOG("cpu", "found CPU generation override %u", generation);
		if (generation < static_cast<uint32_t>(CPUInfo::CpuGeneration::MaxGeneration)) {
			currentGeneration = static_cast<CPUInfo::CpuGeneration>(generation);
			return;
		} else {
			SYSLOG("cpu", "found invalid CPU generation override %u, falling back...", generation);
		}
	}

	// Keep this mostly in sync to cpuid_set_cpufamily from osfmk/i386/cpuid.c
	if (ver.fmt.family == 6) {
		switch (currentModel) {
			case CPU_MODEL_PENRYN:
				currentGeneration = CpuGeneration::Penryn;
				break;
			case CPU_MODEL_NEHALEM:
			case CPU_MODEL_FIELDS:
			case CPU_MODEL_DALES:
			case CPU_MODEL_NEHALEM_EX:
				currentGeneration = CpuGeneration::Nehalem;
				break;
			case CPU_MODEL_DALES_32NM:
			case CPU_MODEL_WESTMERE:
			case CPU_MODEL_WESTMERE_EX:
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
			case CPU_MODEL_HASWELL:
			case CPU_MODEL_HASWELL_EP:
			case CPU_MODEL_HASWELL_ULT:
			case CPU_MODEL_CRYSTALWELL:
				currentGeneration = CpuGeneration::Haswell;
				break;
			case CPU_MODEL_BROADWELL:
			case CPU_MODEL_BRYSTALWELL:
				currentGeneration = CpuGeneration::Broadwell;
				break;
			case CPU_MODEL_SKYLAKE:
			case CPU_MODEL_SKYLAKE_DT:
			case CPU_MODEL_SKYLAKE_W:
				currentGeneration = CpuGeneration::Skylake;
				break;
			case CPU_MODEL_KABYLAKE:
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
			case CPU_MODEL_COMETLAKE:
				currentGeneration = CpuGeneration::CometLake;
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

bool CPUInfo::getCpuTopology(CpuTopology &topology) {
	// Obtain power management callbacks
	pmCallBacks_t callbacks {};
	pmKextRegister(PM_DISPATCH_VERSION, nullptr, &callbacks);

	if (!callbacks.GetPkgRoot) {
		SYSLOG("cpu", "failed to obtain package root callback");
		return false;
	}

	auto pkg = callbacks.GetPkgRoot();
	if (!pkg) {
		SYSLOG("cpu", "failed to obtain valid package root");
		return false;
	}

	while (pkg) {
		auto core = pkg->cores;
		// Set physcal core mapping based on first virtual core
		while (core) {
			// I think lcpus could be null when the core is disabled and the topology is partially constructed
			auto lcpu = core->lcpus;
			if (lcpu) {
				topology.numberToPackage[lcpu->cpu_num] = topology.packageCount;
				topology.numberToPhysical[lcpu->cpu_num] = topology.physicalCount[topology.packageCount];
				topology.numberToLogical[lcpu->cpu_num] = topology.logicalCount[topology.packageCount];
				topology.physicalCount[topology.packageCount]++;
				topology.logicalCount[topology.packageCount]++;
			}
			core = core->next_in_pkg;
		}

		// Set the rest of virtual core mapping
		core = pkg->cores;
		while (core) {
			auto first_lcpu = core->lcpus;
			auto lcpu = first_lcpu ? first_lcpu->next_in_core : nullptr;
			while (lcpu) {
				topology.numberToPackage[lcpu->cpu_num] = topology.packageCount;
				topology.numberToPhysical[lcpu->cpu_num] = topology.numberToPhysical[first_lcpu->cpu_num];
				topology.numberToLogical[lcpu->cpu_num] = topology.logicalCount[topology.packageCount];
				topology.logicalCount[topology.packageCount]++;
				lcpu = lcpu->next_in_core;
			}
			core = core->next_in_pkg;
		}

		topology.packageCount++;
		pkg = pkg->next;
	}

	return true;
}

bool CPUInfo::getCpuid(uint32_t no, uint32_t count, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
	uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

	bool supported = (no & 0x80000000) ? currentMaxLevelExt >= no : currentMaxLevel >= no;

	// At least pass zeroes on failure
	if (supported) {
		asm ("xchgq %%rbx, %q1\n"
			 "cpuid\n"
			 "xchgq %%rbx, %q1"
			 : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
			 : "0" (no), "2" (count));
	}

	if (a) *a = eax;
	if (b) *b = ebx;
	if (c) *c = ecx;
	if (d) *d = edx;

	return supported;
}
