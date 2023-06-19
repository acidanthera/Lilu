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
#include <i386/pmCPU.h>
}

/**
 * Shared R/W BaseDeviceInfo instance.
 */
extern BaseDeviceInfo globalBaseDeviceInfo;

void CPUInfo::init() {
	auto &bdi = globalBaseDeviceInfo;

	// Start with detecting CPU vendor
	uint32_t b = 0, c = 0, d = 0;
	getCpuid(0, 0, &bdi.cpuMaxLevel, &b, &c, &d);
	if (b == signature_INTEL_ebx && c == signature_INTEL_ecx && d == signature_INTEL_edx)
		bdi.cpuVendor = CpuVendor::Intel;
	else if (b == signature_AMD_ebx && c == signature_AMD_ecx && d == signature_AMD_edx)
		bdi.cpuVendor = CpuVendor::AMD;

	getCpuid(0x80000000, 0, &bdi.cpuMaxLevelExt);

	bdi.cpuHasAvx2 = getCpuid(7, 0, nullptr, &b) && (b & CPUInfo::bit_AVX2) != 0;

	// Only do extended model checking on Intel or when unsupported.
	if (bdi.cpuVendor != CpuVendor::Intel || bdi.cpuMaxLevel < 1)
		return;

	// Detect CPU family and model
	union {
		CpuVersion fmt;
		uint32_t raw;
	} ver {};

	getCpuid(1, 0, &ver.raw);
	bdi.cpuFamily = ver.fmt.family;
	if (bdi.cpuFamily == 15) bdi.cpuFamily += ver.fmt.extendedFamily;

	bdi.cpuModel = ver.fmt.model;
	if (bdi.cpuFamily == 15 || bdi.cpuFamily == 6)
		bdi.cpuModel |= ver.fmt.extendedModel << 4;
	bdi.cpuStepping = ver.fmt.stepping;

	// Last but not least detect CPU generation
	uint32_t generation = 0;
	if (lilu_get_boot_args(Configuration::bootargCpu, &generation, sizeof(generation))) {
		DBGLOG("cpu", "found CPU generation override %u", generation);
		if (generation < static_cast<uint32_t>(CPUInfo::CpuGeneration::MaxGeneration)) {
			bdi.cpuGeneration = static_cast<CPUInfo::CpuGeneration>(generation);
			return;
		} else {
			SYSLOG("cpu", "found invalid CPU generation override %u, falling back...", generation);
		}
	}

	// Keep this mostly in sync to cpuid_set_cpufamily from osfmk/i386/cpuid.c
	if (ver.fmt.family == 6) {
		switch (bdi.cpuModel) {
			case CPU_MODEL_PENRYN:
				bdi.cpuGeneration = CpuGeneration::Penryn;
				break;
			case CPU_MODEL_NEHALEM:
			case CPU_MODEL_FIELDS:
			case CPU_MODEL_DALES:
			case CPU_MODEL_NEHALEM_EX:
				bdi.cpuGeneration = CpuGeneration::Nehalem;
				break;
			case CPU_MODEL_DALES_32NM:
			case CPU_MODEL_WESTMERE:
			case CPU_MODEL_WESTMERE_EX:
				bdi.cpuGeneration = CpuGeneration::Westmere;
				break;
			case CPU_MODEL_SANDYBRIDGE:
			case CPU_MODEL_JAKETOWN:
				bdi.cpuGeneration = CpuGeneration::SandyBridge;
				break;
			case CPU_MODEL_IVYBRIDGE:
			case CPU_MODEL_IVYBRIDGE_EP:
				bdi.cpuGeneration = CpuGeneration::IvyBridge;
				break;
			case CPU_MODEL_HASWELL:
			case CPU_MODEL_HASWELL_EP:
			case CPU_MODEL_HASWELL_ULT:
			case CPU_MODEL_CRYSTALWELL:
				bdi.cpuGeneration = CpuGeneration::Haswell;
				break;
			case CPU_MODEL_BROADWELL:
			case CPU_MODEL_BRYSTALWELL:
				bdi.cpuGeneration = CpuGeneration::Broadwell;
				break;
			case CPU_MODEL_SKYLAKE:
			case CPU_MODEL_SKYLAKE_DT:
			case CPU_MODEL_SKYLAKE_W:
				bdi.cpuGeneration = CpuGeneration::Skylake;
				break;
			case CPU_MODEL_KABYLAKE:
			case CPU_MODEL_KABYLAKE_DT:
				// Kaby has 0x9 stepping, and Coffee use 0xA / 0xB stepping.
				if (ver.fmt.stepping == 9)
					bdi.cpuGeneration = CpuGeneration::KabyLake;
				else
					bdi.cpuGeneration = CpuGeneration::CoffeeLake;
				break;
			case CPU_MODEL_CANNONLAKE:
				bdi.cpuGeneration = CpuGeneration::CannonLake;
				break;
			case CPU_MODEL_ICELAKE_Y:
			case CPU_MODEL_ICELAKE_U:
			case CPU_MODEL_ICELAKE_SP:
				bdi.cpuGeneration = CpuGeneration::IceLake;
				break;
			case CPU_MODEL_COMETLAKE_Y:
			case CPU_MODEL_COMETLAKE_U:
				bdi.cpuGeneration = CpuGeneration::CometLake;
				break;
			case CPU_MODEL_ROCKETLAKE_S:
				bdi.cpuGeneration = CpuGeneration::RocketLake;
				break;
			case CPU_MODEL_TIGERLAKE_U:
				bdi.cpuGeneration = CpuGeneration::TigerLake;
				break;
			case CPU_MODEL_ALDERLAKE_S:
				bdi.cpuGeneration = CpuGeneration::AlderLake;
				break;
			case CPU_MODEL_RAPTORLAKE_S:
			case CPU_MODEL_RAPTORLAKE_HX:
				bdi.cpuGeneration = CpuGeneration::RaptorLake;
				break;
			default:
				bdi.cpuGeneration = CpuGeneration::Unknown;
				break;
		}
	}
}

