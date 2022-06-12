//
//  kern_user.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_devinfo.hpp>
#include <PrivateHeaders/kern_config.hpp>

#include <mach/vm_map.h>
#include <mach-o/fat.h>
#include <kern/task.h>
#include <kern/cs_blobs.h>
#include <sys/vm.h>

static UserPatcher *that {nullptr};

struct procref {
	LIST_ENTRY(proc) p_list; /* List of all processes. */
	task_t task;             /* corresponding task (static)*/
};

kern_return_t UserPatcher::vmProtect(vm_map_t map, vm_offset_t start, vm_size_t size, boolean_t set_maximum, vm_prot_t new_protection) {
	// On 10.14 XNU attempted to fix broken W^X and introduced several changes:
	// 1. vm_protect (vm_map_protect) got a call to cs_process_enforcement (formerly cs_enforcement), which aborts
	//    with a KERN_PROTECTION_FAILURE abort on failure. So far global codesign enforcement is not enabled,
	//    and it is enough to remove CS_ENFORCEMENT from each process specifically. A thing to consider in future
	//    macOS versions. Watch out for sysctl vm.cs_process_enforcemen.
	// 2. More processes get CS_KILL in addition to CS_ENFORCEMENT, which does not let us easily lift CS_ENFORCEMENT
	//    during the vm_protect call, we also should remove CS_KILL from the process we patch. This slightly lowers
	//    the security, but only for the patched process, and in no worse way than 10.13 by default. Watch out for
	//    vm.cs_force_kill in the future.
	auto currproc = reinterpret_cast<procref *>(current_proc());
	if ((new_protection & (VM_PROT_EXECUTE|VM_PROT_WRITE)) == (VM_PROT_EXECUTE|VM_PROT_WRITE) &&
		getKernelVersion() >= KernelVersion::Mojave && currproc != nullptr) {
		DBGLOG("user", "found request for W^X switch-off %d", new_protection);

		// struct proc layout usually changes with time, so we will calculate the offset based on partial layout:
		// struct        pgrp *p_pgrp   Pointer to process group. (LL)
		// uint32_t      p_csflags      flags for codesign (PL)
		// uint32_t      p_pcaction     action  for process control on starvation
		// uint8_t       p_uuid[16]     from LC_UUID load command
		// cpu_type_t    p_cputype
		// cpu_subtype_t p_cpusubtype
		if (csFlagsOffset == 0) {
			for (size_t off = 0x200; off < 0x400; off += sizeof (uint32_t)) {
				auto csOff = off - sizeof(uint8_t[16]) - sizeof(uint32_t)*2;
				auto cpu = getMember<uint32_t>(currproc, off);
				auto subcpu = getMember<uint32_t>(currproc, off + sizeof (uint32_t)) & ~CPU_SUBTYPE_MASK;
				if ((cpu == CPU_TYPE_X86_64 || cpu == CPU_TYPE_I386) &&
					(subcpu == CPU_SUBTYPE_X86_64_ALL || subcpu == CPU_SUBTYPE_X86_64_H) &&
					!(getMember<uint32_t>(currproc, csOff) & CS_KILLED)) {
					csFlagsOffset = csOff;
					break;
				}
			}

			// Force xnu-4903.221.2 offset by default, 10.14.1 b5
			if (csFlagsOffset != 0) {
				DBGLOG("user", "found p_csflags offset %X", (uint32_t)csFlagsOffset);
			} else {
				SYSLOG("user", "falling back to hardcoded p_csflags offset");
				csFlagsOffset = 0x308;
			}
		}

		uint32_t &flags = getMember<uint32_t>(currproc, csFlagsOffset);
		if (flags & CS_ENFORCEMENT) {
			DBGLOG("user", "W^X is enforced %X, disabling", flags);
			flags &= ~(CS_KILL|CS_HARD|CS_ENFORCEMENT);
			// Changing CS_HARD, CS_DEBUGGED, and vm_map switch protection is not required, yet may avoid issues
			// in the future.
			flags |= CS_DEBUGGED;
			if (that->orgVmMapSwitchProtect)
				that->orgVmMapSwitchProtect(that->orgGetTaskMap(currproc->task), false);
			auto r = vm_protect(map, start, size, set_maximum, new_protection);
			SYSLOG_COND(r != KERN_SUCCESS, "user", "W^X removal failed with %d", r);
			// Initially thought that we could return CS_ENFORCEMENT, yet this causes certain binaries to crash,
			// like WindowServer patched by -cdfon.
			return r;
		}
	}

	// Forward to the original proc routine
	return vm_protect(map, start, size, set_maximum, new_protection);
}

int UserPatcher::execListener(kauth_cred_t, void *idata, kauth_action_t action, uintptr_t, uintptr_t arg1, uintptr_t, uintptr_t) {
	// Make sure this is ours
	if (atomic_load_explicit(&that->activated, memory_order_relaxed) &&
		idata == &that->cookie && action == KAUTH_FILEOP_EXEC && arg1) {
		const char *path = reinterpret_cast<const char *>(arg1);
		that->onPath(path, static_cast<uint32_t>(strlen(path)));
	}

	return 0;
}

bool UserPatcher::init(KernelPatcher &kernelPatcher, bool preferSlowMode) {
	if (ADDPR(config).isUserDisabled) {
		SYSLOG_COND(ADDPR(debugEnabled), "user", "disabling user patcher on request!");
		return true;
	}

	that = this;
	patchDyldSharedCache = !preferSlowMode;
	patcher = &kernelPatcher;

	pending.init();

	listener = kauth_listen_scope(KAUTH_SCOPE_FILEOP, execListener, &cookie);

	if (!listener) {
		SYSLOG("user", "failed to register a listener");
		return false;
	}

	return true;
}

bool UserPatcher::registerPatches(ProcInfo **procs, size_t procNum, BinaryModInfo **mods, size_t modNum, t_BinaryLoaded callback, void *user) {
	// Silently return if disabled
	if (ADDPR(config).isUserDisabled)
		return true;

	procInfo = procs;
	procInfoSize = procNum;
	binaryMod = mods;
	binaryModSize = modNum;
	userCallback.first = callback;
	userCallback.second = user;

	if (procNum ) {
		currentMinProcLength = procs[0]->len;
		for (size_t i = 1; i < procNum; i++) {
			if (procs[i]->len < currentMinProcLength)
				currentMinProcLength = procs[i]->len;
		}
	}

	return loadFilesForPatching() && (!patchDyldSharedCache || loadDyldSharedCacheMapping()) && loadLookups() && hookMemoryAccess();
}

