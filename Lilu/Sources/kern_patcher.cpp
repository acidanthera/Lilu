//
//  kern_patcher.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_compat.hpp>
#include <PrivateHeaders/kern_patcher.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_iokit.hpp>

#include <mach/mach_types.h>

#ifdef LILU_KEXTPATCH_SUPPORT
static KernelPatcher *that {nullptr};
static SInt32 updateSummariesEntryCount;
#endif /* LILU_KEXTPATCH_SUPPORT */

IOSimpleLock *KernelPatcher::kernelWriteLock {nullptr};

#include <Library/LegacyIOService.h>

KernelPatcher::Error KernelPatcher::getError() {
	return code;
}

void KernelPatcher::clearError() {
	code = Error::NoError;
}

void KernelPatcher::init() {
#ifdef LILU_COMPRESSION_SUPPORT
	if (WIOKit::usingPrelinkedCache()) {
		size_t id = loadKinfo("kernel", prelinkKernelPaths, arrsize(prelinkKernelPaths), true);
		if (getError() == Error::NoError && id == KernelID) {
			prelinkInfo = kinfos[KernelID];
		} else {
			DBGLOG("patcher", "got %d prelink error and %lu kernel id", getError(), id);
			clearError();
		}
	}
#endif

	if (!prelinkInfo) {
		size_t id = loadKinfo("kernel", kernelPaths, arrsize(kernelPaths), true);
		if (getError() != Error::NoError || id != KernelID) {
			DBGLOG("patcher", "got %d error and %lu kernel id", getError(), id);
			return;
		}
	}
	
	if (!kernelWriteLock) {
		kernelWriteLock = IOSimpleLockAlloc();
		if (!kernelWriteLock) {
			DBGLOG("patcher", "lock allocation failures");
			code = Error::LockError;
			return;
		}
	}
	
	if (kinfos[KernelID]->getRunningAddresses() != KERN_SUCCESS) {
		DBGLOG("patcher", "failed to get running kernel mach info");
		code = Error::KernRunningInitFailure;
		return;
	}
}

void KernelPatcher::deinit() {
	// Remove the patches
	if (kinfos.size() > 0) {
		if (kinfos[KernelID]->setKernelWriting(true, kernelWriteLock) == KERN_SUCCESS) {
			for (size_t i = 0, n = kpatches.size(); i < n; i++) {
				kpatches[i]->restore();
			}
			kinfos[KernelID]->setKernelWriting(false, kernelWriteLock);
		} else {
			SYSLOG("patcher", "failed to change kernel protection at patch removal");
		}
	}
	kpatches.deinit();
	
	// Deallocate kinfos
	kinfos.deinit();
	
	// Deallocate pages
	kpages.deinit();
	
	// It is assumed that only one active instance of KernelPatcher is allowed
	if (kernelWriteLock) {
		IOSimpleLockFree(kernelWriteLock);
		kernelWriteLock = nullptr;
	}
}

size_t KernelPatcher::loadKinfo(const char *id, const char * const paths[], size_t num, bool isKernel, bool fsonly, bool fsfallback) {
	for (size_t i = 0; i < kinfos.size(); i++) {
		if (kinfos[i]->objectId && !strcmp(kinfos[i]->objectId, id)) {
			DBGLOG("patcher", "found an already loaded MachInfo for %s at %lu", id, i);
			code = Error::AlreadyDone;
			return i;
		}
	}

	MachInfo *prelink = nullptr;
	if (!fsonly && !isKernel && kinfos.size() > KernelID)
		prelink = prelinkInfo;

	kern_return_t error;
	auto info = MachInfo::create(isKernel, id);
	if (!info) {
		SYSLOG("patcher", "failed to allocate MachInfo for %s", id);
		code = Error::MemoryIssue;
	} else if ((error = info->init(paths, num, prelink, fsfallback)) != KERN_SUCCESS) {
		if (error != KERN_NOT_SUPPORTED) {
			SYSLOG_COND(ADDPR(debugEnabled), "patcher", "failed to init MachInfo for %s", id);
			code = Error::NoKinfoFound;
		} else {
			DBGLOG("patcher", "ignoring %s because it is unused", id);
			code = Error::Unsupported;
		}
	} else if (!kinfos.push_back(info)) {
		SYSLOG("patcher", "unable to store loaded MachInfo for %s", id);
		code = Error::MemoryIssue;
	} else {
		return kinfos.last();
	}
	
	if (info) {
		info->deinit();
		MachInfo::deleter(info);
	}

	return INVALID;
}

