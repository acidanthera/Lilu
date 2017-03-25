//
//  kern_user.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_file.hpp>
#include <PrivateHeaders/kern_config.hpp>

#include <mach/vm_map.h>
#include <mach-o/fat.h>
#include <kern/task.h>

static UserPatcher *that {nullptr};

int UserPatcher::execListener(kauth_cred_t credential, void *idata, kauth_action_t action, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
	
	// Make sure this is ours
	if (idata == that->cookie && action == KAUTH_FILEOP_EXEC && arg1) {
		const char *path = reinterpret_cast<const char *>(arg1);
		that->onPath(path, static_cast<uint32_t>(strlen(path)));
	} else {
		//DBGLOG("user @ listener did not match our needs action %d cookie %d", action, idata == that->cookie);
	}
	
	return 0;
}

bool UserPatcher::init(KernelPatcher &kernelPatcher, bool preferSlowMode) {
	that = this;
	patchDyldSharedCache = !preferSlowMode;
	patcher = &kernelPatcher;
	
	listener = kauth_listen_scope(KAUTH_SCOPE_FILEOP, execListener, cookie);
	
	if (!listener) {
		SYSLOG("user @ failed to register a listener");
		return false;
	}
	
	return true;
}

bool UserPatcher::registerPatches(ProcInfo **procs, size_t procNum, BinaryModInfo **mods, size_t modNum, t_BinaryLoaded callback, void *user) {
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
	if (listener) {
		kauth_unlisten_scope(listener);
		listener = nullptr;
	}
	
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
                            DBGLOG("user @ found a possible match for %zu of %llX\n", i, value);
                            break;
                        }
                    }
                } else {
                    if (lookup.c[i][maybe] != value) {
                        // We failed
                        DBGLOG("user @ failure not matching %zu of %llX to expected %llX\n", i, value, lookup.c[i][maybe]);
                        maybe = sz;
                    } else {
                        DBGLOG("user @ found a possible match for %zu of %llX\n", i, value);
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
						
						
						DBGLOG("user @ found what we are looking for %X %X %X %X %X %X %X %X\n", rpatch.find[0],
								rpatch.size > 1 ? rpatch.find[1] : 0xff,
								rpatch.size > 2 ? rpatch.find[2] : 0xff,
								rpatch.size > 3 ? rpatch.find[3] : 0xff,
								rpatch.size > 4 ? rpatch.find[4] : 0xff,
								rpatch.size > 5 ? rpatch.find[5] : 0xff,
								rpatch.size > 6 ? rpatch.find[6] : 0xff,
								rpatch.size > 7 ? rpatch.find[7] : 0xff
						);
						
						if (sz > 0 && MachInfo::setKernelWriting(true) == KERN_SUCCESS) {
							DBGLOG("user @ obtained write permssions\n");
						
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
										memcpy(patch, rpatch.replace, rpatch.size);
								}
							}
						
							if (MachInfo::setKernelWriting(false) == KERN_SUCCESS) {
								DBGLOG("user @ restored write permssions\n");
							}
						} else {
							SYSLOG("user @ failed to obtain write permssions for %zu\n", sz);
						}
					}
                } else {
                    DBGLOG("user @ failed to match a complete page with %zu\n", maybe);
                }
            }
        }
    }
}

boolean_t UserPatcher::codeSignValidatePageWrapper(void *blobs, memory_object_t pager, memory_object_offset_t page_offset, const void *data, unsigned *tainted) {
	boolean_t res = that->orgCodeSignValidatePageWrapper(blobs, pager, page_offset, data, tainted);
	if (res) that->performPagePatch(data, PAGE_SIZE);
	return res;
}

boolean_t UserPatcher::codeSignValidateRangeWrapper(void *blobs, memory_object_t pager, memory_object_offset_t range_offset, const void *data, memory_object_size_t data_size, unsigned *tainted) {
    boolean_t res = that->orgCodeSignValidateRangeWrapper(blobs, pager, range_offset, data, data_size, tainted);
    
    if (res)
        that->performPagePatch(data, data_size);
    
    /*DBGLOG("user @ cs_validate_range %llX %llX %llX %llX -> %llX %llX", (uint64_t)blobs, (uint64_t)pager, range_offset, (uint64_t)data, data_size, (uint64_t)tainted);*/

    return res;
}

