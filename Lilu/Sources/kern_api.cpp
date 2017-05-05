//
//  kern_api.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_api.hpp>

#include <IOKit/IOLib.h>
#include <IOKit/IORegistryEntry.h>

LiluAPI lilu;

void LiluAPI::init() {
	access = IOSimpleLockAlloc();
}

void LiluAPI::deinit() {
	if (access) {
		IOSimpleLockFree(access);
		access = nullptr;
	}
}

LiluAPI::Error LiluAPI::requestAccess(size_t version, bool check) {
	constexpr size_t currversion = parseModuleVersion(xStringify(MODULE_VERSION));
	if (version > currversion) {
		return Error::UnsupportedFeature;
	}
	
	if (check) {
		if (!IOSimpleLockTryLock(access)) {
			return Error::LockError;
		}
	} else {
		IOSimpleLockLock(access);
	}
	
	if (apiRequestsOver) {
		IOSimpleLockUnlock(access);
		return Error::TooLate;
	}
	
	return Error::NoError;
}

LiluAPI::Error LiluAPI::releaseAccess() {
	IOSimpleLockUnlock(access);
	return Error::NoError;
}

LiluAPI::Error LiluAPI::shouldLoad(const char *product, size_t version, const char **disableArg, size_t disableArgNum, const char **debugArg, size_t debugArgNum, const char **betaArg, size_t betaArgNum, KernelVersion min, KernelVersion max, bool &printDebug) {
	
	DBGLOG("api @ got load request from %s (%zu)", product, version);
	
	char tmp[16];
	printDebug = false;
	
	for (size_t i = 0; i < disableArgNum; i++) {
		if (PE_parse_boot_argn(disableArg[i], tmp, sizeof(tmp)))
			return Error::Disabled;
	}
	
	if (!KernelPatcher::compatibleKernel(min, max)) {
		bool beta = false;
		
		for (size_t i = 0; i < betaArgNum; i++) {
			if (PE_parse_boot_argn(betaArg[i], tmp, sizeof(tmp))) {
				beta = true;
				break;
			}
		}
		
		if (!beta) {
			SYSLOG("api @ automatically disabling %s (%zu) on an unsupported operating system", product, version);
			return Error::IncompatibleOS;
		} else {
			SYSLOG("api @ force enabling %s (%zu) on an unsupported operating system due to beta flag", product, version);
		}
	}
	
	for (size_t i = 0; i < debugArgNum; i++) {
		if (PE_parse_boot_argn(debugArg[i], tmp, sizeof(tmp))) {
			printDebug = true;
			break;
		}
	}
	
	return Error::NoError;
}

LiluAPI::Error LiluAPI::onPatcherLoad(t_patcherLoaded callback, void *user) {
	auto *pcall = stored_pair<t_patcherLoaded>::create();
	
	if (!pcall) {
		SYSLOG("api @ failed to allocate memory for stored_pair<t_patcherLoaded>");
		return Error::MemoryError;
	}
	
	pcall->first = callback;
	pcall->second = user;
	
	if (!patcherLoadedCallbacks.push_back(pcall)) {
		SYSLOG("api @ failed to store stored_pair<t_patcherLoaded>");
		pcall->deleter(pcall);
		return Error::MemoryError;
	}
	
	return Error::NoError;
}

