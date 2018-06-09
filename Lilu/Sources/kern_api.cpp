//
//  kern_api.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <PrivateHeaders/kern_config.hpp>
#include <Headers/kern_api.hpp>

#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>

LiluAPI lilu;

void LiluAPI::init() {
	access = IOLockAlloc();

	if (config.installOrRecovery)
		currentRunMode |= AllowInstallerRecovery;
	else if (config.safeMode)
		currentRunMode |= AllowSafeMode;
	else
		currentRunMode |= AllowNormal;
}

void LiluAPI::deinit() {
	if (access) {
		IOLockFree(access);
		access = nullptr;
	}
}

LiluAPI::Error LiluAPI::requestAccess(size_t version, bool check) {
	if (!config.startSuccess)
		return Error::Offline;

	constexpr size_t currversion = parseModuleVersion(xStringify(MODULE_VERSION));
	if (version > currversion) {
		return Error::UnsupportedFeature;
	}

	if (check) {
		if (!IOLockTryLock(access)) {
			return Error::LockError;
		}
	} else {
		IOLockLock(access);
	}

	if (apiRequestsOver) {
		IOLockUnlock(access);
		return Error::TooLate;
	}

	return Error::NoError;
}

LiluAPI::Error LiluAPI::releaseAccess() {
	IOLockUnlock(access);
	return Error::NoError;
}

LiluAPI::Error LiluAPI::shouldLoad(const char *product, size_t version, uint32_t runmode, const char **disableArg, size_t disableArgNum, const char **debugArg, size_t debugArgNum, const char **betaArg, size_t betaArgNum, KernelVersion min, KernelVersion max, bool &printDebug) {

	DBGLOG("api", "got load request from %s (%lu)", product, version);

	char tmp[16];
	printDebug = false;

	if (!(runmode & currentRunMode))
		return Error::Disabled;

	for (size_t i = 0; i < disableArgNum; i++) {
		if (PE_parse_boot_argn(disableArg[i], tmp, sizeof(tmp)))
			return Error::Disabled;
	}

	if (!KernelPatcher::compatibleKernel(min, max)) {
		bool beta = config.betaForAll;

		for (size_t i = 0; i < betaArgNum && !beta; i++) {
			if (PE_parse_boot_argn(betaArg[i], tmp, sizeof(tmp)))
				beta = true;
		}

		if (!beta) {
			SYSLOG("api", "automatically disabling %s (%lu) on an unsupported operating system", product, version);
			return Error::IncompatibleOS;
		} else {
			SYSLOG("api", "force enabling %s (%lu) on an unsupported operating system due to beta flag", product, version);
		}
	}

	if (config.debugForAll) {
		printDebug = true;
	} else {
		for (size_t i = 0; i < debugArgNum; i++) {
			if (PE_parse_boot_argn(debugArg[i], tmp, sizeof(tmp))) {
				printDebug = true;
				break;
			}
		}
	}

	return Error::NoError;
}

LiluAPI::Error LiluAPI::onPatcherLoad(t_patcherLoaded callback, void *user) {
	auto *pcall = stored_pair<t_patcherLoaded>::create();

	if (!pcall) {
		SYSLOG("api", "failed to allocate memory for stored_pair<t_patcherLoaded>");
		return Error::MemoryError;
	}

	pcall->first = callback;
	pcall->second = user;

	if (!patcherLoadedCallbacks.push_back(pcall)) {
		SYSLOG("api", "failed to store stored_pair<t_patcherLoaded>");
		pcall->deleter(pcall);
		return Error::MemoryError;
	}

	return Error::NoError;
}

LiluAPI::Error LiluAPI::onKextLoad(KernelPatcher::KextInfo *infos, size_t num, t_kextLoaded callback, void *user) {
	// Store the callbacks first
	auto *pcall = stored_pair<t_kextLoaded>::create();

	if (!pcall) {
		SYSLOG("api", "failed to allocate memory for stored_pair<t_kextLoaded>");
		return Error::MemoryError;
	}

	pcall->first = callback;
	pcall->second = user;

	if (!kextLoadedCallbacks.push_back(pcall)) {
		SYSLOG("api", "failed to store stored_pair<t_kextLoaded>");
		pcall->deleter(pcall);
		return Error::MemoryError;
	}

	// Store the kexts next
	auto *pkext = stored_pair<KernelPatcher::KextInfo *, size_t>::create();

	if (!pkext) {
		SYSLOG("api", "failed to allocate memory for stored_pair<KextInfo>");
		return Error::MemoryError;
	}

	pkext->first = infos;
	pkext->second = num;

	if (!storedKexts.push_back(pkext)) {
		SYSLOG("api", "failed to store stored_pair<KextInfo>");
		pkext->deleter(pkext);
		return Error::MemoryError;
	}

	return Error::NoError;
}

