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
	return config.startSuccess ? service : nullptr;
}

bool PRODUCT_NAME::start(IOService *provider) {
	if (!IOService::start(provider)) {
		SYSLOG("init", "failed to start the parent");
		return false;
	}
	
	return config.startSuccess;
}

void PRODUCT_NAME::stop(IOService *provider) {
	IOService::stop(provider);
}

Configuration config;

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
	if (!config.initialised) {
		DBGLOG("config", "init via mac_mount_check_remount");
		config.performInit();
	}
	
	return 0;
}

int Configuration::policyCredCheckLabelUpdateExecve(kauth_cred_t auth, vnode_t vp, ...) {
	if (!config.initialised) {
		DBGLOG("config", "init via mac_cred_check_label_update_execve");
		config.performInit();
	}
	
	return 0;
}

bool Configuration::getBootArguments() {
	if (readArguments) return !isDisabled;
	
	isDisabled = false;
	char tmp[16];
	
	betaForAll = PE_parse_boot_argn(bootargBetaAll, tmp, sizeof(tmp));
	debugForAll = PE_parse_boot_argn(bootargDebugAll, tmp, sizeof(tmp));

	PE_parse_boot_argn(bootargDelay, &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));

	isDisabled |= PE_parse_boot_argn(bootargOff, tmp, sizeof(tmp));
	if (!PE_parse_boot_argn(bootargForce, tmp, sizeof(tmp))) {
		isDisabled |= PE_parse_boot_argn("-s", tmp, sizeof(tmp));
		
		if (!KernelPatcher::compatibleKernel(minKernel, maxKernel)) {
			if (!betaForAll && !PE_parse_boot_argn(bootargBeta, tmp, sizeof(tmp))) {
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
	ADDPR(debugEnabled) |= PE_parse_boot_argn(bootargDebug, tmp, sizeof(tmp));

	allowDecompress = !PE_parse_boot_argn(bootargLowMem, tmp, sizeof(tmp));
	
	installOrRecovery |= PE_parse_boot_argn("rp0", tmp, sizeof(tmp));
	installOrRecovery |= PE_parse_boot_argn("rp", tmp, sizeof(tmp));
	installOrRecovery |= PE_parse_boot_argn("container-dmg", tmp, sizeof(tmp));
	installOrRecovery |= PE_parse_boot_argn("root-dmg", tmp, sizeof(tmp));
	installOrRecovery |= PE_parse_boot_argn("auth-root-dmg", tmp, sizeof(tmp));

	safeMode = PE_parse_boot_argn("-x", tmp, sizeof(tmp));

	preferSlowMode = getKernelVersion() <= KernelVersion::Mavericks || installOrRecovery;

	if (PE_parse_boot_argn(bootargSlow, tmp, sizeof(tmp))) {
		preferSlowMode = true;
	} else if (PE_parse_boot_argn(bootargFast, tmp, sizeof(tmp))) {
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

	if (config.getBootArguments()) {
		DBGLOG("init", "initialising policy");
		
		lilu.init();
		
		if (config.policy.registerPolicy())
			config.startSuccess = true;
		else
			SYSLOG("init", "failed to register the policy");
	}
	
	return KERN_SUCCESS;
}

extern "C" kern_return_t kern_stop(kmod_info_t *ki, void *d) {
	return config.startSuccess ? KERN_FAILURE : KERN_SUCCESS;
}