LiluAPI::Error LiluAPI::onKextLoad(KernelPatcher::KextInfo *infos, size_t num, t_kextLoaded callback, void *user) {
	// Store the callbacks first
	auto *pcall = stored_pair<t_kextLoaded>::create();
	
	if (!pcall) {
		SYSLOG("api @ failed to allocate memory for stored_pair<t_kextLoaded>");
		return Error::MemoryError;
	}
	
	pcall->first = callback;
	pcall->second = user;
	
	if (!kextLoadedCallbacks.push_back(pcall)) {
		SYSLOG("api @ failed to store stored_pair<t_kextLoaded>");
		pcall->deleter(pcall);
		return Error::MemoryError;
	}
	
	// Store the kexts next
	auto *pkext = stored_pair<KernelPatcher::KextInfo *, size_t>::create();
	
	if (!pkext) {
		SYSLOG("api @ failed to allocate memory for stored_pair<KextInfo>");
		return Error::MemoryError;
	}
	
	pkext->first = infos;
	pkext->second = num;

	if (!storedKexts.push_back(pkext)) {
		SYSLOG("api @ failed to store stored_pair<KextInfo>");
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
			SYSLOG("api @ failed to allocate memory for stored_pair<t_binaryLoaded>");
			return Error::MemoryError;
		}
		
		pcall->first = callback;
		pcall->second = user;
		
		if (!binaryLoadedCallbacks.push_back(pcall)) {
			SYSLOG("api @ failed to store stored_pair<t_binaryLoaded>");
			pcall->deleter(pcall);
			return Error::MemoryError;
		}
	}
	
	// Filter disabled processes right away and store the rest
	for (size_t i = 0; i < num; i++) {
		if (infos[i].section && !storedProcs.push_back(&infos[i])) {
			SYSLOG("api @ failed to store ProcInfo");
			return Error::MemoryError;
		}
	}
	
	// Store all the binary mods
	for (size_t i = 0; i < modnum; i++) {
		if (!storedBinaryMods.push_back(&mods[i])) {
			SYSLOG("api @ failed to store BinaryModInfo");
			return Error::MemoryError;
		}
	}
	return Error::UnsupportedFeature;
}

void LiluAPI::processPatcherLoadCallbacks(KernelPatcher &patcher) {
	// Block any new requests
	IOSimpleLockLock(access);
	apiRequestsOver = true;
	IOSimpleLockUnlock(access);
	
	// Process the callbacks
	for (size_t i = 0; i < patcherLoadedCallbacks.size(); i++) {
		auto p = patcherLoadedCallbacks[i];
		p->first(p->second, patcher);
	}
	
	// Queue the kexts we are in need of waiting
	for (size_t i = 0; i < storedKexts.size(); i++) {
		auto stored = storedKexts[i];
		for (size_t j = 0; j < stored->second; j++) {
			patcher.loadKinfo(&stored->first[j]);
			if (patcher.getError() != KernelPatcher::Error::NoError) {
				if (patcher.getError() != KernelPatcher::Error::AlreadyDone)
					SYSLOG("api @ failed to load %s kext file", stored->first[j].id);
				patcher.clearError();
				// Depending on a system some kexts may actually not exist
				continue;
			}
			
			patcher.setupKextListening();
			
			if (patcher.getError() != KernelPatcher::Error::NoError) {
				SYSLOG("api @ failed to setup kext hooking");
				patcher.clearError();
				return;
			}
			
			auto handler = KernelPatcher::KextHandler::create(stored->first[j].id, stored->first[j].loadIndex,
			[](KernelPatcher::KextHandler *h) {
				if (h)
					lilu.processKextLoadCallbacks(*static_cast<KernelPatcher *>(h->self), h->index, h->address, h->size);
				else
					SYSLOG("api @ kext notification callback arrived at nowhere");
			}, stored->first[j].loaded);
			
			if (!handler) {
				SYSLOG("api @ failed to allocate KextHandler for %s", stored->first[j].id);
				return;
			}
			
			handler->self = &patcher;
			
			patcher.waitOnKext(handler);
			
			if (patcher.getError() != KernelPatcher::Error::NoError) {
				SYSLOG("api @ failed to wait on kext %s", stored->first[j].id);
				patcher.clearError();
				KernelPatcher::KextHandler::deleter(handler);
				return;
			}
		}
	}
}

void LiluAPI::processKextLoadCallbacks(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
	// Update running info
	patcher.updateRunningInfo(id, slide, size);
	
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
	
	if (!patcher.registerPatches(storedProcs.data(), storedProcs.size(),
								 storedBinaryMods.data(), storedBinaryMods.size(),
								 [](void *user, UserPatcher &patcher, vm_map_t map, const char *path, size_t len) {
									 auto api = static_cast<LiluAPI *>(user);
									 api->processBinaryLoadCallbacks(patcher, map, path, len);
								 }, this)) {
		SYSLOG("api @ failed to register user patches");
	}
}

void LiluAPI::processBinaryLoadCallbacks(UserPatcher &patcher, vm_map_t map, const char *path, size_t len) {
	// Process the callbacks
	for (size_t i = 0; i < binaryLoadedCallbacks.size(); i++) {
		auto p = binaryLoadedCallbacks[i];
		p->first(p->second, patcher, map, path, len);
	}
}