void UserPatcher::deinit() {
	// Silently return if disabled
	if (ADDPR(config).isUserDisabled)
		return;

	if (listener) {
		kauth_unlisten_scope(listener);
		listener = nullptr;
	}

	pending.deinit();
	lookupStorage.deinit();
	for (size_t i = 0; i < Lookup::matchNum; i++)
		lookup.c[i].deinit();
}

void UserPatcher::performPagePatch(const void *data_ptr, size_t data_size) {
	for (size_t data_off = 0; data_off < data_size; data_off += PAGE_SIZE) {
		size_t sz = that->lookupStorage.size();
		size_t maybe = 0;
		auto ptr = static_cast<const uint8_t *>(data_ptr) + data_off;

		if (sz > 0) {
			for (size_t i = 0; i < Lookup::matchNum && maybe != sz; i++) {
				uint64_t value = *reinterpret_cast<const uint64_t *>(ptr + lookup.offs[i]);

				if (i == 0) {
					for (maybe = 0; maybe < sz; maybe++) {
						if (lookup.c[i][maybe] == value) {
							// We have a possible match
							DBGLOG("user", "found a possible match for %lu of %llX", i, value);
							break;
						}
					}
				} else {
					if (lookup.c[i][maybe] != value) {
						// We failed
						DBGLOG("user", "failure not matching %lu of %llX to expected %llX", i, value, lookup.c[i][maybe]);
						maybe = sz;
					} else {
						DBGLOG("user", "found a possible match for %lu of %llX", i, value);
					}
				}

			}

			if (maybe < sz) {
				auto &storage = that->lookupStorage[maybe];

				// That's a patch
				if (!memcmp(storage->page->p, ptr, PAGE_SIZE)) {
					for (size_t r = 0, rsz = storage->refs.size(); r < rsz; r++) {
						// Apply the patches
						auto &ref = storage->refs[r];
						auto &rpatch = storage->mod->patches[ref->i];
						sz = ref->pageOffs.size();

						// Skip patches that are meant to apply only to select processes.
						if (rpatch.flags & LocalOnly) {
							continue;
						}

						DBGLOG("user", "found what we are looking for %X %X %X %X %X %X %X %X", rpatch.find[0],
								rpatch.size > 1 ? rpatch.find[1] : 0xff,
								rpatch.size > 2 ? rpatch.find[2] : 0xff,
								rpatch.size > 3 ? rpatch.find[3] : 0xff,
								rpatch.size > 4 ? rpatch.find[4] : 0xff,
								rpatch.size > 5 ? rpatch.find[5] : 0xff,
								rpatch.size > 6 ? rpatch.find[6] : 0xff,
								rpatch.size > 7 ? rpatch.find[7] : 0xff
						);

						if (sz > 0 && MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) == KERN_SUCCESS) {
							DBGLOG("user", "obtained write permssions");

							for (size_t i = 0; i < sz; i++) {
								uint8_t *patch = const_cast<uint8_t *>(ptr + ref->pageOffs[i]);

								switch(rpatch.size) {
									case sizeof(uint8_t):
										*const_cast<uint8_t *>(patch) = *rpatch.replace;
										break;
									case sizeof(uint16_t):
										*reinterpret_cast<uint16_t *>(patch) = *reinterpret_cast<const uint16_t *>(rpatch.replace);
										break;
									case sizeof(uint32_t):
										*reinterpret_cast<uint32_t *>(patch) = *reinterpret_cast<const uint32_t *>(rpatch.replace);
										break;
									case sizeof(uint64_t):
										*reinterpret_cast<uint64_t *>(patch) = *reinterpret_cast<const uint64_t *>(rpatch.replace);
										break;
									default:
										lilu_os_memcpy(patch, rpatch.replace, rpatch.size);
								}
							}

							if (MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock) == KERN_SUCCESS) {
								DBGLOG("user", "restored write permssions");
							}
						} else {
							SYSLOG("user", "failed to obtain write permssions for %lu", sz);
						}
					}
				} else {
					DBGLOG("user", "failed to match a complete page with %lu", maybe);
				}
			}
		}
	}
}

boolean_t UserPatcher::codeSignValidatePageWrapper(void *blobs, memory_object_t pager, memory_object_offset_t page_offset, const void *data, unsigned *tainted) {
	boolean_t res = FunctionCast(codeSignValidatePageWrapper, that->orgCodeSignValidatePageWrapper)(blobs, pager, page_offset, data, tainted);
	if (res) that->performPagePatch(data, PAGE_SIZE);
	return res;
}

boolean_t UserPatcher::codeSignValidateRangeWrapper(void *blobs, memory_object_t pager, memory_object_offset_t range_offset, const void *data, memory_object_size_t data_size, unsigned *tainted) {
	boolean_t res = FunctionCast(codeSignValidateRangeWrapper, that->orgCodeSignValidateRangeWrapper)(blobs, pager, range_offset, data, data_size, tainted);

	if (res)
		that->performPagePatch(data, (size_t)data_size);

	return res;
}

void UserPatcher::onPath(const char *path, uint32_t len) {
	if (len >= currentMinProcLength) {
		for (uint32_t i = 0; i < procInfoSize; i++) {
			auto p = procInfo[i];
			if (len >= p->len) {
				auto match = p->flags & ProcInfo::MatchMask;
				if ((match == ProcInfo::MatchExact && len == p->len && !strncmp(p->path, path, len)) ||
					(match == ProcInfo::MatchPrefix && !strncmp(p->path, path, p->len)) ||
					(match == ProcInfo::MatchSuffix && !strncmp(p->path, path + (len - p->len), p->len+1)) ||
					(match == ProcInfo::MatchAny && strstr(path, p->path))) {
					DBGLOG("user", "caught %s performing injection", path);
					if (orgTaskSetMainThreadQos) {
						DBGLOG("user", "requesting delayed patch " PRIKADDR, CASTKADDR(current_thread()));

						auto previous = pending.get();
						if (previous) {
							// This is possible when execution does not happen, and thus we do not remove the patch.
							DBGLOG("user", "found dangling user patch request");
							PANIC_COND(!pending.erase(), "user", "failed to remove dangling user patch");
							delete *previous;
						}

						auto pend = new PendingUser;
						if (pend != nullptr) {
							lilu_strlcpy(pend->path, path, MAXPATHLEN);
							pend->pathLen = len;
							// This should not happen after we added task_set_main_thread_qos hook, which gets always called
							// unlike proc_exec_switch_task. Increasing pending count to 32 should accomodate for most CPUs.
							// Still do not cause a kernel panic but rather just report this.
							if (!pending.set(pend)) {
								SYSLOG("user", "failed to set user patch request, report this!!!");
								delete pend;
							}
						} else {
							SYSLOG("user", "failed to allocate pending user callback");
						}

					} else {
						patchBinary(orgCurrentMap(), path, len);
					}

					return;
				}
			}
		}
	}
}

