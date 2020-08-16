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

#include <Library/LegacyIOService.h>

#ifdef LILU_KEXTPATCH_SUPPORT
static KernelPatcher *that {nullptr};
static SInt32 updateSummariesEntryCount;
#endif /* LILU_KEXTPATCH_SUPPORT */

IOSimpleLock *KernelPatcher::kernelWriteLock {nullptr};

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
		if (MachInfo::setKernelWriting(true, kernelWriteLock) == KERN_SUCCESS) {
			for (size_t i = 0, n = kpatches.size(); i < n; i++) {
				kpatches[i]->restore();
			}
			MachInfo::setKernelWriting(false, kernelWriteLock);
		} else {
			SYSLOG("patcher", "failed to change kernel protection at patch removal");
		}
	}
	kpatches.deinit();

	// Deallocate kinfos
	kinfos.deinit();

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
	} else if (!kinfos.push_back<2>(info)) {
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

size_t KernelPatcher::updateRunningInfo(size_t id, mach_vm_address_t slide, size_t size, bool force) {
	if (id >= kinfos.size()) {
		SYSLOG("patcher", "invalid kinfo id %lu for running info update", id);
		return size;
	}

	if (kinfos[id]->getRunningAddresses(slide, size, force) != KERN_SUCCESS) {
		SYSLOG("patcher", "failed to retrieve running info");
		code = Error::KernRunningInitFailure;
	}

	// In 11.0 khandler's size only contains __TEXT. We need to update this.
	uint8_t *hdr = nullptr;
	size_t nsize = 0;
	kinfos[id]->getRunningPosition(hdr, nsize);
	return nsize > size ? nsize: size;
}

bool KernelPatcher::compatibleKernel(uint32_t min, uint32_t max) {
	return (min == KernelAny || min <= getKernelVersion()) &&
			(max == KernelAny || max >= getKernelVersion());
}

void KernelPatcher::eraseCoverageInstPrefix(mach_vm_address_t addr, size_t count) {
	eraseCoverageInstPrefix(addr, count, -1);
}

void KernelPatcher::eraseCoverageInstPrefix(mach_vm_address_t addr, size_t count, off_t limit) {
	static constexpr uint8_t IncInstPrefix[] {0x48, 0xFF, 0x05}; // inc qword ptr [rip + (disp32 in next 4 bytes)]
	static constexpr size_t IncInstSize {7};

	off_t totalInstSize = 0;
	for (size_t i = 0; i < count; i++) {
		auto instSize = Disassembler::quickInstructionSize(reinterpret_cast<mach_vm_address_t>(addr), 1);
		if (instSize == 0) break; // Unknown instruction

		if (instSize == IncInstSize && !memcmp(reinterpret_cast<void *>(addr), IncInstPrefix, sizeof(IncInstPrefix))) {
			auto status = MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock);
			if (status == KERN_SUCCESS) {
				for (size_t j = 0; j < IncInstSize; j++)
					reinterpret_cast<uint8_t *>(addr)[j] = 0x90; // nop
				MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
				DBGLOG("patcher", "coverage instruction patched, we're cleared for routing");
			} else {
				SYSLOG("patcher", "coverage instruction patch failed to change protection %d", status);
			}
		}
		totalInstSize += instSize;
		addr += instSize;

		if (limit > 0 && totalInstSize >= limit)
			break;
	}
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

	loadedKextSummaries = reinterpret_cast<OSKextLoadedKextSummaryHeaderAny **>(solveSymbol(KernelID, "_gLoadedKextSummaries"));

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
			routeFunctionLong(s, reinterpret_cast<mach_vm_address_t>(onKextSummariesUpdated), true, true)
		);
	} else {
		routeFunctionLong(s, reinterpret_cast<mach_vm_address_t>(onKextSummariesUpdated));
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

	if (!khandlers.push_back<2>(handler)) {
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
	applyLookupPatch(patch, 0, 0);
}

void KernelPatcher::applyLookupPatch(const LookupPatch *patch, uint8_t *startingAddress, size_t maxSize) {
	if (!patch || (patch->kext && patch->kext->loadIndex == KextInfo::Unloaded)) {
		SYSLOG("patcher", "an invalid lookup patch provided");
		code = Error::MemoryIssue;
		return;
	}

	auto kinfo = kinfos[patch->kext ? patch->kext->loadIndex : KernelID];
	uint8_t *kextAddress;
	size_t kextSize;
	kinfo->getRunningPosition(kextAddress, kextSize);

	// We cannot know kernel size, assume maximum available for now.
	if (patch->kext == nullptr)
		kextSize = UINTPTR_MAX - reinterpret_cast<uintptr_t>(kextAddress);

	uint8_t *currentAddress = kextAddress;
	if (currentAddress < startingAddress)
		currentAddress = startingAddress;

	uint8_t *endingAddress = kextAddress + kextSize;
	if (maxSize > 0 && endingAddress > startingAddress + maxSize)
		endingAddress = startingAddress + maxSize;
	endingAddress -= patch->size;

	size_t changes {0};

	if (MachInfo::setKernelWriting(true, kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("patcher", "lookup patching failed to write to kernel");
		code = Error::MemoryProtection;
		return;
	}

	for (size_t i = 0; currentAddress < endingAddress && (i < patch->count || patch->count == 0); i++) {
		while (currentAddress < endingAddress && memcmp(currentAddress, patch->find, patch->size) != 0)
			currentAddress++;

		if (currentAddress != endingAddress) {
			for (size_t j = 0; j < patch->size; j++)
				currentAddress[j] = patch->replace[j];
			changes++;
		}
	}

	if (MachInfo::setKernelWriting(false, kernelWriteLock) != KERN_SUCCESS) {
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
#ifdef LILU_KEXTPATCH_SUPPORT
	if (getKernelVersion() >= KernelVersion::BigSur && waitingForAlreadyLoadedKexts) {
		auto header = *loadedKextSummaries;
		auto num = header->base.numSummaries;
		if (num > 0) {
			processAlreadyLoadedKexts(header, num);
			waitingForAlreadyLoadedKexts = false;
		}
	}
#endif

	atomic_store_explicit(&activated, true, memory_order_relaxed);
}

mach_vm_address_t KernelPatcher::routeFunction(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper, bool kernelRoute, bool revertible) {
	return routeFunctionInternal(from, to, buildWrapper, kernelRoute, revertible);
}

mach_vm_address_t KernelPatcher::routeFunctionLong(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper, bool kernelRoute, bool revertible) {
	return routeFunctionInternal(from, to, buildWrapper, kernelRoute, revertible, JumpType::Long);
}

mach_vm_address_t KernelPatcher::routeFunctionShort(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper, bool kernelRoute, bool revertible) {
	return routeFunctionInternal(from, to, buildWrapper, kernelRoute, revertible, JumpType::Short);
}

mach_vm_address_t KernelPatcher::routeFunctionInternal(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper, bool kernelRoute, bool revertible, JumpType jumpType, MachInfo *info, mach_vm_address_t *org) {
	mach_vm_address_t diff = (to - (from + SmallJump));
	int32_t newArgument = static_cast<int32_t>(diff);

	DBGLOG("patcher", "from " PRIKADDR " to " PRIKADDR " diff " PRIKADDR " argument %X", CASTKADDR(from), CASTKADDR(to), CASTKADDR(diff), newArgument);

	bool absolute {false};

	if (diff != static_cast<mach_vm_address_t>(newArgument)) {
		DBGLOG("patcher", "will use absolute jumping to " PRIKADDR, CASTKADDR(to));
		absolute = true;
	}

	if (jumpType == JumpType::Long) {
		absolute = true;
	} else if (jumpType == JumpType::Short && absolute) {
		DBGLOG("patcher", "cannot do short jump from " PRIKADDR " to " PRIKADDR, CASTKADDR(from), CASTKADDR(to));
		code = Error::MemoryIssue;
		return EINVAL;
	}

	// If we already routed this function, we simply redirect the original function
	// to the new one, and call the previous function as "original".
	mach_vm_address_t trampoline = readChain(from);
	mach_vm_address_t addressSlot = 0;
	if (trampoline) {
		// Do not perform double revert
		revertible = false;
		// In case we were requested to make unconditional route, still obey, but this
		// is an unsupported configuration, as it breaks previous plugin...
		if (!buildWrapper) trampoline = 0;
	} else if (buildWrapper) {
		if (info && absolute && jumpType == JumpType::Auto) {
			addressSlot = info->getAddressSlot();
			DBGLOG("patcher", "using slotted jumping via " PRIKADDR, CASTKADDR(addressSlot));
		}
		trampoline = createTrampoline(from, absolute ? (addressSlot ? MediumJump : LongJump) : SmallJump);
		if (!trampoline) return EINVAL;
	}

	// Write original function before making route to avoid null pointer dereference.
	if (org) *org = trampoline;

	// In case we already a trampoline to return, do not return it.
	Patch::All *opcode = nullptr, *argument = nullptr, *disp = nullptr;
	// The reason to use slots is to reduce the patch size even for absolute patches.
	// In this case we store 6 bytes of indirect jmp with the target address itself being
	// put in the beginning of the image, right after the Mach-O commands.
	// This solves the problem of having short prologues followed by non-movable
	// commands like mov rax, <vtable_address> commonly found in 11.0 (e.g. ATIController::start).

	if (addressSlot) {
		patch.m.opcode = LongJumpPrefix;
		patch.m.argument = static_cast<uint32_t>(addressSlot - (from + MediumJump));
		patch.sourceIt<decltype(patch.m)>(from);

		opcode = Patch::create<Patch::Variant::U16>(from + offsetof(FunctionPatch, m.opcode), patch.m.opcode);
		argument = Patch::create<Patch::Variant::U32>(from + offsetof(FunctionPatch, m.argument), patch.m.argument);
		disp = Patch::create<Patch::Variant::U64>(addressSlot, to);
	} else if (absolute) {
		patch.l.opcode = LongJumpPrefix;
		patch.l.argument = 0;
		patch.l.disp = to;
		patch.sourceIt<decltype(patch.l)>(from);

		opcode = Patch::create<Patch::Variant::U16>(from + offsetof(FunctionPatch, l.opcode), patch.l.opcode);
		argument = Patch::create<Patch::Variant::U32>(from + offsetof(FunctionPatch, l.argument), patch.l.argument);
		disp = Patch::create<Patch::Variant::U64>(from + offsetof(FunctionPatch, l.disp), patch.l.disp);
	} else {
		patch.s.opcode = SmallJumpPrefix;
		patch.s.argument = newArgument;
		patch.sourceIt<decltype(patch.s)>(from);

		opcode = Patch::create<Patch::Variant::U8>(from + offsetof(FunctionPatch, s.opcode), patch.s.opcode);
		argument = Patch::create<Patch::Variant::U32>(from + offsetof(FunctionPatch, s.argument), patch.s.argument);
	}

	if (!opcode || !argument || (absolute && !disp)) {
		SYSLOG("patcher", "cannot create the necessary patches");
		code = Error::MemoryIssue;
		Patch::deleter(opcode); Patch::deleter(argument);
		if (disp) Patch::deleter(disp);
		return EINVAL;
	}

	if (kernelRoute && MachInfo::setKernelWriting(true, kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("patcher", "cannot change kernel memory protection");
		code = Error::MemoryProtection;
		Patch::deleter(opcode); Patch::deleter(argument);
		if (disp) Patch::deleter(disp);
		return EINVAL;
	}

	if (addressSlot)
		disp->patch();

	// Try to perform atomic swapping to avoid corrupting instructions.
	// Functions will be 16-byte aligned most of the time.
	if (addressSlot || !absolute) {
		if ((from & (sizeof(uint64_t)-1)) == 0) {
			auto p = reinterpret_cast<_Atomic(uint64_t) *>(from);
			atomic_store(p, patch.value64);
		} else {
			opcode->patch();
			argument->patch();
		}
	} else if ((from & (sizeof(unsigned __int128)-1)) == 0) {
		auto p = reinterpret_cast<_Atomic(unsigned __int128) *>(from);
		atomic_store(p, patch.value128);
	} else {
		disp->patch();
		opcode->patch();
		argument->patch();
	}

	if (kernelRoute) {
		MachInfo::setKernelWriting(false, kernelWriteLock);

		if (revertible) {
			auto oidx = kpatches.push_back<4>(opcode);
			auto aidx = kpatches.push_back<4>(argument);
			auto didx = disp ? kpatches.push_back<4>(disp) : 0;

			if (oidx && aidx && (!disp || didx))
				return trampoline;

			SYSLOG("patcher", "failed to store patches for later removal, you are in trouble");
#ifndef __clang_analyzer__
			if (oidx) kpatches.erase(oidx);
			if (aidx) kpatches.erase(aidx);
			if (didx) kpatches.erase(didx);
#endif
		}
	}

	Patch::deleter(opcode); Patch::deleter(argument);
	if (disp) Patch::deleter(disp);
	return trampoline;
}

mach_vm_address_t KernelPatcher::routeBlock(mach_vm_address_t from, const uint8_t *opcodes, size_t opnum, bool buildWrapper, bool kernelRoute) {
	// Simply overwrite the function in the easiest case
	if (!buildWrapper) {
		if (!kernelRoute || MachInfo::setKernelWriting(true, kernelWriteLock) == KERN_SUCCESS) {
			lilu_os_memcpy(reinterpret_cast<void *>(from), opcodes, opnum);
			if (kernelRoute)
				MachInfo::setKernelWriting(false, kernelWriteLock);
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

	// And redirect the original function to it, for blocks do not care either.
	return routeFunctionInternal(from, trampoline) == 0 ? trampoline : EINVAL;
}

bool KernelPatcher::routeMultiple(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start, size_t size, bool kernelRoute, bool force) {
	return routeMultipleInternal(id, requests, num, start, size, kernelRoute, force);
}

bool KernelPatcher::routeMultipleLong(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start, size_t size, bool kernelRoute, bool force) {
	return routeMultipleInternal(id, requests, num, start, size, kernelRoute, force, JumpType::Long);
}

bool KernelPatcher::routeMultipleShort(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start, size_t size, bool kernelRoute, bool force) {
	return routeMultipleInternal(id, requests, num, start, size, kernelRoute, force, JumpType::Short);
}


bool KernelPatcher::routeMultipleInternal(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start, size_t size, bool kernelRoute, bool force, JumpType jump) {
	bool errorsFound = false;
	for (size_t i = 0; i < num; i++) {
		auto &request = requests[i];

		if (!request.symbol)
			continue;

		if (start || size)
			request.from = solveSymbol(id, request.symbol, start, size, true);
		else
			request.from = solveSymbol(id, request.symbol);
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
		if (request.to) eraseCoverageInstPrefix(request.from, 5, LongJump);
		auto wrapper = routeFunctionInternal(request.from, request.to, request.org, kernelRoute, true, jump, kinfos[id], request.org);
		if (request.org) {
			if (wrapper) {
				DBGLOG("patcher", "wrapped %s", request.symbol);
			} else {
				SYSLOG("patcher", "failed to wrap %s, err %d", request.symbol, getError());
				clearError();
				errorsFound = true;
				if (!force) return false;
			}
		} else {
			// In non-wraper mode 0 means success
			if (wrapper == 0) {
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

mach_vm_address_t KernelPatcher::readChain(mach_vm_address_t from) {
	// Note, unaligned access for simplicity
	if (*reinterpret_cast<decltype(&LongJumpPrefix)>(from) == LongJumpPrefix) {
		auto disp = *reinterpret_cast<int32_t *>(from + sizeof(LongJumpPrefix));
		return *reinterpret_cast<mach_vm_address_t *>(from + MediumJump + disp);
	}
	if (*reinterpret_cast<decltype(&SmallJumpPrefix)>(from) == SmallJumpPrefix)
		return from + SmallJump + *reinterpret_cast<int32_t *>(from + sizeof(SmallJumpPrefix));
	return 0;
}

mach_vm_address_t KernelPatcher::createTrampoline(mach_vm_address_t func, size_t min, const uint8_t *opcodes, size_t opnum) {
	// Doing it earlier to workaround stack corruption due to a possible 10.12 bug.
	// Otherwise in rare cases there will be random KPs with corrupted stack data.
	if (MachInfo::setKernelWriting(true, kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("patcher", "failed to set executable permissions");
		code = Error::MemoryProtection;
		return 0;
	}

	// Relative destination offset
	size_t off = Disassembler::quickInstructionSize(func, min);

	if (!off || off > PAGE_SIZE - LongJump) {
		MachInfo::setKernelWriting(false, kernelWriteLock);
		SYSLOG("patcher", "unsupported destination offset %lu", off);
		code = Error::DisasmFailure;
		return 0;
	}

	uint8_t *tempDataPtr = reinterpret_cast<uint8_t *>(tempExecutableMemory) + tempExecutableMemoryOff;

	tempExecutableMemoryOff += off + LongJump + opnum;

	if (tempExecutableMemoryOff >= TempExecutableMemorySize) {
		MachInfo::setKernelWriting(false, kernelWriteLock);
		SYSLOG("patcher", "not enough executable memory requested %ld have %lu", tempExecutableMemoryOff+1, TempExecutableMemorySize);
		code = Error::DisasmFailure;
	} else {
		// Copy the opcodes if any
		if (opnum > 0)
			lilu_os_memcpy(tempDataPtr, opcodes, opnum);

		// Copy the prologue, assuming it is PIC
		lilu_os_memcpy(tempDataPtr + opnum, reinterpret_cast<void *>(func), off);

		MachInfo::setKernelWriting(false, kernelWriteLock);

		// Clear previous error to not rely on the user
		clearError();

		// Add a jump, this one type is honestly irrelevant to us, thus auto.
		routeFunctionInternal(reinterpret_cast<mach_vm_address_t>(tempDataPtr+opnum+off), func+off, false, true, false);

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
		//  - the only place modifying gLoadedKextSummaries is updateLoadedKextSummaries
		//  - updateLoadedKextSummaries is called from load/unload separately
		//  - sKextSummariesLock is not exported or visible
		// As a result no syncing should be necessary but there are guards for future
		// changes and in case of any misunderstanding.

		if (getKernelVersion() >= KernelVersion::Sierra) {
			if (OSIncrementAtomic(&updateSummariesEntryCount) != 0) {
				PANIC("patcher", "onKextSummariesUpdated entered another time");
			}

			that->orgUpdateLoadedKextSummaries();
		}

		DBGLOG("patcher", "invoked at kext loading/unloading");

		if (atomic_load_explicit(&that->activated, memory_order_relaxed) &&
			that->loadedKextSummaries) {
			auto num = (*that->loadedKextSummaries)->base.numSummaries;
			if (num > 0) {
				if (that->waitingForAlreadyLoadedKexts) {
					that->processAlreadyLoadedKexts((*that->loadedKextSummaries), num);
					that->waitingForAlreadyLoadedKexts = false;
				}
				if (that->khandlers.size() > 0) {
					OSKextLoadedKextSummaryBase &last = getKernelVersion() >= KernelVersion::BigSur
						? (*that->loadedKextSummaries)->bigSur.summaries[num-1].base : (*that->loadedKextSummaries)->legacy.summaries[num-1].base;
					DBGLOG("patcher", "last kext is " PRIKADDR " and its name is %.*s", CASTKADDR(last.address), KMOD_MAX_NAME, last.name);
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

void KernelPatcher::processAlreadyLoadedKexts(OSKextLoadedKextSummaryHeaderAny *header, size_t num) {
	DBGLOG("patcher", "processing already loaded kexts by iterating over %lu summaries", num);

	for (size_t i = 0; i < num; i++) {
		OSKextLoadedKextSummaryBase &curr = getKernelVersion() >= KernelVersion::BigSur
			? header->bigSur.summaries[i].base : header->legacy.summaries[i].base;
		for (size_t j = 0; j < khandlers.size(); j++) {
			auto handler = khandlers[j];
			if (handler->loaded && !strncmp(handler->id, curr.name, KMOD_MAX_NAME)) {
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
#endif /* LILU_KEXTPATCH_SUPPORT */