void UserPatcher::onPath(const char *path, uint32_t len) {
	if (len >= currentMinProcLength) {
		for (uint32_t i = 0; i < procInfoSize; i++) {
			auto p = procInfo[i];
			if (p->len == len && !strncmp(p->path, path, len)) {
				DBGLOG("user @ caught %s performing injection", path);
				if (orgProcExecSwitchTask) {
					DBGLOG("user @ requesting proc_exec_switch_task patch");
					strncpy(pendingPath, path, MAXPATHLEN);
					pendingPathLen = len;
					pendingPatchCallback = true;
				} else {
					patchBinary(orgCurrentMap(), path, len);
				}
				
				return;
			}
		}
	}
}

void UserPatcher::patchBinary(vm_map_t map, const char *path, uint32_t len) {
	if (patchDyldSharedCache && sharedCacheSlideStored) {
		patchSharedCache(map, storedSharedCacheSlide, CPU_TYPE_X86_64);
	} else {
		if (patchDyldSharedCache) SYSLOG("user @ no slide present, initialisation failed, fallback to restrict");
		injectRestrict(map);
	}
	userCallback.first(userCallback.second, *this, map, path, len);
}

bool UserPatcher::injectRestrict(vm_map_t taskPort) {
	// Get task's mach-o header and determine its cpu type
    //auto taskPort = orgCurrentMap();
	auto baseAddr = orgGetMapMin(taskPort);
	
	DBGLOG("user @ get_map_min returned %llX", baseAddr);
	
	kern_return_t err = orgVmMapReadUser(taskPort, baseAddr, &tmpHeader, sizeof(mach_header_64));
	
	if (err == KERN_SUCCESS){
		constexpr size_t hdr_size = /*tmpHeader.magic == MH_MAGIC ? sizeof(mach_header) :*/ sizeof(mach_header_64);
		
		if (tmpHeader.magic == MH_MAGIC_64 /*|| header->magic == MH_MAGIC*/) {
			
			// Calculate protected addresses
			kern_return_t res = KERN_SUCCESS;
			
			struct {
				uintptr_t off;
				uint32_t val;
			} prots[3] {};
			long org_b = hdr_size + tmpHeader.sizeofcmds;
			long new_b = org_b + sizeof(restrictSegment);
			
			prots[0].off = baseAddr;
			prots[0].val = getPageProtection(taskPort, prots[0].off);
			
			if (org_b + sizeof(restrictSegment) > PAGE_SIZE){
				prots[1].off = baseAddr + org_b - org_b % PAGE_SIZE;
				prots[1].val = getPageProtection(taskPort, prots[1].off);
				if (baseAddr + new_b  > prots[1].off + PAGE_SIZE){
					prots[2].off = prots[1].off + PAGE_SIZE;
					prots[2].val = getPageProtection(taskPort, prots[2].off);
				}
			}
			
			//FIXME: check the space available
			
			// Note, that we don't restore memory protection if we fail somewhere (no need to push something non-critical)
			// Enable writing for the calculated regions
			for (int i = 0; i < 3; i++) {
				if (prots[i].off && !(prots[i].val & VM_PROT_WRITE)) {
					res = vm_protect(taskPort, prots[i].off, PAGE_SIZE, FALSE, prots[i].val|VM_PROT_WRITE);
					if (res != KERN_SUCCESS) {
						SYSLOG("user @ failed to change memory protection (%d, %d)", i, res);
						return true;
					}
				}
			}
	
			vm_map_address_t ncmds_addr = baseAddr+16;
			//vm_map_address_t sizeofcmds_addr = result->mach_header+20;
			vm_map_address_t newcmd_addr = baseAddr+hdr_size+tmpHeader.sizeofcmds;
			
			uint64_t org_combined_value = (((uint64_t)tmpHeader.sizeofcmds) << 32) | (tmpHeader.ncmds);
			uint64_t combined_value = (((uint64_t)(tmpHeader.sizeofcmds + sizeof(restrictSegment))) << 32) | (tmpHeader.ncmds + 1);
			
			// Write new number and size of commands
			res = orgVmMapWriteUser(taskPort, &combined_value, ncmds_addr, sizeof(uint64_t));
			if (res != KERN_SUCCESS) {
				SYSLOG("user @ failed to change mach header (%d)", res);
				return true;
			}
			
			// Write the load command
			res = orgVmMapWriteUser(taskPort, &restrictSegment, newcmd_addr, sizeof(restrictSegment));
			if (res != KERN_SUCCESS) {
				SYSLOG("user @ failed to add dylib load command (%d), reverting...", res);
				res = orgVmMapWriteUser(taskPort, &org_combined_value, ncmds_addr, sizeof(uint64_t));
				if (res != KERN_SUCCESS) {
					SYSLOG("user @ failed to restore mach header (%d), this process will crash...", res);
				}
				return true;
			}
			
			// Restore protection flags
			for (int i = 0; i < 3; i++) {
				if (prots[i].off && !(prots[i].val & VM_PROT_WRITE)) {
					res = vm_protect(taskPort, prots[i].off, PAGE_SIZE, FALSE, prots[i].val);
					if (res != KERN_SUCCESS) {
						SYSLOG("user @ failed to restore memory protection (%d, %d)", i, res);
						return true;
					}
				}
			}

		} else {
			SYSLOG("user @ unknown header magic %X", tmpHeader.magic);
		}
	} else {
		SYSLOG("user @ could not read target mach-o header (error %d)", err);
		return false;
	}
	
	return true;
}