void UserPatcher::patchBinary(vm_map_t map, const char *path, uint32_t len) {
	if (patchDyldSharedCache && sharedCacheSlideStored) {
		patchSharedCache(map, storedSharedCacheSlide, CPU_TYPE_X86_64);
	} else {
		if (patchDyldSharedCache) SYSLOG("user", "no slide present, initialisation failed, fallback to restrict");
		injectRestrict(map);
	}
	userCallback.first(userCallback.second, *this, map, path, len);
}

bool UserPatcher::getTaskHeader(vm_map_t taskPort, mach_header_64 &header) {
	auto baseAddr = orgGetMapMin(taskPort);
	DBGLOG("user", "getTaskHeader map min is " PRIKADDR, CASTKADDR(baseAddr));

	kern_return_t err = orgVmMapReadUser(taskPort, baseAddr, &header, sizeof(mach_header_64));
	if (err == KERN_SUCCESS)
		return true;

	SYSLOG("user", "failed to read image header %d", err);
	return false;
}

bool UserPatcher::injectRestrict(vm_map_t taskPort) {
	// Get task's mach-o header and determine its cpu type
	auto baseAddr = orgGetMapMin(taskPort);

	DBGLOG("user", "injectRestrict map min is " PRIKADDR, CASTKADDR(baseAddr));

	auto &tmpHeader = *reinterpret_cast<mach_header_64 *>(tmpBufferData);
	kern_return_t err = orgVmMapReadUser(taskPort, baseAddr, &tmpHeader, sizeof(mach_header_64));

	if (err == KERN_SUCCESS){
		if (tmpHeader.magic == MH_MAGIC_64 || tmpHeader.magic == MH_MAGIC) {
			size_t hdrSize = tmpHeader.magic == MH_MAGIC ? sizeof(mach_header) : sizeof(mach_header_64);
			size_t restrSize = tmpHeader.magic == MH_MAGIC ? sizeof(restrictSegment32) : sizeof(restrictSegment64);

			struct {
				vm_map_offset_t off;
				vm_prot_t val;
			} prots[3] {};
			size_t orgBound = hdrSize + tmpHeader.sizeofcmds;
			size_t newBound = orgBound + restrSize;

			prots[0].off = baseAddr;
			prots[0].val = getPageProtection(taskPort, prots[0].off);

			if (orgBound + restrSize > PAGE_SIZE){
				prots[1].off = baseAddr + orgBound - orgBound % PAGE_SIZE;
				prots[1].val = getPageProtection(taskPort, prots[1].off);
				if (baseAddr + newBound  > prots[1].off + PAGE_SIZE){
					prots[2].off = prots[1].off + PAGE_SIZE;
					prots[2].val = getPageProtection(taskPort, prots[2].off);
				}
			}

			//TODO: Should implement a check whether the space is available to ensure that we do not break processes.

			// Note, that we don't restore memory protection if we fail somewhere (no need to push something non-critical)
			// Enable writing for the calculated regions
			for (size_t i = 0; i < 3; i++) {
				if (prots[i].off && !(prots[i].val & VM_PROT_WRITE)) {
					auto res = vmProtect(taskPort, (vm_offset_t)prots[i].off, PAGE_SIZE, FALSE, prots[i].val|VM_PROT_WRITE);
					if (res != KERN_SUCCESS) {
						SYSLOG("user", "failed to change memory protection (%lu, %d)", i, res);
						return true;
					}
				}
			}

			vm_map_address_t ncmdsAddr = baseAddr + offsetof(mach_header, ncmds);
			vm_map_address_t newCmdAddr = baseAddr + hdrSize + tmpHeader.sizeofcmds;

			uint64_t orgCombVal = (static_cast<uint64_t>(tmpHeader.sizeofcmds) << 32) | tmpHeader.ncmds;
			uint64_t newCombVal = (static_cast<uint64_t>(tmpHeader.sizeofcmds + restrSize) << 32) | (tmpHeader.ncmds + 1);

			// Write new number and size of commands
			auto res = orgVmMapWriteUser(taskPort, &newCombVal, ncmdsAddr, sizeof(uint64_t));
			if (res != KERN_SUCCESS) {
				SYSLOG("user", "failed to change mach header (%d)", res);
				return true;
			}

			// Write the load command
			auto restrSegment = tmpHeader.magic == MH_MAGIC ? static_cast<void *>(&restrictSegment32) : static_cast<void *>(&restrictSegment64);
			res = orgVmMapWriteUser(taskPort, restrSegment, newCmdAddr, restrSize);
			if (res != KERN_SUCCESS) {
				SYSLOG("user", "failed to add dylib load command (%d), reverting...", res);
				res = orgVmMapWriteUser(taskPort, &orgCombVal, ncmdsAddr, sizeof(uint64_t));
				if (res != KERN_SUCCESS) {
					SYSLOG("user", "failed to restore mach header (%d), this process will crash...", res);
				}
				return true;
			}

			// Restore protection flags
			for (size_t i = 0; i < 3; i++) {
				if (prots[i].off && !(prots[i].val & VM_PROT_WRITE)) {
					res = vmProtect(taskPort, (vm_offset_t)prots[i].off, PAGE_SIZE, FALSE, prots[i].val);
					if (res != KERN_SUCCESS) {
						SYSLOG("user", "failed to restore memory protection (%lu, %d)", i, res);
						return true;
					}
				}
			}

		} else {
			SYSLOG("user", "unknown header magic %X", tmpHeader.magic);
		}
	} else {
		SYSLOG("user", "could not read target mach-o header (error %d)", err);
		return false;
	}

	return true;
}

