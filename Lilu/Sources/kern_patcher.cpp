//
//  kern_patcher.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_file.hpp>
#include <PrivateHeaders/kern_patcher.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_nvram.hpp>

#include <mach/mach_types.h>

#include <IOKit/IOService.h>

#ifdef LILU_KEXTPATCH_SUPPORT
static KernelPatcher *that {nullptr};
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
	auto info = MachInfo::create(isKernel ? MachType::Kernel : MachType::Kext, id);
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


	kextKmods = reinterpret_cast<kmod_info_t **>(solveSymbol(KernelID, "_kmod"));

	if (kextKmods) {
		DBGLOG("patcher", "_kmod address %p", kextKmods);
	} else {
		code = Error::NoSymbolFound;
		return;
	}

#if defined(__i386__)
	if (getKernelVersion() >= KernelVersion::SnowLeopard) {
#endif
		KernelPatcher::RouteRequest requests[] = {
			{ "__ZN6OSKext6unloadEv", onOSKextUnload, orgOSKextUnload },
			{ "__ZN6OSKext23saveLoadedKextPanicListEv", onOSKextSaveLoadedKextPanicList, orgOSKextSaveLoadedKextPanicList }
		};

		if (!routeMultiple(KernelID, requests, arrsize(requests))) {
			SYSLOG("patcher", "failed to route kext listener functions");
			return;
		}
#if defined(__i386__)
	} else {
		// 10.5 and older do not have the OSKext class
		KernelPatcher::RouteRequest request("_kmod_create_internal", onKmodCreateInternal, orgKmodCreateInternal);
		if (!routeMultiple(KernelID, &request, 1)) {
			SYSLOG("patcher", "failed to route kext listener function");
			return;
		}
	}
#endif

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

#ifdef LILU_KCINJECT_SUPPORT
OSReturn KernelPatcher::onOSKextLoadKCFileSet(const char *filepath, kc_kind_t type) {
	OSReturn status = kOSReturnError;

	if (that) {
		PANIC_COND(that->curLoadingKCKind != kc_kind::KCKindNone, "patcher", "OSKext::loadKCFileSet entered twice");
		that->curLoadingKCKind = type;
		status = FunctionCast(onOSKextLoadKCFileSet, that->orgOSKextLoadKCFileSet)(filepath, type);
		that->curLoadingKCKind = kc_kind::KCKindNone;
	}

	return status;
}

