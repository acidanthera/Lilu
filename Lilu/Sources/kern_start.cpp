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

bool Configuration::getBootArguments() {
	if (readArguments) return !isDisabled;
	
	isDisabled = false;

	betaForAll = checkKernelArgument(bootargBetaAll);
	debugForAll = checkKernelArgument(bootargDebugAll);

	PE_parse_boot_argn(bootargDelay, &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));

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
	
	DBGLOG("config", "boot arguments disabled %d, debug %d, slow %d, decompress %d", isDisabled, ADDPR(debugEnabled), preferSlowMode, allowDecompress);
	
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