vm_address_t UserPatcher::injectSegment(vm_map_t taskPort, vm_address_t addr, uint8_t *payload, size_t size, vm_prot_t prot) {
	auto ret = vm_allocate(taskPort, &addr, size, VM_FLAGS_FIXED);
	if (ret != KERN_SUCCESS) {
		SYSLOG("user", "vm_allocate fail %d", ret);
		return 0;
	}

	auto writeProt = prot|VM_PROT_READ|VM_PROT_WRITE;
	ret = vmProtect(taskPort, addr, size, FALSE, writeProt);
	if (ret == KERN_SUCCESS) {
		ret = orgVmMapWriteUser(taskPort, payload, addr, size);
		if (ret == KERN_SUCCESS) {
			if (writeProt != prot)
				ret = vmProtect(taskPort, addr, size, FALSE, prot);
			if (ret == KERN_SUCCESS)
				return addr;
			else
				SYSLOG("user", "vm_protect final %X fail %d", prot, ret);
		} else {
			SYSLOG("user", "vm_write_user fail %d", ret);
		}
	} else {
		SYSLOG("user", "vm_protect initial %X fail %d", writeProt, ret);
	}

	return 0;
}

bool UserPatcher::injectPayload(vm_map_t taskPort, uint8_t *payload, size_t size, void *ep) {
	if (size > PAGE_SIZE) {
		SYSLOG("user", "unreasonably large payload %lu", size);
		return false;
	}

	// Get task's mach-o header and determine its cpu type
	auto baseAddr = orgGetMapMin(taskPort);
	DBGLOG("user", "injectPayload map min is " PRIKADDR, CASTKADDR(baseAddr));

	kern_return_t err = orgVmMapReadUser(taskPort, baseAddr, tmpBufferData, sizeof(tmpBufferData));
	auto machHeader = reinterpret_cast<mach_header_64 *>(tmpBufferData);
	if (err == KERN_SUCCESS){
		if (machHeader->magic == MH_MAGIC_64 || machHeader->magic == MH_MAGIC) {
			size_t hdrSize = machHeader->magic == MH_MAGIC ? sizeof(mach_header) : sizeof(mach_header_64);
			uintptr_t newEp = hdrSize + machHeader->sizeofcmds;
			if (newEp + PAGE_SIZE > sizeof(tmpBufferData)) {
				SYSLOG("user", "unpredictably large image header %lu", newEp);
				return false;
			}

			uint32_t *entry32 = nullptr;
			uint64_t *entry64 = nullptr;
			bool vmEp = true;
			uint64_t vmBase = 0;

			uint8_t *currPtr = tmpBufferData + hdrSize;
			for (uint32_t i = 0; i < machHeader->ncmds; i++) {
				auto cmd = reinterpret_cast<load_command *>(currPtr);

				if (cmd->cmd == LC_MAIN) {
					static constexpr size_t MainOff {0x8};
					entry64 = reinterpret_cast<uint64_t *>(currPtr + MainOff);
					vmEp = false;
				} else if (cmd->cmd == LC_UNIXTHREAD) {
					if (machHeader->magic == MH_MAGIC_64) {
						static constexpr size_t UnixThreadOff64 {0x90};
						entry64 = reinterpret_cast<uint64_t *>(currPtr + UnixThreadOff64);
					} else {
						static constexpr size_t UnixThreadOff32 {0x38};
						entry32 = reinterpret_cast<uint32_t *>(currPtr + UnixThreadOff32);
					}
				} else if (cmd->cmd == LC_SEGMENT) {
					auto seg = reinterpret_cast<segment_command *>(currPtr);
					if (seg->fileoff == 0 && seg->filesize > 0)
						vmBase = seg->vmaddr;
				} else if (cmd->cmd == LC_SEGMENT_64) {
					auto seg = reinterpret_cast<segment_command_64 *>(currPtr);
					if (seg->fileoff == 0 && seg->filesize > 0)
						vmBase = seg->vmaddr;
				}
				currPtr += cmd->cmdsize;
			}

			uint64_t orgEp = baseAddr - (vmEp ? vmBase : 0);
			uint64_t dstEp = newEp + (vmEp ? vmBase : 0);
			if (entry64) {
				orgEp += *entry64;
				*entry64 = dstEp;
			} else if (entry32) {
				orgEp += *entry32;
				*entry32 = static_cast<uint32_t>(dstEp);
			} else {
				SYSLOG("user", "failed to find valid entrypoint");
				return false;
			}

			if (ep) {
				if (machHeader->magic == MH_MAGIC_64)
					*static_cast<uint64_t *>(ep) = orgEp;
				else
					*static_cast<uint32_t *>(ep) = static_cast<uint32_t>(orgEp);
			}

			vm_prot_t prots[sizeof(tmpBufferData)/PAGE_SIZE] {};

			for (size_t i = 0; i < arrsize(prots); i++) {
				prots[i] = getPageProtection(taskPort, baseAddr + i * PAGE_SIZE);

				// Note, that we don't restore memory protection if we fail somewhere (no need to push something non-critical)
				// Enable writing for the calculated regions
				if (!(prots[i] & VM_PROT_WRITE)) {
					auto res = vmProtect(taskPort, (vm_offset_t)baseAddr + i * PAGE_SIZE, PAGE_SIZE, FALSE, prots[i]|VM_PROT_WRITE);
					if (res != KERN_SUCCESS) {
						SYSLOG("user", "failed to change memory protection (%lu, %d)", i, res);
						return false;
					}
				}
			}

			// Write new ep
			lilu_os_memcpy(tmpBufferData + newEp, payload, size);
			auto res = orgVmMapWriteUser(taskPort, tmpBufferData, baseAddr, sizeof(tmpBufferData));
			if (res != KERN_SUCCESS) {
				SYSLOG("user", "failed to chage ep (%d)", res);
				return false;
			}

			// Restore protection flags
			for (size_t i = 0; i < arrsize(prots); i++) {
				if (!(prots[i] & VM_PROT_WRITE)) {
					res = vmProtect(taskPort, (vm_offset_t)baseAddr + i * PAGE_SIZE, PAGE_SIZE, FALSE, prots[i]);
					if (res != KERN_SUCCESS) {
						SYSLOG("user", "failed to restore memory protection (%lu, %d)", i, res);
						return true;
					}
				}
			}

		} else {
			SYSLOG("user", "unknown header magic %X", machHeader->magic);
		}
	} else {
		SYSLOG("user", "could not read target mach-o header (error %d)", err);
		return false;
	}

	return true;
}