void * KernelPatcher::onUbcGetobjectFromFilename(const char *filename, struct vnode **vpp, off_t *file_size) {
	void * ret = nullptr;

	if (that) {
		ret = FunctionCast(onUbcGetobjectFromFilename, that->orgUbcGetobjectFromFilename)(filename, vpp, file_size);
		if (that->curLoadingKCKind == kc_kind::KCKindPageable || that->curLoadingKCKind == kc_kind::KCKindAuxiliary) {
			that->kcControls[that->curLoadingKCKind] = ret;
		}

		if (that->curLoadingKCKind == kc_kind::KCKindPageable) {
			vm_size_t oldKcSize = (vm_size_t)*file_size;
			uint8_t *kcBuf = (uint8_t*)that->orgGetAddressFromKextMap(oldKcSize);
			if (kcBuf == nullptr || 
			    that->orgVmMapKcfilesetSegment((vm_map_offset_t*)&kcBuf, (vm_map_offset_t)oldKcSize, ret, 0, (VM_PROT_READ | VM_PROT_WRITE)) != 0) {
				SYSLOG("patcher", "Failed to map kcBuf");
				return ret;
			}
			SYSLOG("patcher", "Mapped kcBuf at %p", kcBuf);

			vm_size_t patchedKCSize = oldKcSize + 128 * 1024 * 1024;
			uint8_t *patchedKCBuf = (uint8_t*)IOMalloc(patchedKCSize);
			memcpy(patchedKCBuf, kcBuf, oldKcSize);
			FunctionCast(onVmMapRemove, that->orgVmMapRemove)(*that->gKextMap, (vm_map_offset_t)kcBuf, (vm_map_offset_t)kcBuf + *file_size, 0);

			MachInfo* kcInfo = MachInfo::create(MachType::KextCollection);
			kcInfo->initFromBuffer(patchedKCBuf, (uint32_t)patchedKCSize, (uint32_t)oldKcSize);
			kcInfo->setKcBaseAddress((uint64_t)kcBuf);
			kcInfo->setKcIndex(1);

			KextInjectionInfo *injectInfo = (KextInjectionInfo*)IOMalloc(sizeof(KextInjectionInfo));
			size_t tmpSize;
			// injectInfo->identifier = "com.apple.driver.AGPM";
			injectInfo->bundlePath = "/System/Library/Extensions/AppleGraphicsPowerManagement.kext";
			injectInfo->infoPlist = (const char*)FileIO::readFileToBuffer("/Users/nyancat/AppleGraphicsPowerManagement.kext/Contents/Info.plist", tmpSize);
			injectInfo->infoPlistSize = (uint32_t)tmpSize;
			injectInfo->executablePath = "Contents/MacOS/AppleGraphicsPowerManagement";
			injectInfo->executable = FileIO::readFileToBuffer("/Users/nyancat/AppleGraphicsPowerManagement.kext/Contents/MacOS/AppleGraphicsPowerManagement", tmpSize);
			injectInfo->executableSize = (uint32_t)tmpSize;

			// kcInfo->injectKextIntoKC(injectInfo);
			Buffer::deleter((void*)injectInfo->infoPlist);
			Buffer::deleter((void*)injectInfo->executable);
			IOFree(injectInfo, sizeof(KextInjectionInfo));

			NVStorage *nvram = new NVStorage();
			nvram->init();
			OSData *prelinkedSymbolsPtr = nvram->read("E09B9297-7928-4440-9AAB-D1F8536FBF0A:lilu-prelinked-symbols", NVStorage::Options::OptRaw);
			uint64_t prelinkedSymbolsAddr = *(uint64_t*)prelinkedSymbolsPtr->getBytesNoCopy();
			DBGLOG("patcher", "lilu-prelinked-symbols = %llX", prelinkedSymbolsAddr);

			IOMemoryDescriptor *prelinkedSymbolsDesc = IOGeneralMemoryDescriptor::withPhysicalAddress(prelinkedSymbolsAddr, 4096, kIODirectionIn);
			prelinkedSymbolsDesc->prepare();
			char *prelinkedSymbols = (char*)IOMalloc(4096);
			prelinkedSymbolsDesc->readBytes(0, prelinkedSymbols, 4096);
			prelinkedSymbolsDesc->complete();
			DBGLOG("patcher", "lilu-prelinked-symbols content = %s", prelinkedSymbols);

			prelinkedSymbolsPtr->free();

			kcInfo->overwritePrelinkInfo();
			that->kcMachInfos[kc_kind::KCKindPageable] = kcInfo;
			*file_size = patchedKCSize;
			IOSleep(5000);
		}
	}

	return ret;
}

kern_return_t KernelPatcher::onVmMapEnterMemObjectControl(
	vm_map_t                target_map,
	vm_map_offset_t         *address,
	vm_map_size_t           initial_size,
	vm_map_offset_t         mask,
	int                     flags,
	vm_map_kernel_flags_t   vmk_flags,
	vm_tag_t                tag,
	memory_object_control_t control,
	vm_object_offset_t      offset,
	boolean_t               copy,
	vm_prot_t               cur_protection,
	vm_prot_t               max_protection,
	vm_inherit_t            inheritance)
{
	kern_return_t ret = -1;

	if (that) {
		const char * kcName = nullptr;
		kc_kind kcType = kc_kind::KCKindNone;
		if (target_map == *that->gKextMap) {
			kcName = "Unknown";
			kcType = kc_kind::KCKindUnknown;
			if (control == that->kcControls[kc_kind::KCKindPageable]) {
				kcName = "Sys";
				kcType = kc_kind::KCKindPageable;
			} else if (control == that->kcControls[kc_kind::KCKindAuxiliary]) {
				kcName = "Aux";
				kcType = kc_kind::KCKindAuxiliary;
			}
		}

		bool doOverride = kcType != kc_kind::KCKindNone && that->kcMachInfos[kcType] != nullptr;
		vm_object_offset_t realOffset = offset;
		if (doOverride) {
			offset = 0;
			SYSLOG("patcher", "onVmMapEnterMemObjectControl: Mapping %sKC range %llX ~ %llX", kcName, realOffset, realOffset + initial_size);
		}
		ret = FunctionCast(onVmMapEnterMemObjectControl, that->orgVmMapEnterMemObjectControl)
			  (target_map, address, initial_size, mask, flags, vmk_flags, tag,
			   control, offset, copy, cur_protection, max_protection, inheritance);
		if (doOverride) {
			if (ret) SYSLOG("patcher", "onVmMapEnterMemObjectControl: ret=%d with *address set to %p", ret, *address);
			uint8_t *patchedKC = that->kcMachInfos[kcType]->getFileBuf();
			memcpy((void*)*address, patchedKC + realOffset, (size_t)initial_size);
		}
	}

	return ret;
}