LiluAPI::Error LiluAPI::onProcLoad(UserPatcher::ProcInfo *infos, size_t num, UserPatcher::t_BinaryLoaded callback, void *user, UserPatcher::BinaryModInfo *mods, size_t modnum) {
	// It seems to partially work
	// Offer no support for user patcher before 10.9
	//if (getKernelVersion() <= KernelVersion::MountainLion)
	//	return Error::IncompatibleOS;

	// Store the callbacks
	if (callback) {
		auto *pcall = stored_pair<UserPatcher::t_BinaryLoaded>::create();

		if (!pcall) {
			SYSLOG("api", "failed to allocate memory for stored_pair<t_binaryLoaded>");
			return Error::MemoryError;
		}

		pcall->first = callback;
		pcall->second = user;

		if (!binaryLoadedCallbacks.push_back(pcall)) {
			SYSLOG("api", "failed to store stored_pair<t_binaryLoaded>");
			pcall->deleter(pcall);
			return Error::MemoryError;
		}
	}

	// Filter disabled processes right away and store the rest
	for (size_t i = 0; i < num; i++) {
		if (infos[i].section != UserPatcher::ProcInfo::SectionDisabled &&
			!storedProcs.push_back(&infos[i])) {
			SYSLOG("api", "failed to store ProcInfo");
			return Error::MemoryError;
		}
	}

	// Store all the binary mods
	for (size_t i = 0; i < modnum; i++) {
		if (!storedBinaryMods.push_back(&mods[i])) {
			SYSLOG("api", "failed to store BinaryModInfo");
			return Error::MemoryError;
		}
	}

	return Error::NoError;
}

LiluAPI::Error LiluAPI::onEntitlementRequest(t_entitlementRequested callback, void *user) {
	auto *ecall = stored_pair<t_entitlementRequested>::create();

	if (!ecall) {
		SYSLOG("api", "failed to allocate memory for stored_pair<t_entitlementRequested>");
		return Error::MemoryError;
	}

	ecall->first = callback;
	ecall->second = user;

	if (!entitlementRequestedCallbacks.push_back(ecall)) {
		SYSLOG("api", "failed to store stored_pair<t_entitlementRequested>");
		ecall->deleter(ecall);
		return Error::MemoryError;
	}

	return Error::NoError;
}