kern_return_t UserPatcher::vmSharedRegionMapFile(vm_shared_region_t shared_region, unsigned int mappings_count, shared_file_mapping_np *mappings, memory_object_control_t file_control, memory_object_size_t file_size, void *root_dir, uint32_t slide, user_addr_t slide_start, user_addr_t slide_size) {
	auto res = FunctionCast(vmSharedRegionMapFile, that->orgVmSharedRegionMapFile)(shared_region, mappings_count, mappings, file_control, file_size, root_dir, slide, slide_start, slide_size);
	if (!slide) {
		that->patchSharedCache(that->orgCurrentMap(), 0, CPU_TYPE_X86_64);
	}
	return res;
}

int UserPatcher::vmSharedRegionSlide(uint32_t slide, mach_vm_offset_t entry_start_address, mach_vm_size_t entry_size, mach_vm_offset_t slide_start, mach_vm_size_t slide_size, memory_object_control_t sr_file_control) {

	DBGLOG("user", "params are %X %llX %llX %llX %llX", slide, entry_start_address, entry_size, slide_start, slide_size);

	that->patchSharedCache(that->orgCurrentMap(), slide, CPU_TYPE_X86_64);

	return FunctionCast(vmSharedRegionSlide, that->orgVmSharedRegionSlide)(slide, entry_start_address, entry_size, slide_start, slide_size, sr_file_control);
}

int UserPatcher::vmSharedRegionSlideMojave(uint32_t slide, mach_vm_offset_t entry_start_address, mach_vm_size_t entry_size, mach_vm_offset_t slide_start, mach_vm_size_t slide_size, mach_vm_offset_t slid_mapping, memory_object_control_t sr_file_control) {

	DBGLOG("user", "params are %X %llX %llX %llX %llX", slide, entry_start_address, entry_size, slide_start, slide_size);

	that->patchSharedCache(that->orgCurrentMap(), slide, CPU_TYPE_X86_64);

	return FunctionCast(vmSharedRegionSlideMojave, that->orgVmSharedRegionSlideMojave)(slide, entry_start_address, entry_size, slide_start, slide_size, slid_mapping, sr_file_control);
}

void UserPatcher::taskSetMainThreadQos(task_t task, thread_t main_thread) {
	FunctionCast(taskSetMainThreadQos, that->orgTaskSetMainThreadQos)(task, main_thread);

	auto entry = that->pending.get();
	if (entry) {
		DBGTRACE("user", "firing hook from task_set_main_thread_qos " PRIKADDR, CASTKADDR(current_thread()));
		that->patchBinary(that->orgGetTaskMap(task), (*entry)->path, (*entry)->pathLen);
		PANIC_COND(!that->pending.erase(), "user", "failed to remove pending user patch in task_set_main_thread_qos");
		delete *entry;
	}
}

void UserPatcher::patchSharedCache(vm_map_t taskPort, uint32_t slide, cpu_type_t cpu, bool applyChanges) {
	// Save the slide for restoration
	if (applyChanges && !sharedCacheSlideStored) {
		storedSharedCacheSlide = slide;
		sharedCacheSlideStored = true;
	}

	for (size_t i = 0, sz = lookupStorage.size(); i < sz; i++) {
		auto &storageEntry = lookupStorage[i];
		auto &mod = storageEntry->mod;
		for (size_t j = 0, rsz = storageEntry->refs.size(); j < rsz; j++) {
			auto &ref = storageEntry->refs[j];
			auto &patch = storageEntry->mod->patches[ref->i];
			size_t offNum = ref->segOffs.size();

			vm_address_t modStart = 0;
			vm_address_t modEnd = 0;

			static_assert(FileSegment::SegmentsTextStart == 0, "ABI changes should reflect code changes!");
			if (patch.segment <= FileSegment::SegmentsTextEnd) {
				modStart = mod->startTEXT;
				modEnd = mod->endTEXT;
			} else if (patch.segment >= FileSegment::SegmentsDataStart && patch.segment <= FileSegment::SegmentsDataEnd) {
				modStart = mod->startDATA;
				modEnd = mod->endDATA;
			}

			if (modStart && modEnd && offNum && patch.cpu == cpu) {
				DBGLOG("user", "patch for %s in %lX %lX", mod->path, modStart, modEnd);
				auto tmp = Buffer::create<uint8_t>(patch.size);
				if (tmp) {
					for (size_t k = 0; k < offNum; k++) {
						auto place = modStart+ref->segOffs[k]+slide;
						auto r = orgVmMapReadUser(taskPort, place, tmp, patch.size);
						if (!r) {
							bool comparison = !memcmp(tmp, applyChanges? patch.find : patch.replace, patch.size);
							DBGLOG("user", "%d/%d found %X %X %X %X", applyChanges, comparison, tmp[0], tmp[1], tmp[2], tmp[3]);
							if (comparison) {
								r = vmProtect(taskPort, (vm_offset_t)(place & -PAGE_SIZE), PAGE_SIZE, FALSE, VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE);
								if (r == KERN_SUCCESS) {
									DBGLOG("user", "obtained write permssions");

									r = orgVmMapWriteUser(taskPort, applyChanges ? patch.replace : patch.find, place, patch.size);

									if (r != KERN_SUCCESS)
										SYSLOG("user", "patching %llX -> res %d", place, r);

									r = vmProtect(taskPort, (vm_offset_t)(place & -PAGE_SIZE), PAGE_SIZE, FALSE, VM_PROT_READ|VM_PROT_EXECUTE);
									if (r == KERN_SUCCESS)
										DBGLOG("user", "restored write permssions");
									else
										DBGLOG("user", "failed to restore write permssions %d", r);
								} else {
									SYSLOG("user", "failed to obtain write permissions for patching %d", r);
								}
							} else if (ADDPR(debugEnabled)) {
								for (size_t l = 0; l < patch.size; l++) {
									auto v = (applyChanges? patch.find : patch.replace)[l];
									if (tmp[l] != v) {
										DBGLOG("user", "miss at %lu: %02X vs %02X", l, tmp[l], v);
										break;
									}
								}
							}
						}

						DBGLOG("user", "done reading patches for %llX", ref->segOffs[k]);
					}
					Buffer::deleter(tmp);
				}
			}
		}
	}
}

