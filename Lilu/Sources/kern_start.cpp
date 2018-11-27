//
//  kern_start.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <PrivateHeaders/kern_config.hpp>
#include <PrivateHeaders/kern_start.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_efi.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_time.hpp>

#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <mach/mach_types.h>

OSDefineMetaClassAndStructors(PRODUCT_NAME, IOService)

static const char kextVersion[] {
#ifdef DEBUG
	'D', 'B', 'G', '-',
#else
	'R', 'E', 'L', '-',
#endif
	xStringify(MODULE_VERSION)[0], xStringify(MODULE_VERSION)[2], xStringify(MODULE_VERSION)[4], '-',
	getBuildYear<0>(), getBuildYear<1>(), getBuildYear<2>(), getBuildYear<3>(), '-',
	getBuildMonth<0>(), getBuildMonth<1>(), '-', getBuildDay<0>(), getBuildDay<1>(), '\0'
};

IOService *PRODUCT_NAME::probe(IOService *provider, SInt32 *score) {
	setProperty("VersionInfo", kextVersion);
	auto service = IOService::probe(provider, score);
	return ADDPR(config).startSuccess ? service : nullptr;
}

bool PRODUCT_NAME::start(IOService *provider) {
	if (!IOService::start(provider)) {
		SYSLOG("init", "failed to start the parent");
		return false;
	}
	
	return ADDPR(config).startSuccess;
}

void PRODUCT_NAME::stop(IOService *provider) {
	IOService::stop(provider);
}

Configuration ADDPR(config);

bool Configuration::performInit() {
	kernelPatcher.init();
		
	if (kernelPatcher.getError() != KernelPatcher::Error::NoError) {
		DBGLOG("config", "failed to initialise kernel patcher");
		kernelPatcher.deinit();
		kernelPatcher.clearError();
		return false;
	}
	
	lilu.processPatcherLoadCallbacks(kernelPatcher);
	
	initialised = userPatcher.init(kernelPatcher, preferSlowMode);
	if (!initialised) {
		DBGLOG("config", "initialisation failed");
		userPatcher.deinit();
		kernelPatcher.deinit();
		kernelPatcher.clearError();
		return false;
	}
	
	lilu.processUserLoadCallbacks(userPatcher);
	
	lilu.activate(kernelPatcher, userPatcher);

	return true;
}

int Configuration::policyCheckRemount(kauth_cred_t cred, mount *mp, label *mlabel) {
	if (!ADDPR(config).initialised) {
		DBGLOG("config", "init via mac_mount_check_remount");
		ADDPR(config).performInit();
	}
	
	return 0;
}

int Configuration::policyCredCheckLabelUpdateExecve(kauth_cred_t auth, vnode_t vp, ...) {
	if (!ADDPR(config).initialised) {
		DBGLOG("config", "init via mac_cred_check_label_update_execve");
		ADDPR(config).performInit();
	}
	
	return 0;
}

#ifdef DEBUG

void Configuration::initCustomDebugSupport() {
	if (debugDumpTimeout == 0)
		return;

	if (!debugBuffer)
		debugBuffer = Buffer::create<uint8_t>(MaxDebugBufferSize);

	if (!debugLock)
		debugLock = IOSimpleLockAlloc();

	if (debugBuffer && debugLock) {
		if (debugDumpCall) {
			while (!thread_call_free(debugDumpCall))
				thread_call_cancel(debugDumpCall);
			debugDumpCall = nullptr;
		}

		debugDumpCall = thread_call_allocate(saveCustomDebugOnDisk, nullptr);
		if (debugDumpCall) {
			uint64_t deadlineNs = convertScToNs(debugDumpTimeout);
			uint64_t deadlineAbs = 0;
			nanoseconds_to_absolutetime(deadlineNs, &deadlineAbs);
			thread_call_enter_delayed(debugDumpCall, mach_absolute_time() + deadlineAbs);
			return;
		}
	}

	if (debugBuffer) {
		Buffer::deleter(debugBuffer);
		debugBuffer = nullptr;
	}

	if (debugLock) {
		IOSimpleLockFree(debugLock);
		debugLock = nullptr;
	}
}

void Configuration::saveCustomDebugOnDisk(thread_call_param_t, thread_call_param_t) {
	if (ADDPR(config).debugLock && ADDPR(config).debugBuffer) {
		auto logBuf = Buffer::create<uint8_t>(MaxDebugBufferSize);
		if (logBuf) {
			size_t logBufSize = 0;
			IOSimpleLockLock(ADDPR(config).debugLock);
			logBufSize = ADDPR(config).debugBufferLength;
			if (logBufSize > 0)
				lilu_os_memcpy(logBuf, ADDPR(config).debugBuffer, logBufSize);
			IOSimpleLockUnlock(ADDPR(config).debugLock);

			if (logBufSize > 0) {
				char name[64];
				snprintf(name, sizeof(name), "/var/log/Lilu_" xStringify(MODULE_VERSION) "_%d.%d.txt", getKernelVersion(), getKernelMinorVersion());
				FileIO::writeBufferToFile(name, logBuf, logBufSize);
			}

			Buffer::deleter(logBuf);
		}
	}

	thread_call_free(ADDPR(config).debugDumpCall);
	ADDPR(config).debugDumpCall = nullptr;
}