#ifdef LILU_KEXTPATCH_SUPPORT
size_t KernelPatcher::loadKinfo(KernelPatcher::KextInfo *info) {
	if (!info) {
		SYSLOG("patcher", "loadKinfo got a null info");
		code = Error::MemoryIssue;
		return INVALID;
	}

	if (info->loadIndex != KernelPatcher::KextInfo::Unloaded) {
		DBGLOG("patcher", "provided KextInfo (%s) has already been loaded at %lu index", info->id, info->loadIndex);
		return info->loadIndex;
	}
	
	auto idx = loadKinfo(info->id, info->paths, info->pathNum, false,
	                     info->sys[KextInfo::FSOnly], info->sys[KextInfo::FSFallback]);
	if (getError() == Error::NoError || getError() == Error::AlreadyDone) {
		info->loadIndex = idx;
		DBGLOG("patcher", "loaded kinfo %s at %lu index", info->id, idx);
	}
	
	return idx;
}
#endif /* KEXTPATH_SUPPORT */

void KernelPatcher::updateRunningInfo(size_t id, mach_vm_address_t slide, size_t size, bool force) {
	if (id >= kinfos.size()) {
		SYSLOG("patcher", "invalid kinfo id %lu for running info update", id);
		return;
	}
	
	if (kinfos[id]->getRunningAddresses(slide, size, force) != KERN_SUCCESS) {
		SYSLOG("patcher", "failed to retrieve running info");
		code = Error::KernRunningInitFailure;
	}
}

bool KernelPatcher::compatibleKernel(uint32_t min, uint32_t max) {
	return (min == KernelAny || min <= getKernelVersion()) &&
			(max == KernelAny || max >= getKernelVersion());
}

mach_vm_address_t KernelPatcher::solveSymbol(size_t id, const char *symbol) {
	if (id < kinfos.size()) {
		auto addr = kinfos[id]->solveSymbol(symbol);
		if (addr) {
			return addr;
		}
	} else {
		SYSLOG("patcher", "invalid kinfo id %lu for %s symbol lookup", id, symbol);
	}

	code = Error::NoSymbolFound;
	return 0;
}

#ifdef LILU_KEXTPATCH_SUPPORT
void KernelPatcher::setupKextListening() {
	// We have already done this
	if (that) return;
	
	loadedKextSummaries = reinterpret_cast<OSKextLoadedKextSummaryHeader **>(solveSymbol(KernelID, "_gLoadedKextSummaries"));

	if (loadedKextSummaries) {
		DBGLOG("patcher", "_gLoadedKextSummaries address %p", loadedKextSummaries);
	} else {
		code = Error::NoSymbolFound;
		return;
	}
	
	bool hookOuter = getKernelVersion() >= KernelVersion::Sierra;
	
	mach_vm_address_t s = solveSymbol(KernelID, hookOuter ?
									  "__ZN6OSKext25updateLoadedKextSummariesEv" :
									  "_OSKextLoadedKextSummariesUpdated");
	
	if (s) {
		DBGLOG("patcher", "kext summaries (%d) address %llX value %llX", hookOuter, s, *reinterpret_cast<uint64_t *>(s));
	} else {
		code = Error::NoSymbolFound;
		return;
	}
	
	if (hookOuter) {
		orgUpdateLoadedKextSummaries = reinterpret_cast<void(*)(void)>(
			routeFunction(s, reinterpret_cast<mach_vm_address_t>(onKextSummariesUpdated), true, true)
		);
	} else {
		routeFunction(s, reinterpret_cast<mach_vm_address_t>(onKextSummariesUpdated));
	}
	
	if (getError() == Error::NoError) {
		// Allow static functions to access the patcher body
		that = this;
	}
}

void KernelPatcher::waitOnKext(KextHandler *handler) {
	if (!that) {
		SYSLOG("patcher", "you should have called setupKextListening first");
		code = Error::KextListeningFailure;
		return;
	}
	
	// If we need to process already loaded kexts, do it
	if (handler->loaded)
		waitingForAlreadyLoadedKexts = true;
	
	if (!khandlers.push_back(handler)) {
		code = Error::MemoryIssue;
	}
}

void KernelPatcher::updateKextHandlerFeatures(KextInfo *info) {
	for (size_t i = 0; i < khandlers.size(); i++) {
		if (!strcmp(khandlers[i]->id, info->id)) {
			khandlers[i]->loaded |= info->sys[KextInfo::Loaded];
			khandlers[i]->reloadable |= info->sys[KextInfo::Reloadable];
			break;
		}
	}
}

