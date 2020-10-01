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
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_time.hpp>
#include <Headers/kern_version.hpp>

#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>
#include <mach/mach_types.h>

OSDefineMetaClassAndStructors(PRODUCT_NAME, IOService)

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

bool Configuration::performEarlyInit() {
	kernelPatcher.init();

	if (kernelPatcher.getError() != KernelPatcher::Error::NoError) {
		DBGLOG("config", "failed to initialise kernel patcher");
		kernelPatcher.deinit();
		kernelPatcher.clearError();
		return false;
	}

	KernelPatcher::RouteRequest request {"_PE_initialize_console", initConsole, orgInitConsole};
	if (!kernelPatcher.routeMultiple(KernelPatcher::KernelID, &request, 1, 0, 0, true, false)) {
		SYSLOG("config", "failed to initialise through console routing");
		kernelPatcher.deinit();
		kernelPatcher.clearError();
		return false;
	}

	return true;
}

int Configuration::initConsole(PE_Video *info, int op) {
	DBGLOG("config", "PE_initialize_console %d", op);
	if (op == kPEEnableScreen && !atomic_load_explicit(&ADDPR(config).initialised, memory_order_relaxed)) {
		IOLockLock(ADDPR(config).policyLock);
		if (!atomic_load_explicit(&ADDPR(config).initialised, memory_order_relaxed)) {
			DBGLOG("config", "PE_initialize_console %d performing init", op);

			// Complete plugin registration and mark ourselves as loaded ahead of time to avoid race conditions.
			lilu.finaliseRequests();
			atomic_store_explicit(&ADDPR(config).initialised, true, memory_order_relaxed);

			// Fire plugin init in the thread to avoid colliding with PCI configuration.
			auto thread = thread_call_allocate([](thread_call_param_t, thread_call_param_t thread) {
				ADDPR(config).performCommonInit();
				thread_call_free(static_cast<thread_call_t>(thread));
			}, nullptr);
			if (thread)
				thread_call_enter1(thread, thread);
		}
		IOLockUnlock(ADDPR(config).policyLock);
	}
	return FunctionCast(initConsole, ADDPR(config).orgInitConsole)(info, op);
}

bool Configuration::performCommonInit() {
	DeviceInfo::createCached();

	lilu.processPatcherLoadCallbacks(kernelPatcher);

	bool ok = userPatcher.init(kernelPatcher, preferSlowMode);
	if (ok) {
		// We are safely locked, just need to ensure atomicity
		atomic_store_explicit(&initialised, true, memory_order_relaxed);
	} else {
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

bool Configuration::performInit() {
	kernelPatcher.init();

	if (kernelPatcher.getError() != KernelPatcher::Error::NoError) {
		DBGLOG("config", "failed to initialise kernel patcher");
		kernelPatcher.deinit();
		kernelPatcher.clearError();
		return false;
	}

	lilu.finaliseRequests();

	return performCommonInit();
}

int Configuration::policyCheckRemount(kauth_cred_t, mount *, label *) {
	ADDPR(config).policyInit("mac_mount_check_remount");
	return 0;
}

int Configuration::policyCredCheckLabelUpdateExecve(kauth_cred_t, vnode_t, ...) {
	ADDPR(config).policyInit("mac_cred_check_label_update_execve");
	return 0;
}

void Configuration::policyInitBSD(mac_policy_conf *conf) {
	DBGLOG("config", "init bsd policy on %u in %d", getKernelVersion(), ADDPR(config).installOrRecovery);
	if (getKernelVersion() >= KernelVersion::BigSur)
		ADDPR(config).policyInit("init bsd");
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
	isUserDisabled = checkKernelArgument(bootargUserOff) || getKernelVersion() >= KernelVersion::BigSur;

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
		SYSLOG("config", "found a disabling argument or no arguments, exiting");
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

bool Configuration::registerPolicy() {
	DBGLOG("config", "initialising policy");

	policyLock = IOLockAlloc();

	if (policyLock == nullptr) {
		SYSLOG("config", "failed to alloc policy lock");
		return false;
	}

	if (getKernelVersion() >= KernelVersion::BigSur) {
		if (performEarlyInit()) {
			startSuccess = true;
			return true;
		} else {
			SYSLOG("config", "failed to perform early init");
		}
	}

	if (!policy.registerPolicy()) {
		SYSLOG("config", "failed to register the policy");
		IOLockFree(policyLock);
		policyLock = nullptr;
		return false;
	}

	startSuccess = true;
	return true;
}

extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
	if (ADDPR(config).getBootArguments()) {
		// Make EFI runtime services available now, since they are standalone.
		EfiRuntimeServices::activate();
		// Init basic device information.
		BaseDeviceInfo::init();
		// Init Lilu API.
		lilu.init();

		ADDPR(config).registerPolicy();
	}

	return KERN_SUCCESS;
}

extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
	return ADDPR(config).startSuccess ? KERN_FAILURE : KERN_SUCCESS;
}