kern_return_t KernelPatcher::onVmMapRemove(
	vm_map_t        map,
	vm_map_offset_t start,
	vm_map_offset_t end,
	boolean_t       flags)
{
	kern_return_t ret = -1;

	if (that) {
		if (map == *that->gKextMap) {
			SYSLOG("patcher", "onVmMapRemove: Unmapping range %llX ~ %llX from g_kext_map", start, end);
		}
		ret = FunctionCast(onVmMapRemove, that->orgVmMapRemove)(map, start, end, flags);
	}

	return ret;
}

void KernelPatcher::setupKCListening() {
	gKextMap = reinterpret_cast<vm_map_t*>(solveSymbol(KernelPatcher::KernelID, "_g_kext_map"));
	if (getError() != Error::NoError) {
		SYSLOG("patcher", "failed to resolve _g_kext_map symbol");
		clearError();
		return;
	}

	orgVmMapKcfilesetSegment = reinterpret_cast<t_vmMapKcfilesetSegment>(solveSymbol(KernelPatcher::KernelID, "_vm_map_kcfileset_segment"));
	if (getError() != Error::NoError) {
		DBGLOG("patcher", "failed to resolve _vm_map_kcfileset_segment");
		clearError();
		return;
	}

	orgGetAddressFromKextMap = reinterpret_cast<t_getAddressFromKextMap>(solveSymbol(KernelPatcher::KernelID, "_get_address_from_kext_map"));
	if (getError() != Error::NoError) {
		DBGLOG("patcher", "failed to resolve _get_address_from_kext_map");
		clearError();
		return;
	}

	KernelPatcher::RouteRequest requests[] = {
		{ "__ZN6OSKext13loadKCFileSetEPKc7kc_kind", onOSKextLoadKCFileSet, orgOSKextLoadKCFileSet },
		{ "_ubc_getobject_from_filename", onUbcGetobjectFromFilename, orgUbcGetobjectFromFilename },
		{ "_vm_map_enter_mem_object_control", onVmMapEnterMemObjectControl, orgVmMapEnterMemObjectControl },
		{ "_vm_map_remove", onVmMapRemove, orgVmMapRemove },
	};

	if (!routeMultiple(KernelID, requests, arrsize(requests))) {
		SYSLOG("patcher", "failed to route KC listener functions");
		return;
	}
}
#endif /* LILU_KCINJECT_SUPPORT */

void KernelPatcher::freeFileBufferResources() {
	if (kinfos.size() > KernelID)
		kinfos[KernelID]->freeFileBufferResources();
}