void KernelPatcher::applyLookupPatch(const LookupPatch *patch) {
	if (!patch || !patch->kext || patch->kext->loadIndex == KextInfo::Unloaded) {
		SYSLOG("patcher", "an invalid lookup patch provided");
		code = Error::MemoryIssue;
		return;
	}
	
	uint8_t *off, *curr;
	size_t size;
	auto kinfo = kinfos[patch->kext->loadIndex];
	kinfo->getRunningPosition(off, size);
	
	curr = off;
	off += size - patch->size;
	size_t changes {0};
	
	if (kinfo->setKernelWriting(true, kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("patcher", "lookup patching failed to write to kernel");
		code = Error::MemoryProtection;
		return;
	}
	
	for (size_t i = 0; curr < off && (i < patch->count || patch->count == 0); i++) {
		while (curr < off && memcmp(curr, patch->find, patch->size))
			curr++;
		
		if (curr != off) {
			for (size_t j = 0; j < patch->size; j++)
				curr[j] = patch->replace[j];
			changes++;
		}
	}
	
	if (kinfo->setKernelWriting(false, kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("patcher", "lookup patching failed to disable kernel writing");
		code = Error::MemoryProtection;
		return;
	}
	
	if (changes != patch->count) {
		SYSLOG_COND(ADDPR(debugEnabled), "patcher", "lookup patching applied only %lu patches out of %lu", changes, patch->count);
		code = Error::MemoryIssue;
	}
}
#else
void KernelPatcher::setupKextListening() {
	code = Error::Unsupported;
}
#endif /* LILU_KEXTPATCH_SUPPORT */

void KernelPatcher::freeFileBufferResources() {
	if (kinfos.size() > KernelID)
		kinfos[KernelID]->freeFileBufferResources();
}

void KernelPatcher::activate() {
	activated = true;
}

mach_vm_address_t KernelPatcher::routeFunction(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper, bool kernelRoute, bool revertible) {
	mach_vm_address_t diff = (to - (from + SmallJump));
	int32_t newArgument = static_cast<int32_t>(diff);
	
	DBGLOG("patcher", "from " PRIKADDR " to " PRIKADDR " diff " PRIKADDR " argument %X", CASTKADDR(from), CASTKADDR(from), CASTKADDR(diff), newArgument);
	
	bool absolute {false};
	
	if (diff != static_cast<mach_vm_address_t>(newArgument)) {
		DBGLOG("patcher", "will use absolute jumping to %llX", to);
		absolute = true;
	}
	
	mach_vm_address_t trampoline {0};
	
	if (buildWrapper) {
		trampoline = createTrampoline(from, absolute ? LongJump : SmallJump);
		if (!trampoline) return EINVAL;
	}
	
	Patch::All *opcode = nullptr, *argument = nullptr, *disp = nullptr;
	if (absolute) {
		opcode = Patch::create<Patch::Variant::U16>(from, 0x25FF);
		argument = Patch::create<Patch::Variant::U32>(from+2, 0);
		disp = Patch::create<Patch::Variant::U64>(from+6, to);
	} else {
		opcode = Patch::create<Patch::Variant::U8>(from, 0xE9);
		argument = Patch::create<Patch::Variant::U32>(from+1, newArgument);
	}
	
	if (!opcode || !argument || (absolute && !disp)) {
		SYSLOG("patcher", "cannot create the necessary patches");
		code = Error::MemoryIssue;
		Patch::deleter(opcode); Patch::deleter(argument);
		if (disp) Patch::deleter(disp);
		return EINVAL;
	}
	
	if (kernelRoute && kinfos[KernelID]->setKernelWriting(true, kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("patcher", "cannot change kernel memory protection");
		code = Error::MemoryProtection;
		Patch::deleter(opcode); Patch::deleter(argument);
		if (disp) Patch::deleter(disp);
		return EINVAL;
	}
	
	opcode->patch();
	argument->patch();
	if (disp) disp->patch();

	if (kernelRoute) {
		kinfos[KernelID]->setKernelWriting(false, kernelWriteLock);

		if (revertible) {
			auto oidx = kpatches.push_back(opcode);
			auto aidx = kpatches.push_back(argument);
			auto didx = disp ? kpatches.push_back(disp) : 0;

			if (oidx && aidx && (!disp || didx))
				return trampoline;
			
			SYSLOG("patcher", "failed to store patches for later removal, you are in trouble");
			if (oidx) kpatches.erase(oidx);
			if (aidx) kpatches.erase(aidx);
			if (didx) kpatches.erase(didx);
		}
	}
	
	Patch::deleter(opcode); Patch::deleter(argument);
	if (disp) Patch::deleter(disp);
	return trampoline;
}

mach_vm_address_t KernelPatcher::routeBlock(mach_vm_address_t from, const uint8_t *opcodes, size_t opnum, bool buildWrapper, bool kernelRoute) {
	// Simply overwrite the function in the easiest case
	if (!buildWrapper) {
		if (!kernelRoute || kinfos[KernelID]->setKernelWriting(true, kernelWriteLock) == KERN_SUCCESS) {
			lilu_os_memcpy(reinterpret_cast<void *>(from), opcodes, opnum);
			if (kernelRoute)
				kinfos[KernelID]->setKernelWriting(false, kernelWriteLock);
		} else {
			SYSLOG("patcher", "block overwrite failed to change protection");
			code = Error::MemoryProtection;
			return EINVAL;
		}
		
		return 0;
	} else if (!kernelRoute) {
		SYSLOG("patcher", "cannot generate blocks outside the kernelspace");
		code = Error::MemoryProtection;
		return EINVAL;
	}
	
	// Otherwise generate a trampoline with opcodes
	mach_vm_address_t trampoline = createTrampoline(from, LongJump, opcodes, opnum);
	if (!trampoline) return EINVAL;

	// And redirect the original function to it
	return routeFunction(from, trampoline) == 0 ? trampoline : EINVAL;
}

bool KernelPatcher::routeMultiple(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start, size_t size, bool kernelRoute, bool force) {
	bool errorsFound = false;
	for (size_t i = 0; i < num; i++) {
		auto &request = requests[i];
		request.from = solveSymbol(id, request.symbol, start, size, true);
		if (!request.from) {
			SYSLOG("patcher", "failed to solve %s, err %d", request.symbol, getError());
			clearError();
			errorsFound = true;
			if (!force) return false;
		}
	}

	for (size_t i = 0; i < num; i++) {
		auto &request = requests[i];
		if (!request.from) continue;
		auto wrapper = routeFunction(request.from, request.to, request.org, kernelRoute, true);
		if (request.org) {
			if (wrapper) {
				DBGLOG("patcher", "wrapped %s", request.symbol);
				*request.org = wrapper;
			} else {
				SYSLOG("patcher", "failed to wrap %s, err %d", request.symbol, getError());
				clearError();
				errorsFound = true;
				if (!force) return false;
			}
		} else {
			if (wrapper) {
				DBGLOG("patcher", "routed %s", request.symbol);
			} else {
				SYSLOG("patcher", "failed to route %s, err %d", request.symbol, getError());
				clearError();
				errorsFound = true;
				if (!force) return false;
			}
		}
	}

	return !errorsFound;
}

uint8_t KernelPatcher::tempExecutableMemory[TempExecutableMemorySize] __attribute__((section("__TEXT,__text")));

mach_vm_address_t KernelPatcher::createTrampoline(mach_vm_address_t func, size_t min, const uint8_t *opcodes, size_t opnum) {
	// Doing it earlier to workaround stack corruption due to a possible 10.12 bug.
	// Otherwise in rare cases there will be random KPs with corrupted stack data.
	if (kinfos[KernelID]->setKernelWriting(true, kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("patcher", "failed to set executable permissions");
		code = Error::MemoryProtection;
		return 0;
	}
	
	// Relative destination offset
	size_t off = Disassembler::quickInstructionSize(func, min);
	
	if (!off || off > PAGE_SIZE - LongJump) {
		kinfos[KernelID]->setKernelWriting(false, kernelWriteLock);
		SYSLOG("patcher", "unsupported destination offset %lu", off);
		code = Error::DisasmFailure;
		return 0;
	}
	
	uint8_t *tempDataPtr = reinterpret_cast<uint8_t *>(tempExecutableMemory) + tempExecutableMemoryOff;
	
	tempExecutableMemoryOff += off + LongJump + opnum;
	
	if (tempExecutableMemoryOff >= TempExecutableMemorySize) {
		kinfos[KernelID]->setKernelWriting(false, kernelWriteLock);
		SYSLOG("patcher", "not enough executable memory requested %lld have %lu", tempExecutableMemoryOff+1, TempExecutableMemorySize);
		code = Error::DisasmFailure;
	} else {
		// Copy the opcodes if any
		if (opnum > 0)
			lilu_os_memcpy(tempDataPtr, opcodes, opnum);
		
		// Copy the prologue, assuming it is PIC
		lilu_os_memcpy(tempDataPtr + opnum, reinterpret_cast<void *>(func), off);

		kinfos[KernelID]->setKernelWriting(false, kernelWriteLock);
		
		// Add a jump
		routeFunction(reinterpret_cast<mach_vm_address_t>(tempDataPtr+opnum+off), func+off, false, true, false);
		
		if (getError() == Error::NoError) {
			return reinterpret_cast<mach_vm_address_t>(tempDataPtr);
		} else {
			SYSLOG("patcher", "failed to route an inner trempoline");
		}
	}
	
	return 0;
}

#ifdef LILU_KEXTPATCH_SUPPORT
void KernelPatcher::onKextSummariesUpdated() {
	if (that) {
		// macOS 10.12 generates an interrupt during this call but unlike 10.11 and below
		// it never stops handling interrupts hanging forever inside hndl_allintrs.
		// This happens even with cpus=1, and the reason is not fully understood.
		//
		// For this reason on 10.12 and above the outer function is routed, and so far it
		// seems to cause fewer issues. Regarding syncing:
		//  - the only place modifying gLoadedKextSummaries is updateLoadedKextSummaries;
		//  - updateLoadedKextSummaries is called from load/unload separately;
		//  - sKextSummariesLock is not exported or visible.
		// As a result no syncing should be necessary but there are guards for future
		// changes and in case of any misunderstanding.
		
		if (getKernelVersion() >= KernelVersion::Sierra) {
			if (OSIncrementAtomic(&updateSummariesEntryCount) != 0) {
				PANIC("patcher", "onKextSummariesUpdated entered another time");
			}
			
			that->orgUpdateLoadedKextSummaries();
		}

		DBGLOG("patcher", "invoked at kext loading/unloading");
		
		if (that->activated && that->loadedKextSummaries) {
			auto num = (*that->loadedKextSummaries)->numSummaries;
			if (num > 0) {
				if (that->waitingForAlreadyLoadedKexts) {
					that->processAlreadyLoadedKexts((*that->loadedKextSummaries)->summaries, num);
					that->waitingForAlreadyLoadedKexts = false;
				}
				if (that->khandlers.size() > 0) {
					OSKextLoadedKextSummary &last = (*that->loadedKextSummaries)->summaries[num-1];
					DBGLOG("patcher", "last kext is %llX and its name is %.*s", last.address, KMOD_MAX_NAME, last.name);
					// We may add khandlers items inside the handler
					for (size_t i = 0; i < that->khandlers.size(); i++) {
						auto handler = that->khandlers[i];
						if (!strncmp(handler->id, last.name, KMOD_MAX_NAME)) {
							DBGLOG("patcher", "caught the right kext at " PRIKADDR ", invoking handler", CASTKADDR(last.address));
							if (!that->kinfos[handler->index]->isCurrentBinary(last.address)) {
								SYSLOG("patcher", "uuid mismatch for %s at " PRIKADDR ", ignoring", last.name, CASTKADDR(last.address));
								continue;
							}
							handler->address = last.address;
							handler->size = last.size;
							handler->handler(handler);
							// Remove the item
							if (!that->khandlers[i]->reloadable)
								that->khandlers.erase(i);
							break;
						}
					}
				}
			} else {
				SYSLOG("patcher", "no kext is currently loaded, this should not happen");
			}
		}
		
		if (getKernelVersion() >= KernelVersion::Sierra && OSDecrementAtomic(&updateSummariesEntryCount) != 1) {
			PANIC("patcher", "onKextSummariesUpdated left another time");
		}
	}
}

void KernelPatcher::processAlreadyLoadedKexts(OSKextLoadedKextSummary *summaries, size_t num) {
	DBGLOG("patcher", "processing already loaded kexts by iterating over %lu summaries", num);
	
	for (size_t i = 0; i < num; i++) {
		auto curr = summaries[i];
		for (size_t j = 0; j < khandlers.size(); j++) {
			auto handler = khandlers[j];
			if (handler->loaded) {
				if (!strncmp(handler->id, curr.name, KMOD_MAX_NAME)) {
					DBGLOG("patcher", "discovered the right kext %s at " PRIKADDR ", invoking handler", curr.name, CASTKADDR(curr.address));
					if (!kinfos[handler->index]->isCurrentBinary(curr.address)) {
						SYSLOG("patcher", "uuid mismatch for %s at " PRIKADDR ", ignoring", curr.name, CASTKADDR(curr.address));
						continue;
					}
					handler->address = curr.address;
					handler->size = curr.size;
					handler->handler(handler);
					// Remove the item
					if (!that->khandlers[j]->reloadable)
						that->khandlers.erase(j);
					break;
				}
			}
		}
	}
}
#endif /* LILU_KEXTPATCH_SUPPORT */