size_t UserPatcher::mapAddresses(const char *mapBuf, MapEntry *mapEntries, size_t nentries) {
	if (nentries == 0 || !mapBuf)
		return 0;

	size_t nfound = 0;
	const char *ptr = mapBuf;
	while (*ptr) {
		size_t i = 1;
		if (*ptr == '\n') {
			MapEntry *currEntry = nullptr;

			for (size_t j = 0; j < nentries; j++) {
				if (!mapEntries[j].filename)
					continue;
				if (!strncmp(&ptr[i], mapEntries[j].filename, mapEntries[j].length)) {
					currEntry = &mapEntries[j];
					i += mapEntries[j].length;
					break;
				}
			}

			if (currEntry) {
				const char *text = strstr(&ptr[i], "__TEXT", strlen("__TEXT"));
				if (text) {
					i += strlen("__TEXT");
					const char *arrow = strstr(&ptr[i], "->", strlen("->"));
					if (arrow) {
						currEntry->startTEXT = lilu_strtou(text + strlen("__TEXT") + 1, nullptr, 16);
						currEntry->endTEXT = lilu_strtou(arrow + strlen("->") + 1, nullptr, 16);

						const char *data = strstr(&ptr[i], "__DATA", strlen("__DATA"));
						if (data) {
							i += strlen("__DATA");
							arrow = strstr(&ptr[i], "->", strlen("->"));
							if (arrow) {
								currEntry->startDATA = lilu_strtou(data + strlen("__DATA") + 1, nullptr, 16);
								currEntry->endDATA = lilu_strtou(arrow + strlen("->") + 1, nullptr, 16);
							}
						}

						nfound++;
					}
				}
			}
		}
		ptr += i;
	}

	return nfound;
}

bool UserPatcher::loadDyldSharedCacheMapping() {
	DBGLOG("user", "loadDyldSharedCacheMapping %lu", binaryModSize);

	if (binaryModSize == 0)
		return true;

	uint8_t *buffer {nullptr};
	size_t bufferSize {0};
	bool isHaswell = BaseDeviceInfo::get().cpuHasAvx2;
	if (getKernelVersion() >= KernelVersion::BigSur) {
		buffer = FileIO::readFileToBuffer(isHaswell ? bigSurSharedCacheMapHaswell : bigSurSharedCacheMapLegacy, bufferSize);
	}
	else if (isHaswell && getKernelVersion() >= KernelVersion::Yosemite) {
		buffer = FileIO::readFileToBuffer(SharedCacheMapHaswell, bufferSize);
	}

	if (!buffer)
		buffer = FileIO::readFileToBuffer(SharedCacheMapLegacy, bufferSize);

	bool res {false};

	if (buffer && bufferSize > 0) {
		auto entries = Buffer::create<MapEntry>(binaryModSize);
		if (entries) {
			for (size_t i = 0; i < binaryModSize; i++) {
				entries[i].filename = binaryMod[i]->path;
				entries[i].length = strlen(binaryMod[i]->path);
				entries[i].startTEXT = entries[i].endTEXT = entries[i].startDATA = entries[i].endDATA = 0;
			}

			size_t nEntries = mapAddresses(reinterpret_cast<char *>(buffer), entries, binaryModSize);

			if (nEntries > 0) {
				DBGLOG("user", "mapped %lu entries out of %lu", nEntries, binaryModSize);

				for (size_t i = 0; i < binaryModSize; i++) {
					binaryMod[i]->startTEXT = entries[i].startTEXT;
					binaryMod[i]->endTEXT = entries[i].endTEXT;
					binaryMod[i]->startDATA = entries[i].startDATA;
					binaryMod[i]->endDATA = entries[i].endDATA;
				}

				res = true;
			} else {
				SYSLOG("user", "failed to map any entry out of %lu", binaryModSize);
			}
		} else {
			SYSLOG("user", "failed to allocate memory for MapEntry %lu", binaryModSize);
		}

		if (entries) Buffer::deleter(entries);
	} else {
		SYSLOG("user", "no dyld_shared_cache discovered, fallback to slow!");
		patchDyldSharedCache = false;
		res = true;
	}

	if (buffer) Buffer::deleter(buffer);

	return res;
}

