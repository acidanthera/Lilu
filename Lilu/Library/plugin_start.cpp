//
//  plugin_start.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_version.hpp>

#ifndef LILU_CUSTOM_KMOD_INIT
bool ADDPR(startSuccess) = false;
#else
// Workaround custom kmod code and enable by default
bool ADDPR(startSuccess) = true;
#endif /* LILU_CUSTOM_KMOD_INIT */

bool ADDPR(debugEnabled) = false;
uint32_t ADDPR(debugPrintDelay) = 0;

#ifndef LILU_CUSTOM_IOKIT_INIT

OSDefineMetaClassAndStructors(PRODUCT_NAME, IOService)

PRODUCT_NAME *ADDPR(selfInstance) = nullptr;

IOService *PRODUCT_NAME::probe(IOService *provider, SInt32 *score) {
	ADDPR(selfInstance) = this;
	setProperty("VersionInfo", kextVersion);
	auto service = IOService::probe(provider, score);
	return ADDPR(startSuccess) ? service : nullptr;
}

bool PRODUCT_NAME::start(IOService *provider) {
	ADDPR(selfInstance) = this;
	if (!IOService::start(provider)) {
		SYSLOG("init", "failed to start the parent");
		return false;
	}

	return ADDPR(startSuccess);
}

void PRODUCT_NAME::stop(IOService *provider) {
	ADDPR(selfInstance) = nullptr;
	IOService::stop(provider);
}

#endif /* LILU_CUSTOM_IOKIT_INIT */

#ifndef LILU_CUSTOM_KMOD_INIT

EXPORT extern "C" kern_return_t ADDPR(kern_start)(kmod_info_t *, void *) {
	// This is an ugly hack necessary on some systems where buffering kills most of debug output.
	PE_parse_boot_argn("liludelay", &ADDPR(debugPrintDelay), sizeof(ADDPR(debugPrintDelay)));

	auto error = lilu.requestAccess();
	if (error == LiluAPI::Error::NoError) {
		error = lilu.shouldLoad(ADDPR(config).product, ADDPR(config).version, ADDPR(config).runmode, ADDPR(config).disableArg, ADDPR(config).disableArgNum,
								ADDPR(config).debugArg, ADDPR(config).debugArgNum, ADDPR(config).betaArg, ADDPR(config).betaArgNum, ADDPR(config).minKernel,
								ADDPR(config).maxKernel, ADDPR(debugEnabled));

		if (error == LiluAPI::Error::NoError) {
			DBGLOG("init", "%s bootstrap %s", xStringify(PRODUCT_NAME), kextVersion);
			(void)kextVersion;
			ADDPR(startSuccess) = true;
			ADDPR(config).pluginStart();
		} else {
			SYSLOG("init", "parent said we should not continue %d", error);
		}

		lilu.releaseAccess();
	} else {
		SYSLOG("init", "failed to call parent %d", error);

		for (size_t i = 0; i < ADDPR(config).debugArgNum; i++) {
			if (checkKernelArgument(ADDPR(config).debugArg[i])) {
				ADDPR(debugEnabled) = true;
				break;
			}
		}

		if (checkKernelArgument("-liludbgall"))
			ADDPR(debugEnabled) = true;
	}

	// Report success but actually do not start and let I/O Kit unload us.
	// This works better and increases boot speed in some cases.
	return KERN_SUCCESS;
}

EXPORT extern "C" kern_return_t ADDPR(kern_stop)(kmod_info_t *, void *) {
	// It is not safe to unload Lilu plugins unless they were disabled!
	return ADDPR(startSuccess) ? KERN_FAILURE : KERN_SUCCESS;
}

#endif /* LILU_CUSTOM_KMOD_INIT */