#endif

bool Configuration::getBootArguments() {
	if (readArguments) return !isDisabled;
	
	isDisabled = false;

	betaForAll = checkKernelArgument(bootargBetaAll);
	debugForAll = checkKernelArgument(bootargDebugAll);
	isUserDisabled = checkKernelArgument(bootargUserOff);

	PE_parse_boot_argn(bootargDelay, &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));

#ifdef DEBUG
	PE_parse_boot_argn(bootargDump, &debugDumpTimeout, sizeof(debugDumpTimeout));
	// Slightly out of place, but we need to do that as early as possible.
	initCustomDebugSupport();
#endif

	isDisabled |= checkKernelArgument(bootargOff);
	if (!checkKernelArgument(bootargForce)) {
		isDisabled |= checkKernelArgument("-s");
		
		if (!KernelPatcher::compatibleKernel(minKernel, maxKernel)) {
			if (!betaForAll && !checkKernelArgument(bootargBeta)) {
				SYSLOG("config", "automatically disabling on an unsupported operating system");
				isDisabled = true;
			} else if (!isDisabled) {
				SYSLOG("config", "force enabling on an unsupported operating system due to beta flag");
			}
		}
	} else if (!isDisabled) {
		SYSLOG("config", "force enabling due to force flag");
	}

	ADDPR(debugEnabled) = debugForAll;
	ADDPR(debugEnabled) |= checkKernelArgument(bootargDebug);

	allowDecompress = !checkKernelArgument(bootargLowMem);

	installOrRecovery |= checkKernelArgument("rp0");
	installOrRecovery |= checkKernelArgument("rp");
	installOrRecovery |= checkKernelArgument("container-dmg");
	installOrRecovery |= checkKernelArgument("root-dmg");
	installOrRecovery |= checkKernelArgument("auth-root-dmg");

	safeMode = checkKernelArgument("-x");

	preferSlowMode = getKernelVersion() <= KernelVersion::Mavericks || installOrRecovery;

	if (checkKernelArgument(bootargSlow)) {
		preferSlowMode = true;
	} else if (checkKernelArgument(bootargFast)) {
		preferSlowMode = false;
	}

	if (!preferSlowMode && getKernelVersion() <= KernelVersion::Mavericks) {
		// Since vm_shared_region_map_file interface is a little different
		if (!isDisabled) SYSLOG("config", "enforcing -liluslow on Mavericks and lower");
		preferSlowMode = true;
	}
	
	if (!preferSlowMode && installOrRecovery) {
		// Since vdyld shared cache is not available
		if (!isDisabled) SYSLOG("config", "enforcing -liluslow in installer or recovery");
		preferSlowMode = true;
	}
	
	readArguments = true;
	
	DBGLOG("config", "version %s, args: disabled %d, debug %d, slow %d, decompress %d",
		   kextVersion, isDisabled, ADDPR(debugEnabled), preferSlowMode, allowDecompress);
	
	if (isDisabled) {
		SYSLOG("init", "found a disabling argument or no arguments, exiting");
	} else {
		// Decide on booter
		if (!preferSlowMode) {
			policyOps.mpo_cred_check_label_update_execve = reinterpret_cast<mpo_cred_check_label_update_execve_t *>(policyCredCheckLabelUpdateExecve);
		} else {
			policyOps.mpo_mount_check_remount = policyCheckRemount;
		}
	}
	
	return !isDisabled;
}

extern "C" kern_return_t kern_start(kmod_info_t * ki, void *d) {
	// We should be aware of the CPU we run on.
	CPUInfo::loadCpuInformation();
	// Make EFI runtime services available now, since they are standalone.
	EfiRuntimeServices::activate();

	if (ADDPR(config).getBootArguments()) {
		DBGLOG("init", "initialising policy");
		
		lilu.init();
		
		if (ADDPR(config).policy.registerPolicy())
			ADDPR(config).startSuccess = true;
		else
			SYSLOG("init", "failed to register the policy");
	}
	
	return KERN_SUCCESS;
}

extern "C" kern_return_t kern_stop(kmod_info_t *ki, void *d) {
	return ADDPR(config).startSuccess ? KERN_FAILURE : KERN_SUCCESS;
}