kern_return_t UserPatcher::vmSharedRegionMapFile(vm_shared_region_t shared_region, unsigned int mappings_count, shared_file_mapping_np *mappings, memory_object_control_t file_control, memory_object_size_t file_size, void *root_dir, uint32_t slide, user_addr_t slide_start, user_addr_t slide_size) {
	auto res = that->orgVmSharedRegionMapFile(shared_region, mappings_count, mappings, file_control, file_size, root_dir, slide, slide_start, slide_size);
	if (!slide) {
		that->patchSharedCache(that->orgCurrentMap(), 0, CPU_TYPE_X86_64);
	}
	return res;
}

int UserPatcher::vmSharedRegionSlide(uint32_t slide, mach_vm_offset_t entry_start_address, mach_vm_size_t entry_size, mach_vm_offset_t slide_start, mach_vm_size_t slide_size, memory_object_control_t sr_file_control) {

	DBGLOG("user @ params are %X %llX %llX %llX %llX", slide, entry_start_address, entry_size, slide_start, slide_size);
	
	that->patchSharedCache(that->orgCurrentMap(), slide, CPU_TYPE_X86_64);
	
	return that->orgVmSharedRegionSlide(slide, entry_start_address, entry_size, slide_start, slide_size, sr_file_control);
}

int UserPatcher::exImgact(image_params *igmp) {
    int res = that->orgExecMachImgact(igmp);
    
    if (res == 0 && igmp) {
        auto buf = Buffer::create<char>(PATH_MAX);
        if (buf) {
            int len = PATH_MAX;
            vn_getpath(igmp->ip_vp, buf, &len);
			len--; // null terminator
			
            if (len > 0) {
                //DBGLOG("user @ imgact found %s (%d)", buf, len);
                that->onPath(buf, static_cast<uint32_t>(len));
            }
            
            Buffer::deleter(buf);
        }
    }
    
    return res;
}