bool UserPatcher::loadFilesForPatching() {
	DBGLOG("user", "loadFilesForPatching %lu", binaryModSize);

	for (size_t i = 0; i < binaryModSize; i++) {
		bool hasPatches = false;

		for (size_t p = 0; p < binaryMod[i]->count; p++) {
			if (binaryMod[i]->patches[p].section != ProcInfo::SectionDisabled) {
				hasPatches = true;
				break;
			}
		}

		if (hasPatches) {
			DBGLOG("user", "requesting file %s at %lu", binaryMod[i]->path, i);
		} else {
			DBGLOG("user", "ignoring file %s at %lu, no mods out of %lu apply", binaryMod[i]->path, i, binaryMod[i]->count);
			continue;
		}

		size_t fileSize;
		auto buf = FileIO::readFileToBuffer(binaryMod[i]->path, fileSize);
		if (buf) {
			vm_address_t vmsegment {0};
			vm_address_t vmsection {0};
			void *sectionptr {nullptr};
			size_t size {0};

			DBGLOG("user", "have %lu mods for %s (read as %lu)", binaryMod[i]->count, binaryMod[i]->path, fileSize);

			for (size_t p = 0; p < binaryMod[i]->count; p++) {
				auto &patch = binaryMod[i]->patches[p];

				if (patch.section == ProcInfo::SectionDisabled) {
					DBGLOG("user", "skipping not requested patch %s for %lu", binaryMod[i]->path, p);
					continue;
				}

				if (patch.segment >= FileSegment::SegmentTotal) {
					SYSLOG("user", "skipping patch %s for %lu with invalid segment id %u", binaryMod[i]->path, p, patch.segment);
					continue;
				}

				MachInfo::findSectionBounds(buf, fileSize, vmsegment, vmsection, sectionptr, size,
											fileSegments[patch.segment], fileSections[patch.segment], patch.cpu);

				DBGLOG("user", "findSectionBounds returned vmsegment %lX vmsection %lX sectionptr %p size %lu", vmsegment, vmsection, sectionptr, size);

				if (size) {
					uint8_t *start = reinterpret_cast<uint8_t *>(sectionptr);
					uint8_t *end = start + size - patch.size;
					size_t skip = patch.skip;
					size_t count = patch.count;

					DBGLOG("user", "this patch will start from %lu entry and will replace %lu findings", skip, count);

					while (start < end && count) {
						if (!memcmp(start, patch.find, patch.size)) {
							DBGLOG("user", "found entry of %X %X patch", patch.find[0], patch.find[1]);

							if (skip == 0) {
								off_t sectOff = start - reinterpret_cast<uint8_t *>(sectionptr);
								vm_address_t vmpage = (vmsection + (vm_address_t)sectOff) & -PAGE_SIZE;
								vm_address_t pageOff = vmpage - vmsection;
								off_t valueOff = reinterpret_cast<uintptr_t>(start - pageOff - reinterpret_cast<uintptr_t>(sectionptr));
								off_t segOff = vmsection-vmsegment+sectOff;

								DBGLOG("user", "using it off %llX pageOff %llX new %lX segOff %llX", sectOff, pageOff, vmpage, segOff);

								// We need binary entry, i.e. the page our patch belong to
								LookupStorage *entry = nullptr;
								for (size_t e = 0, esz = lookupStorage.size(); e < esz && !entry; e++) {
									if (lookupStorage[e]->pageOff == static_cast<vm_address_t>(pageOff))
										entry = lookupStorage[e];
								}


								if (!entry) {
									entry = LookupStorage::create();
									if (entry) {
										entry->mod = binaryMod[i];
										if (!entry->page->alloc()) {
											LookupStorage::deleter(entry);
											entry = nullptr;
										} else {
											// One could find entries by flooring first ref address but that's unreasonably complicated
											entry->pageOff = pageOff;
											// Now copy page data
											lilu_os_memcpy(entry->page->p, reinterpret_cast<uint8_t *>(sectionptr) + pageOff, PAGE_SIZE);
											DBGLOG("user", "first page bytes are %X %X %X %X %X %X %X %X",
												   entry->page->p[0], entry->page->p[1], entry->page->p[2], entry->page->p[3],
												   entry->page->p[4], entry->page->p[5], entry->page->p[6], entry->page->p[7]);
											// Save entry in lookupStorage
											if (!lookupStorage.push_back<2>(entry)) {
												SYSLOG("user", "failed to push entry to LookupStorage");
												LookupStorage::deleter(entry);
												entry = nullptr;
												continue;
											}
										}
									}

									if (!entry) {
										SYSLOG("user", "failed to allocate memory for LookupStorage");
										continue;
									}
								}

								// Use an existent reference to the same patch in the same page if any.
								// Happens when a patch has 2+ replacements and they are close to each other.
								LookupStorage::PatchRef *ref = nullptr;
								for (size_t r = 0, rsz = entry->refs.size(); r < rsz && !ref; r++) {
									if (entry->refs[r]->i == p) {
										ref = entry->refs[r];
									}
								}

								DBGLOG("user", "ref find %d", ref != nullptr);

								// Or add a new patch reference
								if (!ref) {
									ref = LookupStorage::PatchRef::create();
									if (!ref) {
										SYSLOG("user", "failed to allocate memory for PatchRef");
										continue;
									}
									ref->i = p; // Set the reference patch
									if (!entry->refs.push_back<2>(ref)) {
										SYSLOG("user", "failed to insert PatchRef");
										LookupStorage::PatchRef::deleter(ref);
										continue;
									}
								}

								if (ref) {
									DBGLOG("user", "pushing off %llX to patch", valueOff);
									// These values belong to the current ref
									ref->pageOffs.push_back<2>(valueOff);
									ref->segOffs.push_back<2>(segOff);
								}
								count--;
							} else {
								skip--;
							}
						}
						start++;
					}
				} else {
					SYSLOG("user", "failed to obtain a corresponding section");
				}
			}

			Buffer::deleter(buf);
		}
	}
	return true;
}

bool UserPatcher::loadLookups() {
	uint32_t off = 0;

	for (size_t i = 0; i < Lookup::matchNum; i++) {
		auto &lookupCurr = lookup.c[i];

		DBGLOG("user", "loading lookup %lu current off is %X", i, off);

		auto obtainValues = [&lookupCurr, &off, this]() {
			for (size_t p = 0; p < lookupStorage.size(); p++) {
				uint64_t val = *reinterpret_cast<uint64_t *>(lookupStorage[p]->page->p + off);
				if (p >= lookupCurr.size()) {
					lookupCurr.push_back<2>(val);
				} else {
					lookupCurr[p] = val;
				}
			}
		};

		auto hasSameValues = [&lookupCurr]() {
			for (size_t i = 0, sz = lookupCurr.size(); i < sz; i++) {
				for (size_t j = i + 1; j < sz; j++) {
					if (lookupCurr[i] == lookupCurr[j]) {
						return true;
					}
				}
			}

			return false;
		};

		// First match must choose a page
		if (i == 0) {
			// Find non matching off
			while (off < PAGE_SIZE) {
				// Obtain values
				obtainValues();

				if (!hasSameValues()) {
					DBGLOG("user", "successful finding at %X", off);
					lookup.offs[i] = off;
					break;
				}

				off += sizeof(uint64_t);
			}
		} else {
			if (off == PAGE_SIZE) {
				DBGLOG("user", "resetting off to 0");
				off = 0;
			}

			if (off == lookup.offs[0]) {
				DBGLOG("user", "matched off %X with 0th", off);
				off += sizeof(uint64_t);
			}

			DBGLOG("user", "chose %X", off);

			obtainValues();
			lookup.offs[i] = off;

			off += sizeof(uint64_t);
		}

	}

	return true;
}

vm_prot_t UserPatcher::getPageProtection(vm_map_t map, vm_map_address_t addr) {
	vm_prot_t prot = VM_PROT_NONE;
	if (orgVmMapCheckProtection(map, addr, addr+PAGE_SIZE, VM_PROT_READ))
		prot |= VM_PROT_READ;
	if (orgVmMapCheckProtection(map, addr, addr+PAGE_SIZE, VM_PROT_WRITE))
		prot |= VM_PROT_WRITE;
	if (orgVmMapCheckProtection(map, addr, addr+PAGE_SIZE, VM_PROT_EXECUTE))
		prot |= VM_PROT_EXECUTE;

	return prot;
}