void KernelPatcher::activate() {
#ifdef LILU_KEXTPATCH_SUPPORT
	if (getKernelVersion() >= KernelVersion::BigSur && waitingForAlreadyLoadedKexts) {
		processAlreadyLoadedKexts();
		waitingForAlreadyLoadedKexts = false;
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
	JumpType prevJump;
	mach_vm_address_t trampoline = readChain(from, prevJump);
	mach_vm_address_t addressSlot = 0;
	if (trampoline) {
		// Do not perform double revert
		revertible = false;
		// In case we were requested to make unconditional route, still obey, but this
		// is an unsupported configuration, as it breaks previous plugin...
		if (!buildWrapper) trampoline = 0;
		// Forbid routing multiple times with anything but long and medium functions.
		// You must update all the plugins sharing function routes with routeMultipleLong call.
		if (prevJump == JumpType::Short)
			PANIC("patcher", "previous plugin had short jump type on a multiroute function, this is not allowed");
		if (jumpType == JumpType::Short)
			PANIC("patcher", "current plugin has short jump type on a multiroute function, this is not allowed");

		// Make sure to use just 6 bytes for medium routes instead of 14.
		if (prevJump == JumpType::Medium) {
			// If this happens, we can corrupt memory. Force everyone use new APIs.
			if (!info)
				PANIC("patcher", "trying to use long jump on top of slotted jump, please use routeMultipleLong");
			addressSlot = info->getAddressSlot();
			DBGLOG("patcher", "using slotted jumping for previous via " PRIKADDR, CASTKADDR(addressSlot));
			// If this happens, then we should allow slotted jumping only for Auto type.
			if (addressSlot == 0)
				PANIC("patcher", "not enough memory for slotted jumping, this is a bug in Lilu");
		}

	} else if (buildWrapper) {
		if (info && absolute && (jumpType == JumpType::Auto || jumpType == JumpType::Long)) {
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
#if defined(__i386__)
		patch.l.argument = static_cast<uint32_t>(from + MediumJump);
		patch.l.disp = static_cast<uint32_t>(to);
#elif defined(__x86_64__)
		patch.l.argument = 0;
		patch.l.disp = to;
#else
#error Unsupported arch
#endif
		patch.sourceIt<decltype(patch.l)>(from);

		opcode = Patch::create<Patch::Variant::U16>(from + offsetof(FunctionPatch, l.opcode), patch.l.opcode);
		argument = Patch::create<Patch::Variant::U32>(from + offsetof(FunctionPatch, l.argument), patch.l.argument);
#if defined(__i386__)
		disp = Patch::create<Patch::Variant::U32>(from + offsetof(FunctionPatch, l.disp), patch.l.disp);
#elif defined(__x86_64__)
		disp = Patch::create<Patch::Variant::U64>(from + offsetof(FunctionPatch, l.disp), patch.l.disp);
#else
#error Unsupported arch
#endif
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
#if defined(__x86_64__)
	} else if ((from & (sizeof(unsigned __int128)-1)) == 0) {
		auto p = reinterpret_cast<_Atomic(unsigned __int128) *>(from);
		atomic_store(p, patch.value128);
#endif
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

bool KernelPatcher::findPattern(const void *pattern, const void *patternMask, size_t patternSize, const void *data, size_t dataSize, size_t *dataOffset) {
	if (patternSize == 0 || dataSize < patternSize)
		return false;

	size_t currOffset = *dataOffset;
	size_t lastOffset = dataSize - patternSize;

	const uint8_t *d = (const uint8_t *) data;
	const uint8_t *ptn = (const uint8_t *) pattern;
	const uint8_t *ptnMask = (const uint8_t *) patternMask;

	if (patternMask == nullptr) {
		while (currOffset <= lastOffset) {
			size_t i;
			for (i = 0; i < patternSize; i++) {
				if (d[currOffset + i] != ptn[i])
					break;
			}

			if (i == patternSize) {
				*dataOffset = currOffset;
				return true;
			}

			currOffset++;
		}
	} else {
		while (currOffset <= lastOffset) {
			size_t i;
			for (i = 0; i < patternSize; i++) {
				if ((d[currOffset + i] & ptnMask[i]) != ptn[i])
					break;
			}

			if (i == patternSize) {
				*dataOffset = currOffset;
				return true;
			}

			currOffset++;
		}
	}

	return false;
}

bool KernelPatcher::findAndReplaceWithMask(void *data, size_t dataSize, const void *find, size_t findSize, const void *findMask, size_t findMaskSize, const void *replace, size_t replaceSize, const void *replaceMask, size_t replaceMaskSize, size_t count, size_t skip) {
	if (dataSize < findSize) return false;
	
	uint8_t *d = (uint8_t *) data;
	const uint8_t *repl = (const uint8_t *) replace;
	const uint8_t *replMsk = (const uint8_t *) replaceMask;

	size_t replCount = 0;
	size_t dataOffset = 0;

	while (true) {
		bool found = findPattern(find, findMask, findSize, data, dataSize, &dataOffset);
		if (!found) break;

		// dataOffset + findSize - 1 is guaranteed to be a valid offset here. As
		// dataSize can at most be SIZE_T_MAX, the maximum valid offset is
		// SIZE_T_MAX - 1. In consequence, dataOffset + findSize cannot wrap around.

		// skip this finding if requested
		if (skip > 0) {
			skip--;
			dataOffset += findSize;
			continue;
		}

		if (UNLIKELY(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS)) {
			SYSLOG("patcher", "failed to obtain write permissions for f/r");
			return false;
		}

		// perform replacement
		if (replaceMask == nullptr) {
			lilu_os_memcpy(&d[dataOffset], replace, replaceSize);
		} else {
			for (size_t i = 0; i < findSize; i++)
				d[dataOffset + i] = (d[dataOffset + i] & ~replMsk[i]) | (repl[i] & replMsk[i]);
		}

		if (UNLIKELY(MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock) != KERN_SUCCESS)) {
			SYSLOG("patcher", "failed to restore write permissions for f/r");
		}

		replCount++;
		dataOffset += replaceSize;

		// check replace count if requested
		if (count > 0) {
			count--;
			if (count == 0)
				break;
		}
	}

	return replCount > 0;
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

mach_vm_address_t KernelPatcher::readChain(mach_vm_address_t from, JumpType &jumpType) {
	// Note, unaligned access for simplicity
	if (*reinterpret_cast<decltype(&LongJumpPrefix)>(from) == LongJumpPrefix) {
#if defined(__i386__)
		auto disp = *reinterpret_cast<uint32_t *>(from + sizeof(LongJumpPrefix));
		jumpType = JumpType::Long;
		return *reinterpret_cast<mach_vm_address_t *>(disp);
#elif defined(__x86_64__)
		auto disp = *reinterpret_cast<int32_t *>(from + sizeof(LongJumpPrefix));
		jumpType = disp != 0 ? JumpType::Medium : JumpType::Long;
		return *reinterpret_cast<mach_vm_address_t *>(from + MediumJump + disp);
#else
#error Unsupported arch
#endif
	}
	if (*reinterpret_cast<decltype(&SmallJumpPrefix)>(from) == SmallJumpPrefix) {
		jumpType = JumpType::Short;
		return from + SmallJump + *reinterpret_cast<int32_t *>(from + sizeof(SmallJumpPrefix));
	}
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
OSReturn KernelPatcher::onOSKextUnload(void *thisKext) {
	OSReturn status = kOSReturnError;

	if (that) {
		// Prevent handling kexts if we are unloading, which may change the head of the kmod list.
		that->isKextUnloading = true;
		status = FunctionCast(onOSKextUnload, that->orgOSKextUnload)(thisKext);
		that->isKextUnloading = false;
	}

	return status;
}

void KernelPatcher::onOSKextSaveLoadedKextPanicList() {
	if (!that || !atomic_load_explicit(&that->activated, memory_order_relaxed)) {
		return;
	}
	
	FunctionCast(onOSKextSaveLoadedKextPanicList, that->orgOSKextSaveLoadedKextPanicList)();
	
	// Flag set during OSKext::unload() to prevent triggering during an unload.
	if (that->isKextUnloading) {
		return;
	}
	
	DBGLOG("patcher", "invoked at kext loading");

	if (that->waitingForAlreadyLoadedKexts) {
		that->processAlreadyLoadedKexts();
		that->waitingForAlreadyLoadedKexts = false;
	} else {
		kmod_info_t *kmod = *that->kextKmods;
		if (kmod) {
			DBGLOG("patcher", "newly loaded kext is " PRIKADDR " and its name is %.*s (start func is " PRIKADDR ")",
						 CASTKADDR((uint64_t)kmod->address), KMOD_MAX_NAME, kmod->name, CASTKADDR((uint64_t)kmod->start));
			that->processKext(kmod, false);
		} else {
			SYSLOG("patcher", "no kext is currently loaded, this should not happen");
		}
	}
}

#if defined(__i386__)

kern_return_t KernelPatcher::onKmodCreateInternal(kmod_info_t *kmod, kmod_t *id) {
	if (!that)
		return KERN_INVALID_ARGUMENT;
	
	kern_return_t result = FunctionCast(onKmodCreateInternal, that->orgKmodCreateInternal)(kmod, id);
	if (result == KERN_SUCCESS) {
		DBGLOG("patcher", "invoked at kext loading");
		
		if (that->waitingForAlreadyLoadedKexts) {
			that->processAlreadyLoadedKexts();
			that->waitingForAlreadyLoadedKexts = false;
		} else {
			DBGLOG("patcher", "newly loaded kext is " PRIKADDR " and its name is %.*s (start func is " PRIKADDR ")",
						 CASTKADDR((uint64_t)kmod->address), KMOD_MAX_NAME, kmod->name, CASTKADDR((uint64_t)kmod->start));
			that->processKext(kmod, false);
		}
	}
	
	return result;
}

#endif

void KernelPatcher::processKext(kmod_info_t *kmod, bool loaded) {
	uint64_t kmodAddr = (uint64_t)kmod->address;

	for (size_t i = 0; i < khandlers.size(); i++) {
		auto handler = khandlers[i];
		if (loaded && !handler->loaded)
			continue;

		if (!strncmp(handler->id, kmod->name, KMOD_MAX_NAME)) {
			DBGLOG("patcher", "caught the right kext %s at " PRIKADDR ", invoking handler", kmod->name, CASTKADDR(kmodAddr));
			if (!kinfos[handler->index]->isCurrentBinary(kmodAddr)) {
				SYSLOG("patcher", "uuid mismatch for %s at " PRIKADDR ", ignoring", kmod->name, CASTKADDR(kmodAddr));
				continue;
			}
			handler->address = kmodAddr;
			handler->size = kmod->size;
			handler->handler(handler);

			// Remove the item
			if (!khandlers[i]->reloadable)
				khandlers.erase(i);
			break;
		}
	}
}

void KernelPatcher::processAlreadyLoadedKexts() {
	DBGLOG("patcher", "processing already loaded kexts by iterating over all kmods");

	for (kmod_info_t *kmod = *kextKmods; kmod; kmod = kmod->next) {
		processKext(kmod, true);
	}
}
#endif /* LILU_KEXTPATCH_SUPPORT */