proc_t UserPatcher::procExecSwitchTask(proc_t p, task_t current_task, task_t new_task, thread_t new_thread) {
	proc_t rp = that->orgProcExecSwitchTask(p, current_task, new_task, new_thread);

	if (that->pendingPatchCallback) {
		DBGLOG("user @ firing hook from procExecSwitchTask\n");
		that->patchBinary(that->orgGetTaskMap(new_task), that->pendingPath, that->pendingPathLen);
		that->pendingPatchCallback = false;
	}

	return rp;
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
			if (mod->start && mod->end && offNum && patch.cpu == cpu) {
				DBGLOG("user @ patch for %s in %lX %lX\n", mod->path, mod->start, mod->end);
				auto tmp = Buffer::create<uint8_t>(patch.size);
				if (tmp) {
					for (size_t k = 0; k < offNum; k++) {
						auto place = mod->start+ref->segOffs[k]+slide;
						auto r = orgVmMapReadUser(taskPort, place, tmp, patch.size);
						if (!r) {
							DBGLOG("user @ found %X %X %X %X", tmp[0], tmp[1], tmp[2], tmp[3]);
							if ((applyChanges && !memcmp(tmp, patch.find, patch.size)) ||
								(!applyChanges && !memcmp(tmp, patch.replace, patch.size))) {
								if (vm_protect(taskPort, (place & -PAGE_SIZE), PAGE_SIZE, FALSE, VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE) == KERN_SUCCESS) {
									DBGLOG("user @ obtained write permssions\n");
									
									r = orgVmMapWriteUser(taskPort, applyChanges ? patch.replace : patch.find, place, patch.size);
										
									DBGLOG("user @ patching %llX -> res %d", place, r);
										
									if (vm_protect(taskPort, (place & -PAGE_SIZE), PAGE_SIZE, FALSE, VM_PROT_READ|VM_PROT_EXECUTE) == KERN_SUCCESS) {
										DBGLOG("user @ restored write permssions\n");
									}

								} else {
									SYSLOG("user @ failed to obtain write permissions for patching");
								}
							}
						}
						
						DBGLOG("user @ done reading patches for %llX", ref->segOffs[k]);
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
						currEntry->start = strtouq(text + strlen("__TEXT") + 1, nullptr, 16);
						currEntry->end = strtouq(arrow + strlen("->") + 1, nullptr, 16);
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
	DBGLOG("user @ loading files %zu", binaryModSize);
	
	uint8_t *buffer {nullptr};
	size_t bufferSize {0};
	for (size_t i = 0; i < sharedCacheMapPathsNum; i++) {
		buffer = FileIO::readFileToBuffer(sharedCacheMap[i], bufferSize);
		if (buffer) break;
	}
	
	bool res {false};
	auto entries = Buffer::create<MapEntry>(binaryModSize);
	if (entries && buffer && bufferSize > 0) {
		for (size_t i = 0; i < binaryModSize; i++) {
			entries[i].filename = binaryMod[i]->path;
			entries[i].length = strlen(binaryMod[i]->path);
			entries[i].start = entries[i].end = 0;
		}
		
		size_t nEntries = mapAddresses(reinterpret_cast<char *>(buffer), entries, binaryModSize);
		
		if (nEntries > 0) {
			DBGLOG("user @ mapped %zu entries out of %zu", nEntries, binaryModSize);
			
			for (size_t i = 0; i < binaryModSize; i++) {
				binaryMod[i]->start = entries[i].start;
				binaryMod[i]->end = entries[i].end;
			}
			
			res = true;
		} else {
			SYSLOG("user @ failed to map any entry out of %zu", binaryModSize);
		}
	} else {
		SYSLOG("user @ failed to allocate memory for MapEntry %zu", binaryModSize);
	}
	
	if (buffer) Buffer::deleter(buffer);
	if (entries) Buffer::deleter(entries);
	
	return res;
}

bool UserPatcher::loadFilesForPatching() {
	DBGLOG("user @ loading files %zu", binaryModSize);

	for (size_t i = 0; i < binaryModSize; i++) {
		size_t fileSize;
		auto buf = FileIO::readFileToBuffer(binaryMod[i]->path, fileSize);
		if (buf) {
			vm_address_t vmsegment {0};
			vm_address_t vmsection {0};
			void *sectionptr {nullptr};
			size_t size {0};
		
			DBGLOG("user @ have %zu mods for %s (read as %zu)", binaryMod[i]->count, binaryMod[i]->path, fileSize);
		
			for (size_t p = 0; p < binaryMod[i]->count; p++) {
				auto &patch = binaryMod[i]->patches[p];
				
				if (!patch.section) {
					DBGLOG("user @ skipping not requested patch %s for %zu", binaryMod[i]->path, p);
					continue;
				}

				MachInfo::findSectionBounds(buf, vmsegment, vmsection, sectionptr, size, "__TEXT", "__text", patch.cpu);
				
				DBGLOG("user @ findSectionBounds returned vmsegment %lX vmsection %lX sectionptr %p size %zu", vmsegment, vmsection, sectionptr, size);
				
				if (size) {
					uint8_t *start = reinterpret_cast<uint8_t *>(sectionptr);
					uint8_t *end = start + size - patch.size;
					size_t skip = patch.skip;
					size_t count = patch.count;
					
					DBGLOG("user @ this patch will start from %zu entry and will replace %zu findings", skip, count);
					
					while (start < end && count) {
						if (!memcmp(start, patch.find, patch.size)) {
							DBGLOG("user @ found entry of %X %X patch", patch.find[0], patch.find[1]);
							
							if (skip == 0) {
								off_t sectOff = start - reinterpret_cast<uint8_t *>(sectionptr);
								vm_address_t vmpage = (vmsection + sectOff) & -PAGE_SIZE;
								off_t pageOff = vmpage - vmsection;
								off_t valueOff = reinterpret_cast<uintptr_t>(start - pageOff - reinterpret_cast<uintptr_t>(sectionptr));
								off_t segOff = vmsection-vmsegment+sectOff;
								
								DBGLOG("user @ using it off %llX pageOff %llX new %lX segOff %llX", sectOff, pageOff, vmpage, segOff);
								
								// We need binary entry, i.e. the page our patch belong to
								LookupStorage *entry = nullptr;
								for (size_t e = 0, esz = lookupStorage.size(); e < esz && !entry; e++) {
									if (lookupStorage[e]->pageOff == pageOff)
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
											memcpy(entry->page->p, reinterpret_cast<uint8_t *>(sectionptr) + pageOff, PAGE_SIZE);
											DBGLOG("user @ first page bytes are %X %X %X %X %X %X %X %X",
												   entry->page->p[0], entry->page->p[1], entry->page->p[2], entry->page->p[3],
												   entry->page->p[4], entry->page->p[5], entry->page->p[6], entry->page->p[7]);
											// Save entry in lookupStorage
											lookupStorage.push_back(entry);
										}
									}
									
									if (!entry) {
										SYSLOG("user @ failed to allocate memory for LookupStorage");
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
								
								DBGLOG("user @ ref find %d\n", ref != nullptr);
								
								// Or add a new patch reference
								if (!ref) {
									ref = LookupStorage::PatchRef::create();
									if (!ref) {
										SYSLOG("user @ failed to allocate memory for PatchRef");
										continue;
									}
									ref->i = p; // Set the reference patch
									entry->refs.push_back(ref);
								}
								
								DBGLOG("user @ ref pre %d\n", ref != nullptr);
								
								if (ref) {
									DBGLOG("user @ pushing off %llX to patch", valueOff);
									// These values belong to the current ref
									ref->pageOffs.push_back(valueOff);
									ref->segOffs.push_back(segOff);
								}
								count--;
							} else {
								skip--;
							}
						}
						start++;
					}
				} else {
					SYSLOG("user @ failed to obtain a corresponding section");
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
		
		DBGLOG("user @ loading lookup %zu current off is %X", i, off);
		
		auto obtainValues = [&lookupCurr, &off, this]() {
			for (size_t p = 0; p < lookupStorage.size(); p++) {
				uint64_t val = *reinterpret_cast<uint64_t *>(lookupStorage[p]->page->p + off);
				if (p >= lookupCurr.size()) {
					lookupCurr.push_back(val);
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
					DBGLOG("user @ successful finding at %X", off);
					lookup.offs[i] = off;
					break;
				}
				
				off += sizeof(uint64_t);
			}
		} else {
			if (off == PAGE_SIZE) {
				DBGLOG("user @ resetting off to 0");
				off = 0;
			}
			
			if (off == lookup.offs[0]) {
				DBGLOG("user @ matched off %X with 0th", off);
				off += sizeof(uint64_t);
			}
			
			DBGLOG("user @ chose %X", off);
				
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
	mach_vm_address_t kern = patcher->solveSymbol(KernelPatcher::KernelID, "_cs_validate_page");
	
	if (patcher->getError() == KernelPatcher::Error::NoError) {
        orgCodeSignValidatePageWrapper = reinterpret_cast<t_codeSignValidatePageWrapper>(
			patcher->routeFunction(kern, reinterpret_cast<mach_vm_address_t>(codeSignValidatePageWrapper), true, true)
		);
		
		if (patcher->getError() != KernelPatcher::Error::NoError) {
			SYSLOG("user @ failed to hook _cs_validate_page");
			patcher->clearError();
			return false;
		}
    // 10.12 and newer
	} else if (patcher->clearError(),
               kern = patcher->solveSymbol(KernelPatcher::KernelID, "_cs_validate_range"),
               patcher->getError() == KernelPatcher::Error::NoError) {
        orgCodeSignValidateRangeWrapper = reinterpret_cast<t_codeSignValidateRangeWrapper>(
			patcher->routeFunction(kern, reinterpret_cast<mach_vm_address_t>(codeSignValidateRangeWrapper), true, true)
		);
        
        if (patcher->getError() != KernelPatcher::Error::NoError) {
            SYSLOG("user @ failed to hook _cs_validate_range");
            patcher->clearError();
            return false;
        }
    } else {
		SYSLOG("user @ failed to resolve _cs_validate function");
		patcher->clearError();
		return false;
	}
	
	orgCurrentMap = reinterpret_cast<t_currentMap>(patcher->solveSymbol(KernelPatcher::KernelID, "_current_map"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user @ failed to resolve _current_map");
		patcher->clearError();
		return false;
	}
	
	orgGetMapMin = reinterpret_cast<t_getMapMin>(patcher->solveSymbol(KernelPatcher::KernelID, "_get_map_min"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user @ failed to resolve _get_map_min");
		patcher->clearError();
		return false;
	}
	
	orgGetTaskMap = reinterpret_cast<t_getTaskMap>(patcher->solveSymbol(KernelPatcher::KernelID, "_get_task_map"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user @ failed to resolve _get_task_map");
		patcher->clearError();
		return false;
	}
	
	orgVmMapCheckProtection = reinterpret_cast<t_vmMapCheckProtection>(patcher->solveSymbol(KernelPatcher::KernelID, "_vm_map_check_protection"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user @ failed to resolve _vm_map_check_protection");
		patcher->clearError();
		return false;
	}
	
	orgVmMapReadUser = reinterpret_cast<t_vmMapReadUser>(patcher->solveSymbol(KernelPatcher::KernelID, "_vm_map_read_user"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user @ failed to resolve _vm_map_read_user");
		patcher->clearError();
		return false;
	}
	
	orgVmMapWriteUser = reinterpret_cast<t_vmMapWriteUser>(patcher->solveSymbol(KernelPatcher::KernelID, "_vm_map_write_user"));
	if (patcher->getError() != KernelPatcher::Error::NoError) {
		SYSLOG("user @ failed to resolve _vm_map_write_user");
		patcher->clearError();
		return false;
	}
	
	// On 10.12.1 b4 Apple decided not to let current_map point to the current process
	// For this reason we have to obtain the map with the other methods
	if (getKernelVersion() >= KernelVersion::Sierra) {
		kern = patcher->solveSymbol(KernelPatcher::KernelID, "_proc_exec_switch_task");
		
		if (patcher->getError() == KernelPatcher::Error::NoError) {
			orgProcExecSwitchTask = reinterpret_cast<t_procExecSwitchTask>(
				patcher->routeFunction(kern, reinterpret_cast<mach_vm_address_t>(procExecSwitchTask), true, true)
			);
			
			if (patcher->getError() != KernelPatcher::Error::NoError) {
				SYSLOG("user @ failed to hook _proc_exec_switch_task");
				patcher->clearError();
				return false;
			}
			
		} else {
			DBGLOG("user @ failed to resolve _proc_exec_switch_task");
			patcher->clearError();
			// This is not an error, early 10.12 versions have no such function
		}
	}
	
	if (patchDyldSharedCache) {
		kern = patcher->solveSymbol(KernelPatcher::KernelID, "_vm_shared_region_map_file");
		
		if (patcher->getError() == KernelPatcher::Error::NoError) {
			orgVmSharedRegionMapFile = reinterpret_cast<t_vmSharedRegionMapFile>(
				patcher->routeFunction(kern, reinterpret_cast<mach_vm_address_t>(vmSharedRegionMapFile), true, true)
			);
			
			if (patcher->getError() != KernelPatcher::Error::NoError) {
				SYSLOG("user @ failed to hook _vm_shared_region_map_file");
				patcher->clearError();
				return false;
			}
			
		} else {
			SYSLOG("user @ failed to resolve _vm_shared_region_map_file");
			patcher->clearError();
			return false;
		}
		
		kern = patcher->solveSymbol(KernelPatcher::KernelID, "_vm_shared_region_slide");
		
		if (patcher->getError() == KernelPatcher::Error::NoError) {
			orgVmSharedRegionSlide = reinterpret_cast<t_vmSharedRegionSlide>(
				patcher->routeFunction(kern, reinterpret_cast<mach_vm_address_t>(vmSharedRegionSlide), true, true)
			);
			
			if (patcher->getError() != KernelPatcher::Error::NoError) {
				SYSLOG("user @ failed to hook _vm_shared_region_slide");
				patcher->clearError();
				return false;
			}
			
		} else {
			SYSLOG("user @ failed to resolve _vm_shared_region_slide");
			patcher->clearError();
			return false;
		}
	}

	return true;
}