bool UserPatcher::hookMemoryAccess() {
	// 10.12 and newer
	KernelPatcher::RouteRequest rangeRoute {"_cs_validate_range", codeSignValidateRangeWrapper, orgCodeSignValidateRangeWrapper};
	if (!patcher->routeMultipleLong(KernelPatcher::KernelID, &rangeRoute, 1)) {
		KernelPatcher::RouteRequest pageRoute {"_cs_validate_page", codeSignValidatePageWrapper, orgCodeSignValidatePageWrapper};
		if (!patcher->routeMultipleLong(KernelPatcher::KernelID, &pageRoute, 1)) {
			SYSLOG("user", "failed to resolve _cs_validate function");
			return false;
		}
	}

	orgCurrentMap = reinterpret_cast<t_currentMap>(patcher->solveSymbol(KernelPatcher::KernelID, "_current_map"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user", "failed to resolve _current_map");
		patcher->clearError();
		return false;
	}

	orgGetMapMin = reinterpret_cast<t_getMapMin>(patcher->solveSymbol(KernelPatcher::KernelID, "_get_map_min"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user", "failed to resolve _get_map_min");
		patcher->clearError();
		return false;
	}

	orgGetTaskMap = reinterpret_cast<t_getTaskMap>(patcher->solveSymbol(KernelPatcher::KernelID, "_get_task_map"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user", "failed to resolve _get_task_map");
		patcher->clearError();
		return false;
	}

	orgVmMapSwitchProtect = reinterpret_cast<t_vmMapSwitchProtect>(patcher->solveSymbol(KernelPatcher::KernelID, "_vm_map_switch_protect"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		DBGLOG("user", "failed to resolve _vm_map_switch_protect");
		patcher->clearError();
		// Not an error, may be missing
	}

	orgVmMapCheckProtection = reinterpret_cast<t_vmMapCheckProtection>(patcher->solveSymbol(KernelPatcher::KernelID, "_vm_map_check_protection"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user", "failed to resolve _vm_map_check_protection");
		patcher->clearError();
		return false;
	}

	orgVmMapReadUser = reinterpret_cast<t_vmMapReadUser>(patcher->solveSymbol(KernelPatcher::KernelID, "_vm_map_read_user"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user", "failed to resolve _vm_map_read_user");
		patcher->clearError();
		return false;
	}

	orgVmMapWriteUser = reinterpret_cast<t_vmMapWriteUser>(patcher->solveSymbol(KernelPatcher::KernelID, "_vm_map_write_user"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user", "failed to resolve _vm_map_write_user");
		patcher->clearError();
		return false;
	}

	// On 10.12.1 b4 Apple decided not to let current_map point to the current process
	// For this reason we have to obtain the map with the other methods
	if (getKernelVersion() >= KernelVersion::Sierra) {
		KernelPatcher::RouteRequest request {"_task_set_main_thread_qos", taskSetMainThreadQos, orgTaskSetMainThreadQos};
		if (!patcher->routeMultiple(KernelPatcher::KernelID, &request, 1, 0, 0, true, false)) {
			DBGLOG("user", "failed to hook _task_set_main_thread_qos");
			// This is not an error, early 10.12 versions have no such function
		}
	}

	if (patchDyldSharedCache) {
		KernelPatcher::RouteRequest mapRoute {"_vm_shared_region_map_file", vmSharedRegionMapFile, orgVmSharedRegionMapFile};
		if (!patcher->routeMultipleLong(KernelPatcher::KernelID, &mapRoute, 1)) {
			SYSLOG("user", "failed to hook _vm_shared_region_map_file");
			return false;
		}
		
		if (getKernelVersion() >= KernelVersion::Mojave) {
			KernelPatcher::RouteRequest sharedRegionRoute {"_vm_shared_region_slide", vmSharedRegionSlideMojave, orgVmSharedRegionSlideMojave};
			if (!patcher->routeMultipleLong(KernelPatcher::KernelID, &sharedRegionRoute, 1)) {
				SYSLOG("user", "failed to hook _vm_shared_region_slide");
				return false;
			}
		} else {
			KernelPatcher::RouteRequest sharedRegionRoute {"_vm_shared_region_slide", vmSharedRegionSlide, orgVmSharedRegionSlide};
			if (!patcher->routeMultipleLong(KernelPatcher::KernelID, &sharedRegionRoute, 1)) {
				SYSLOG("user", "failed to hook _vm_shared_region_slide");
				return false;
			}
		}
	}

	return true;
}

void UserPatcher::activate() {
	atomic_store_explicit(&activated, true, memory_order_relaxed);
}

const char *UserPatcher::getSharedCachePath() {
	bool isHaswell = BaseDeviceInfo::get().cpuHasAvx2;
	if (getKernelVersion() >= KernelVersion::Ventura)
		return isHaswell ? venturaSharedCacheHaswell : venturaSharedCacheLegacy;
	else if (getKernelVersion() >= KernelVersion::BigSur)
		return isHaswell ? bigSurSharedCacheHaswell : bigSurSharedCacheLegacy;
	return isHaswell ? sharedCacheHaswell : sharedCacheLegacy;
}

bool UserPatcher::matchSharedCachePath(const char *path) {
	if (getKernelVersion() >= KernelVersion::BigSur) {
		auto dyld_path = getKernelVersion() >= KernelVersion::Ventura ? venturaSharedCacheLegacy : bigSurSharedCacheLegacy;
		auto len = strlen(dyld_path);
		if (strncmp(path, dyld_path, len) != 0)
			return false;
		path += len;
	} else {
		auto len = strlen(sharedCacheLegacy);
		if (strncmp(path, sharedCacheLegacy, len) != 0)
			return false;
		path += len;
	}

	// Allow non-haswell cache on haswell, but not otherwise.
	if (BaseDeviceInfo::get().cpuHasAvx2 && path[0] == 'h')
		path++;

	// Skip suffix matching on macOS 12 and newer
	if (getKernelVersion() >= KernelVersion::Monterey && path[0] == '.' && path[1] >= '1' && path[1] <= '9')
		path += 2;

	return path[0] == '\0';
}