void LiluAPI::processPatcherLoadCallbacks(KernelPatcher &patcher) {
	// Block any new requests
	IOLockLock(access);
	apiRequestsOver = true;
	IOLockUnlock(access);

	// Process the callbacks
	for (size_t i = 0; i < patcherLoadedCallbacks.size(); i++) {
		auto p = patcherLoadedCallbacks[i];
		p->first(p->second, patcher);
	}

	if (entitlementRequestedCallbacks.size() > 0) {
		auto entitlement = patcher.solveSymbol(KernelPatcher::KernelID, "__ZN12IOUserClient21copyClientEntitlementEP4taskPKc");

		if (entitlement) {
			orgCopyClientEntitlement = reinterpret_cast<t_copyClientEntitlement>(patcher.routeFunction(entitlement, reinterpret_cast<mach_vm_address_t>(copyClientEntitlement), true));
			if (patcher.getError() == KernelPatcher::Error::NoError)
				DBGLOG("api", "hooked copy user entitlement");
			else
				SYSLOG("api", "failed to hook copy user entitlement");
		} else {
			SYSLOG("api", "failed to solve copy user entitlement");
		}

		patcher.clearError();
	}

#ifdef LILU_KEXTPATCH_SUPPORT
	// Queue the kexts we are in need of waiting
	for (size_t i = 0; i < storedKexts.size(); i++) {
		auto stored = storedKexts[i];
		for (size_t j = 0; j < stored->second; j++) {
			if (stored->first[j].sys[KernelPatcher::KextInfo::Disabled])
				continue;

			if (stored->first[j].sys[KernelPatcher::KextInfo::FSOnly] && stored->first[j].pathNum == 0) {
				SYSLOG("api", "improper request with 0 paths for %s kext", stored->first[j].id);
				continue;
			}

			patcher.loadKinfo(&stored->first[j]);
			auto error = patcher.getError();
			if (error != KernelPatcher::Error::NoError) {
				patcher.clearError();
				if (error == KernelPatcher::Error::AlreadyDone) {
					if (stored->first[j].sys[KernelPatcher::KextInfo::Loaded] ||
						stored->first[j].sys[KernelPatcher::KextInfo::Reloadable]) {
						DBGLOG("api", "updating new kext handler features");
						patcher.updateKextHandlerFeatures(&stored->first[j]);
					}
				} else if (error != KernelPatcher::Error::Unsupported) {
					SYSLOG_COND(ADDPR(debugEnabled), "api", "failed to load %s kext file", stored->first[j].id);
				}

				// Depending on a system some kexts may actually not exist
				continue;
			}

			patcher.setupKextListening();

			if (patcher.getError() != KernelPatcher::Error::NoError) {
				SYSLOG("api", "failed to setup kext hooking");
				patcher.clearError();
				i = storedKexts.size();
				break;
			}

			auto handler = KernelPatcher::KextHandler::create(stored->first[j].id, stored->first[j].loadIndex,
			[](KernelPatcher::KextHandler *h) {
				if (h)
					lilu.processKextLoadCallbacks(*static_cast<KernelPatcher *>(h->self), h->index, h->address, h->size, h->reloadable);
				else
					SYSLOG("api", "kext notification callback arrived at nowhere");
			}, stored->first[j].sys[KernelPatcher::KextInfo::Loaded], stored->first[j].sys[KernelPatcher::KextInfo::Reloadable]);

			if (!handler) {
				SYSLOG("api", "failed to allocate KextHandler for %s", stored->first[j].id);
				i = storedKexts.size();
				break;
			}

			handler->self = &patcher;

			patcher.waitOnKext(handler);

			if (patcher.getError() != KernelPatcher::Error::NoError) {
				SYSLOG("api", "failed to wait on kext %s", stored->first[j].id);
				patcher.clearError();
				KernelPatcher::KextHandler::deleter(handler);
				i = storedKexts.size();
				break;
			}
		}
	}
#endif

	// We no longer need to load kexts, forget about prelinked
	patcher.freeFileBufferResources();
}

void LiluAPI::processKextLoadCallbacks(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size, bool reloadable) {
	// Update running info
	patcher.updateRunningInfo(id, slide, size, reloadable);

	// Process the callbacks
	for (size_t i = 0; i < kextLoadedCallbacks.size(); i++) {
		auto p = kextLoadedCallbacks[i];
		p->first(p->second, patcher, id, slide, size);
	}
}

void LiluAPI::processUserLoadCallbacks(UserPatcher &patcher) {
	if (storedProcs.size() == 0 && storedBinaryMods.size() == 0) {
		return;
	}

	if (!patcher.registerPatches(storedProcs.data(), storedProcs.size(), storedBinaryMods.data(), storedBinaryMods.size(),
		[](void *user, UserPatcher &patcher, vm_map_t map, const char *path, size_t len) {
			auto api = static_cast<LiluAPI *>(user);
			api->processBinaryLoadCallbacks(patcher, map, path, len);
		}, this)) {
		SYSLOG("api", "failed to register user patches");
	}
}

void LiluAPI::processBinaryLoadCallbacks(UserPatcher &patcher, vm_map_t map, const char *path, size_t len) {
	// Process the callbacks
	for (size_t i = 0; i < binaryLoadedCallbacks.size(); i++) {
		auto p = binaryLoadedCallbacks[i];
		p->first(p->second, patcher, map, path, len);
	}
}

OSObject *LiluAPI::copyClientEntitlement(task_t task, const char *entitlement) {
	if (lilu.orgCopyClientEntitlement) {
		auto obj = lilu.orgCopyClientEntitlement(task, entitlement);
		auto &callbacks = lilu.entitlementRequestedCallbacks;
		for (size_t i = 0, sz = callbacks.size(); i < sz; i++)
			callbacks[i]->first(callbacks[i]->second, task, entitlement, obj);
		return obj;
	}

	SYSLOG("api", "copy client entitlement arrived at nowhere");
	return nullptr;
}

void LiluAPI::activate(KernelPatcher &kpatcher, UserPatcher &upatcher) {
	kpatcher.activate();
	upatcher.activate();
}