CPUInfo::CpuGeneration CPUInfo::getGeneration(uint32_t *ofamily, uint32_t *omodel, uint32_t *ostepping) {
	auto &bdi = BaseDeviceInfo::get();
	if (ofamily) *ofamily = bdi.cpuFamily;
	if (omodel) *omodel = bdi.cpuModel;
	if (ostepping) *ostepping = bdi.cpuStepping;

	return bdi.cpuGeneration;
}

bool CPUInfo::getCpuTopology(CpuTopology &topology) {
#if __MAC_OS_X_VERSION_MIN_REQUIRED > __MAC_10_4
	// Obtain power management callbacks
	if (getKernelVersion() < KernelVersion::Lion) {
		SYSLOG("cpu", "cannot use pmKextRegister before 10.7");
		return false;
	}

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
#else
	SYSLOG("cpu", "cannot use pmKextRegister on this platform");
	return false;
#endif
}

bool CPUInfo::getCpuid(uint32_t no, uint32_t count, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
	auto &bdi = BaseDeviceInfo::get();

	uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

	bool supported = (no & 0x80000000) ? bdi.cpuMaxLevelExt >= no : bdi.cpuMaxLevel >= no;

	// At least pass zeroes on failure
	if (supported) {
#if defined(__i386__)
		asm ("xchg %%ebx, %q1\n"
			 "cpuid\n"
			 "xchg %%ebx, %q1"
			 : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
			 : "0" (no), "2" (count));
#elif defined(__x86_64__)
		asm ("xchgq %%rbx, %q1\n"
			 "cpuid\n"
			 "xchgq %%rbx, %q1"
			 : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
			 : "0" (no), "2" (count));
#else
#error Unsupported arch.
#endif
	}

	if (a) *a = eax;
	if (b) *b = ebx;
	if (c) *c = ecx;
	if (d) *d = edx;

	return supported;
}
