//
//  kern_mach.cpp
//  KernelCommon
//
//  Certain parts of code are the subject of
//   copyright © 2011, 2012, 2013, 2014 fG!, reverser@put.as - http://reverse.put.as
//  Copyright © 2016 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_compat.hpp>
#include <PrivateHeaders/kern_config.hpp>
#include <Headers/kern_mach.hpp>
#ifdef LILU_COMPRESSION_SUPPORT
#include <Headers/kern_compression.hpp>
#endif /* LILU_COMPRESSION_SUPPORT */
#include <Headers/kern_file.hpp>
#include <Headers/kern_util.hpp>

#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/disk.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/vm_param.h>
#include <i386/proc_reg.h>
#include <kern/thread.h>
#include <libkern/c++/OSUnserialize.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSNumber.h>

kern_return_t MachInfo::init(const char * const paths[], size_t num, MachInfo *prelink, bool fsfallback) {
	kern_return_t error = KERN_FAILURE;
	
	allow_decompress = config.allowDecompress;

	// Check if we have a proper credential, prevents a race-condition panic on 10.11.4 Beta
	// When calling kauth_cred_get() for the current_thread.
	// This probably wants a better solution...
	if (!kernproc || !current_thread() || !vfs_context_current() || !vfs_context_ucred(vfs_context_current())) {
		SYSLOG("mach", "current context has no credential, it's too early");
		return error;
	}

	// Attempt to load directly from the filesystem
	(void)fsfallback;

	//FIXME: There still is a chance of booting with outdated prelink cache, so we cannot optimise it currently.
	// We are fine to detect prelinked usage (#27), but prelinked may not contain certain kexts and even more
	// it may contain different kexts (#30). In theory we should be able to compare _PrelinkInterfaceUUID with
	// LC_UUID like OSKext::registerIdentifier does, but this would overcomplicate the logic with no practical
	// performance gain.
	// For this reason we always try to read the kext from the filesystem and if we failed, then we fallback
	// to prelinked. This does not solve the main problem of distinguishing kexts, but the only practical cases
	// of our failure are missing kexts in both places and AirPort drivers in installer/recovery only present
	// in prelinked. For this reason we are fine.
	error = initFromFileSystem(paths, num);

	// Attempt to get linkedit from prelink
	if (!linkedit_buf)
		error = initFromPrelinked(prelink);

	return error;
}

void MachInfo::deinit() {
	freeFileBufferResources();

	if (linkedit_buf) {
		Buffer::deleter(linkedit_buf);
		linkedit_buf = nullptr;
	}
}

kern_return_t MachInfo::initFromPrelinked(MachInfo *prelink) {
	kern_return_t error = KERN_FAILURE;

	// Attempt to get linkedit from prelink
	if (prelink && objectId) {
		bool missing = false;
		file_buf = prelink->findImage(objectId, file_buf_size, prelink_vmaddr, missing);

		if (file_buf && file_buf_size >= HeaderSize) {
			processMachHeader(file_buf);
			if (linkedit_fileoff && symboltable_fileoff) {
				// read linkedit from prelink
				error = readLinkedit(NULLVP, nullptr);
				if (error == KERN_SUCCESS) {
					// for prelinked kexts assume that we have slide (this is true for modern os)
					prelink_slid = true;
				} else {
					SYSLOG("mach", "could not read the linkedit segment from prelink");
				}
			} else {
				SYSLOG("mach", "couldn't find the necessary mach segments or sections in prelink (linkedit %llX, sym %X)",
					   linkedit_fileoff, symboltable_fileoff);
			}
		} else {
			DBGLOG("mach", "unable to load missing %d image %s from prelink", missing, objectId);
		}

		file_buf = nullptr;
	}

	return error;
}

kern_return_t MachInfo::initFromFileSystem(const char * const paths[], size_t num) {
	kern_return_t error = KERN_FAILURE;

	// Allocate some data for header
	auto machHeader = Buffer::create<uint8_t>(HeaderSize);
	if (!machHeader) {
		SYSLOG("mach", "can't allocate header memory.");
		return error;
	}

	vnode_t vnode = NULLVP;
	vfs_context_t ctxt = vfs_context_create(nullptr);
	bool found = false;
	// Starting with 10.10 macOS supports kcsuffix that may be appended to processes, kernels, and kexts
	char suffix[32];
	size_t suffixnum = getKernelVersion() >= KernelVersion::Yosemite && PE_parse_boot_argn("kcsuffix", suffix, sizeof(suffix));

	for (size_t i = 0; i < num && !found; i++) {
		auto pathlen = static_cast<uint32_t>(strlen(paths[i]));
		if (pathlen == 0 || pathlen >= PATH_MAX) {
			SYSLOG("mach", "invalid path for mach info %s", paths[i]);
			continue;
		}

		for (size_t j = 0; j <= suffixnum; j++) {
			auto path = paths[i];
			char tmppath[PATH_MAX];
			// Prefer the suffixed version
			if (suffixnum - j > 0) {
				snprintf(tmppath, sizeof(tmppath), "%s.%s", path, suffix);
				path = tmppath;
			}

			vnode = NULLVP;
			errno_t err = vnode_lookup(path, 0, &vnode, ctxt);
			if (!err) {
				DBGLOG("mach", "readMachHeader for %s", path);
				kern_return_t readError = readMachHeader(machHeader, vnode, ctxt);
				if (readError == KERN_SUCCESS && (!isKernel || (isKernel && isCurrentKernel(machHeader)))) {
					DBGLOG("mach", "found executable at path: %s", path);
					found = true;
					break;
				}

				vnode_put(vnode);
			} else {
				DBGLOG("mach", "vnode_lookup failed for %s with %d", path, err);
			}
		}
	}

	if (!found) {
		DBGLOG("mach", "couldn't find a suitable executable");
		vfs_context_rele(ctxt);
		Buffer::deleter(machHeader);
		return error;
	}

	processMachHeader(machHeader);
	if (linkedit_fileoff && symboltable_fileoff) {
		// read linkedit from filesystem
		error = readLinkedit(vnode, ctxt);
		if (error != KERN_SUCCESS)
			SYSLOG("mach", "could not read the linkedit segment");
	} else {
		SYSLOG("mach", "couldn't find the necessary mach segments or sections (linkedit %llX, sym %X)",
			   linkedit_fileoff, symboltable_fileoff);
	}

	vnode_put(vnode);
	vfs_context_rele(ctxt);
	// drop the iocount due to vnode_lookup()
	// we must do this or the machine gets stuck on shutdown/reboot

	Buffer::deleter(machHeader);

	return error;
}

mach_vm_address_t MachInfo::findKernelBase() {
	auto tmp = reinterpret_cast<mach_vm_address_t>(IOLog);

	// Align the address
	tmp &= ~(KASLRAlignment - 1);

	// Search backwards for the kernel base address (mach-o header)
	while (true) {
		if (*reinterpret_cast<uint32_t *>(tmp) == MH_MAGIC_64) {
			// make sure it's the header and not some reference to the MAGIC number
			auto segmentCommand = reinterpret_cast<segment_command_64 *>(tmp + sizeof(mach_header_64));
			if (!strncmp(segmentCommand->segname, "__TEXT", strlen("__TEXT"))) {
				DBGLOG("mach", "found kernel mach-o header address at %llx", tmp);
				return tmp;
			}
		}

		tmp -= KASLRAlignment;
	}
	return 0;
}

bool MachInfo::setInterrupts(bool enable) {
	unsigned long flags;
	
	if (enable)
		asm volatile("pushf; pop %0; sti" : "=r"(flags));
	else
		asm volatile("pushf; pop %0; cli" : "=r"(flags));
	
	return static_cast<bool>(flags & EFL_IF) != enable;
}

kern_return_t MachInfo::setKernelWriting(bool enable, IOSimpleLock *lock) {
	static bool interruptsDisabled = false;
	
	kern_return_t res = KERN_SUCCESS;
	
	if (enable) {
		// Disable preemption
		if (lock) IOSimpleLockLock(lock);
		
		// Disable interrupts
		interruptsDisabled = !setInterrupts(false);
	}
	
	if (setWPBit(!enable) != KERN_SUCCESS) {
		SYSLOG("mach", "failed to set wp bit to %d", !enable);
		enable = false;
		res = KERN_FAILURE;
	}
	
	if (!enable) {
		// Enable interrupts if they were on previously
		if (!interruptsDisabled) setInterrupts(true);
		
		// Enable preemption
		if (lock) IOSimpleLockUnlock(lock);
	}
	
	return res;
}

mach_vm_address_t MachInfo::solveSymbol(const char *symbol) {
	if (!linkedit_buf) {
		SYSLOG("mach", "no loaded linkedit buffer found");
		return 0;
	}
	
	if (!symboltable_fileoff) {
		SYSLOG("mach", "no symtable offsets found");
		return 0;
	}
	
	if (!kaslr_slide_set) {
		SYSLOG("mach", "no slide is present");
		return 0;
	}
	
	// symbols and strings offsets into LINKEDIT
	// we just read the __LINKEDIT but fileoff values are relative to the full Mach-O
	// subtract the base of LINKEDIT to fix the value into our buffer

	auto nlist = reinterpret_cast<nlist_64 *>(linkedit_buf + (symboltable_fileoff - linkedit_fileoff));
	auto strlist = reinterpret_cast<char *>(linkedit_buf + (stringtable_fileoff - linkedit_fileoff));
	auto endaddr = linkedit_buf + linkedit_size;

	if (reinterpret_cast<uint8_t *>(nlist) >= linkedit_buf && reinterpret_cast<uint8_t *>(strlist) >= linkedit_buf) {
		auto symlen = strlen(symbol) + 1;
		for (uint32_t i = 0; i < symboltable_nr_symbols; i++, nlist++) {
			if (reinterpret_cast<uint8_t *>(nlist+1) <= endaddr) {
				// get the pointer to the symbol entry and extract its symbol string
				auto symbolStr = reinterpret_cast<char *>(strlist + nlist->n_un.n_strx);
				// find if symbol matches
				if (reinterpret_cast<uint8_t *>(symbolStr + symlen) <= endaddr && !strncmp(symbol, symbolStr, symlen)) {
					DBGLOG("mach", "found symbol %s at 0x%llx (non-aslr 0x%llx)", symbol, nlist->n_value + kaslr_slide, nlist->n_value);
					// the symbol values are without kernel ASLR so we need to add it
					return nlist->n_value + kaslr_slide;
				}
			} else {
				SYSLOG("mach", "symbol at %u out of %u exceeds linkedit bounds", i, symboltable_nr_symbols);
				break;
			}
		}
	} else {
		SYSLOG("mach", "invalid symbol/string tables point behind linkedit");
	}

	return 0;
}

kern_return_t MachInfo::readMachHeader(uint8_t *buffer, vnode_t vnode, vfs_context_t ctxt, off_t off) {
	int error = FileIO::readFileData(buffer, off, HeaderSize, vnode, ctxt);
	if (error) {
		SYSLOG("mach", "mach header read failed with %d error", error);
		return KERN_FAILURE;
	}
	
	while (1) {
		auto magic = *reinterpret_cast<uint32_t *>(buffer);
		DBGLOG("mach", "readMachHeader got magic %08X", magic);

		switch (magic) {
			case MH_MAGIC_64:
				fat_offset = off;
				return KERN_SUCCESS;
			case FAT_MAGIC: {
				uint32_t num = reinterpret_cast<fat_header *>(buffer)->nfat_arch;
				for (uint32_t i = 0; i < num; i++) {
					auto arch = reinterpret_cast<fat_arch *>(buffer + i*sizeof(fat_arch) + sizeof(fat_header));
					if (arch->cputype == CPU_TYPE_X86_64)
						return readMachHeader(buffer, vnode, ctxt, arch->offset);
				}
				SYSLOG("mach", "magic failed to find a x86_64 mach");
				return KERN_FAILURE;
			}
			case FAT_CIGAM: {
				uint32_t num = OSSwapInt32(reinterpret_cast<fat_header *>(buffer)->nfat_arch);
				for (uint32_t i = 0; i < num; i++) {
					auto arch = reinterpret_cast<fat_arch *>(buffer + i*sizeof(fat_arch) + sizeof(fat_header));
					if (OSSwapInt32(arch->cputype) == CPU_TYPE_X86_64)
						return readMachHeader(buffer, vnode, ctxt, OSSwapInt32(arch->offset));
				}
				SYSLOG("mach", "cigam failed to find a x86_64 mach");
				return KERN_FAILURE;
			}
#ifdef LILU_COMPRESSION_SUPPORT
			case Compression::Magic: { // comp
				if (allow_decompress) {
					auto header = reinterpret_cast<Compression::Header *>(buffer);
					auto compressedBuf = Buffer::create<uint8_t>(OSSwapInt32(header->compressed));
					if (!compressedBuf) {
						SYSLOG("mach", "failed to allocate memory for reading mach binary");
					} else if (FileIO::readFileData(compressedBuf, off+sizeof(Compression::Header), OSSwapInt32(header->compressed),
													vnode, ctxt) != KERN_SUCCESS) {
						SYSLOG("mach", "failed to read compressed binary");
					} else {
						uint32_t comp = OSSwapInt32(header->compressed);
						uint32_t dec  = OSSwapInt32(header->decompressed);
						DBGLOG("mach", "decompressing %d bytes (estimated %u bytes) with %X compression mode", comp, dec, header->compression);

						if (header->decompressed > HeaderSize) {
							if (file_buf) Buffer::deleter(file_buf);
							file_buf = Compression::decompress(header->compression, dec, compressedBuf, comp);
							// Try again
							if (file_buf) {
								file_buf_size = dec;
								lilu_os_memcpy(buffer, file_buf, HeaderSize);
								Buffer::deleter(compressedBuf);
								continue;
							}
						} else {
							SYSLOG("mach", "decompression disallowed due to low out size %u", header->decompressed);
						}
					}

					Buffer::deleter(compressedBuf);
				} else {
					SYSLOG("mach", "decompression disallowed due to lowmem flag");
				}
				return KERN_FAILURE;
			}
#endif /* LILU_COMPRESSION_SUPPORT */
			default:
				SYSLOG("mach", "read mach has unsupported %X magic", magic);
				return KERN_FAILURE;
		}
	}
	
	return KERN_FAILURE;
}

kern_return_t MachInfo::readLinkedit(vnode_t vnode, vfs_context_t ctxt) {
	// we know the location of linkedit and offsets into symbols and their strings
	// now we need to read linkedit into a buffer so we can process it later
	// __LINKEDIT total size is around 1MB
	// we should free this buffer later when we don't need anymore to solve symbols
	linkedit_buf = Buffer::create<uint8_t>(linkedit_size);
	if (!linkedit_buf) {
		SYSLOG("mach", "Could not allocate enough memory (%lld) for __LINKEDIT segment", linkedit_size);
		return KERN_FAILURE;
	}

#ifdef LILU_COMPRESSION_SUPPORT
	if (file_buf) {
		if (file_buf_size >= linkedit_size && file_buf_size - linkedit_size >= linkedit_fileoff) {
			lilu_os_memcpy(linkedit_buf, file_buf + linkedit_fileoff, linkedit_size);
			return KERN_SUCCESS;
		}
		SYSLOG("mach", "requested linkedit (%llu %llu) exceeds file buf size (%u)", linkedit_fileoff, linkedit_size, file_buf_size);
	} else
#endif /* LILU_COMPRESSION_SUPPORT */
	{
		int error = FileIO::readFileData(linkedit_buf, fat_offset + linkedit_fileoff, linkedit_size, vnode, ctxt);
		if (!error)
			return KERN_SUCCESS;
		SYSLOG("mach", "linkedit read failed with %d error", error);
	}

	Buffer::deleter(linkedit_buf);
	linkedit_buf = nullptr;

	return KERN_FAILURE;
}

void MachInfo::findSectionBounds(void *ptr, vm_address_t &vmsegment, vm_address_t &vmsection, void *&sectionptr, size_t &size, const char *segmentName, const char *sectionName, cpu_type_t cpu) {
	vmsegment = vmsection = 0;
	sectionptr = 0;
	size = 0;
	
	auto header = static_cast<mach_header *>(ptr);
	auto cmd = static_cast<load_command *>(ptr);
	
	if (header->magic == MH_MAGIC_64) {
		reinterpret_cast<uintptr_t &>(cmd) += sizeof(mach_header_64);
	} else if (header->magic == MH_MAGIC) {
		reinterpret_cast<uintptr_t &>(cmd) += sizeof(mach_header);
	} else if (header->magic == FAT_CIGAM){
		fat_header *fheader = static_cast<fat_header *>(ptr);
		uint32_t num = OSSwapInt32(fheader->nfat_arch);
		fat_arch *farch = reinterpret_cast<fat_arch *>(reinterpret_cast<uintptr_t>(ptr) + sizeof(fat_header));
		for (size_t i = 0; i < num; i++, farch++) {
			if (OSSwapInt32(farch->cputype) ==  cpu) {
				findSectionBounds(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(ptr) + OSSwapInt32(farch->offset)), vmsegment, vmsection, sectionptr, size, segmentName, sectionName, cpu);
				break;
			}
		}
		return;
	}
	
	for (uint32_t no = 0; no < header->ncmds; no++) {
		if (cmd->cmd == LC_SEGMENT) {
			segment_command *scmd = reinterpret_cast<segment_command *>(cmd);
			if (!strcmp(scmd->segname, segmentName)) {
				section *sect = reinterpret_cast<section *>(cmd);
				reinterpret_cast<uintptr_t &>(sect) += sizeof(segment_command);
				
				for (uint32_t sno = 0; sno < scmd->nsects; sno++) {
					if (!strcmp(sect->sectname, sectionName)) {
						vmsegment = scmd->vmaddr;
						vmsection = sect->addr;
						sectionptr = reinterpret_cast<void *>(sect->offset+reinterpret_cast<uintptr_t>(ptr));
						size = static_cast<size_t>(sect->size);
						DBGLOG("mach", "found section %lu size %lu in segment %lu\n", vmsection, vmsegment, size);
						return;
					}
					
					sect++;
				}
			}
		} else if (cmd->cmd == LC_SEGMENT_64) {
			segment_command_64 *scmd = reinterpret_cast<segment_command_64 *>(cmd);
			if (!strcmp(scmd->segname, segmentName)) {
				section_64 *sect = reinterpret_cast<section_64 *>(cmd);
				reinterpret_cast<uintptr_t &>(sect) += sizeof(segment_command_64);
				
				for (uint32_t sno = 0; sno < scmd->nsects; sno++) {
					if (!strcmp(sect->sectname, sectionName)) {
						vmsegment = scmd->vmaddr;
						vmsection = sect->addr;
						sectionptr = reinterpret_cast<void *>(sect->offset+reinterpret_cast<uintptr_t>(ptr));
						size = static_cast<size_t>(sect->size);
						DBGLOG("mach", "found section %lu size %lu in segment %lu\n", vmsection, vmsegment, size);
						return;
					}
					
					sect++;
				}
			}
		}
		
		reinterpret_cast<uintptr_t &>(cmd) += cmd->cmdsize;
	}
}

void MachInfo::freeFileBufferResources() {
	if (file_buf) {
		Buffer::deleter(file_buf);
		file_buf = nullptr;
	}

	if (prelink_dict) {
		prelink_dict->release();
		prelink_dict = nullptr;
	}
}

void MachInfo::processMachHeader(void *header) {
	mach_header_64 *mh = static_cast<mach_header_64 *>(header);
	size_t headerSize = sizeof(mach_header_64);
	
	// point to the first load command
	auto addr    = static_cast<uint8_t *>(header) + headerSize;
	auto endaddr = static_cast<uint8_t *>(header) + HeaderSize;
	// iterate over all load cmds and retrieve required info to solve symbols
	// __LINKEDIT location and symbol/string table location
	for (uint32_t i = 0; i < mh->ncmds; i++) {
		auto loadCmd = reinterpret_cast<load_command *>(addr);

		if (addr + sizeof(load_command) > endaddr || addr + loadCmd->cmdsize > endaddr) {
			SYSLOG("mach", "header command %u of info %s exceeds header size", i, objectId ? objectId : "(null)");
			return;
		}

		if (loadCmd->cmd == LC_SEGMENT_64) {
			segment_command_64 *segCmd = reinterpret_cast<segment_command_64 *>(loadCmd);
			// use this one to retrieve the original vm address of __TEXT so we can compute kernel aslr slide
			if (strncmp(segCmd->segname, "__TEXT", strlen("__TEXT")) == 0) {
				DBGLOG("mach", "header processing found TEXT");
				disk_text_addr = segCmd->vmaddr;
			} else if (strncmp(segCmd->segname, "__LINKEDIT", strlen("__LINKEDIT")) == 0) {
				DBGLOG("mach", "header processing found LINKEDIT");
				linkedit_fileoff = segCmd->fileoff;
				linkedit_size = segCmd->filesize;
			}
		}
		// table information available at LC_SYMTAB command
		else if (loadCmd->cmd == LC_SYMTAB) {
			DBGLOG("mach", "header processing found SYMTAB");
			auto symtab_cmd = reinterpret_cast<symtab_command *>(loadCmd);
			symboltable_fileoff = symtab_cmd->symoff;
			symboltable_nr_symbols = symtab_cmd->nsyms;
			stringtable_fileoff = symtab_cmd->stroff;
		}
		addr += loadCmd->cmdsize;
	}
}

void MachInfo::updatePrelinkInfo() {
	if (!prelink_dict && isKernel && file_buf) {
		vm_address_t tmpSeg, tmpSect;
		void *tmpSectPtr;
		size_t tmpSectSize;
		findSectionBounds(file_buf, tmpSeg, tmpSect, tmpSectPtr, tmpSectSize, "__PRELINK_INFO", "__info");
		auto startoff = tmpSectSize && static_cast<uint8_t *>(tmpSectPtr) >= file_buf ?
		                static_cast<uint8_t *>(tmpSectPtr) - file_buf : file_buf_size;
		if (tmpSectSize > 0 && file_buf_size > startoff && file_buf_size - startoff >= tmpSectSize) {
			auto xmlData = static_cast<const char *>(tmpSectPtr);
			auto objData = xmlData[tmpSectSize-1] == '\0' ? OSUnserializeXML(xmlData, nullptr) : nullptr;
			prelink_dict = OSDynamicCast(OSDictionary, objData);
			if (prelink_dict) {
				findSectionBounds(file_buf, tmpSeg, tmpSect, tmpSectPtr, tmpSectSize, "__PRELINK_TEXT", "__text");
				if (tmpSectSize){
					prelink_addr = static_cast<uint8_t *>(tmpSectPtr);
					prelink_vmaddr = tmpSect;
					// If _PrelinkLinkKASLROffsets is set, then addresses are already slid
					prelink_slid = prelink_dict->getObject("_PrelinkLinkKASLROffsets");
				} else {
					SYSLOG("mach", "unable to get prelink offset");
					prelink_dict->release();
					prelink_dict = nullptr;
				}
			} else if (objData) {
				SYSLOG("mach", "unable to parse prelink info section");
				objData->release();
			} else {
				SYSLOG("mach", "unable to deserialize prelink info section");
			}
		} else {
			SYSLOG("mach", "unable to find prelink info section");
		}
	} else {
		//DBGLOG("mach", "dict present %d kernel %d buf present %d", prelink_dict != nullptr, isKernel, file_buf != nullptr);
	}
}

uint8_t *MachInfo::findImage(const char *identifier, uint32_t &imageSize, mach_vm_address_t &slide, bool &missing) {
	updatePrelinkInfo();

	//DBGLOG("mach", "looking up %s kext in prelink", identifier);

	if (prelink_dict) {
		static OSArray *imageArr = nullptr;
		static uint32_t imageNum = 0;

		if (!imageArr) imageArr = OSDynamicCast(OSArray, prelink_dict->getObject("_PrelinkInfoDictionary"));
		if (imageArr) {
			if (!imageNum) imageNum = imageArr->getCount();

			for (uint32_t i = 0; i < imageNum; i++) {
				auto image = OSDynamicCast(OSDictionary, imageArr->getObject(i));
				if (image) {
					auto imageID = OSDynamicCast(OSString, image->getObject("CFBundleIdentifier"));
					if (imageID && imageID->isEqualTo(identifier)) {
						DBGLOG("mach", "found kext %s at %u of prelink", identifier, i);
						auto saddr = OSDynamicCast(OSNumber, image->getObject("_PrelinkExecutableSourceAddr"));
						auto laddr = OSDynamicCast(OSNumber, image->getObject("_PrelinkExecutableLoadAddr"));
						auto size = OSDynamicCast(OSNumber, image->getObject("_PrelinkExecutableSize"));

						if (saddr && laddr && size) {
							imageSize = size->unsigned32BitValue();
							uint8_t *imageaddr = (saddr->unsigned64BitValue() - prelink_vmaddr) + prelink_addr;
							auto startoff = imageaddr >= file_buf ? imageaddr - file_buf : file_buf_size;
							if (file_buf_size > startoff && file_buf_size - startoff >= imageSize) {
								// Normally all the kexts are off by kaslr slide unless already slid
								slide = !prelink_slid ? kaslr_slide : 0;
								return imageaddr;
							} else {
								SYSLOG("mach", "invalid addresses of kext %s at %u of %u prelink", identifier, i, imageNum);
							}
						}

						SYSLOG("mach", "unable to obtain addr and size for %s at %u of %u prelink", identifier, i, imageNum);
						return nullptr;
					} else {
						//DBGLOG("mach", "prelink %u of %u contains %s id", i, imageNum, imageID ? imageID->getCStringNoCopy() : "(null)");
					}
				} else {
					SYSLOG("mach", "prelink %u of %u is not a dictionary", i, imageNum);
				}
			}

			// We optimise our boot process by ignoring unused kexts
			missing = true;
		} else {
			SYSLOG("mach", "unable to find prelink info array");
		}
	}

	return nullptr;
}

kern_return_t MachInfo::getRunningAddresses(mach_vm_address_t slide, size_t size, bool force) {
	if (force) {
		kaslr_slide_set = false;
		running_mh = nullptr;
		running_text_addr = 0;
		memory_size = 0;
	}
	
	if (kaslr_slide_set) return KERN_SUCCESS;
	
	if (size > 0)
		memory_size = size;
	
	// We are meant to know the base address of kexts
	mach_vm_address_t base = slide ? slide : findKernelBase();
	if (base != 0) {
		// get the vm address of __TEXT segment
		auto mh = reinterpret_cast<mach_header_64 *>(base);
		auto headerSize = sizeof(mach_header_64);
		
		load_command *loadCmd;
		auto addr = reinterpret_cast<uint8_t *>(base) + headerSize;
		auto endaddr = reinterpret_cast<uint8_t *>(base) + HeaderSize;
		for (uint32_t i = 0; i < mh->ncmds; i++) {
			loadCmd = reinterpret_cast<load_command *>(addr);

			if (addr + sizeof(load_command) > endaddr || addr + loadCmd->cmdsize > endaddr) {
				SYSLOG("mach", "running command %u of info %s exceeds header size", i, objectId ? objectId : "(null)");
				return KERN_FAILURE;
			}

			if (loadCmd->cmd == LC_SEGMENT_64) {
				segment_command_64 *segCmd = reinterpret_cast<segment_command_64 *>(loadCmd);
				if (strncmp(segCmd->segname, "__TEXT", 16) == 0) {
					running_text_addr = segCmd->vmaddr;
					running_mh = mh;
					break;
				}
			}
			addr += loadCmd->cmdsize;
		}
	}
	
	// compute kaslr slide
	if (running_text_addr && running_mh) {
		if (!slide) // This is kernel image
			kaslr_slide = running_text_addr - disk_text_addr;
		else // This is kext image
			kaslr_slide = prelink_slid ? prelink_vmaddr : slide;
		kaslr_slide_set = true;
		
		DBGLOG("mach", "aslr/load slide is 0x%llx", kaslr_slide);
	} else {
		SYSLOG("mach", "couldn't find the running addresses");
		return KERN_FAILURE;
	}
	
	return KERN_SUCCESS;
}

kern_return_t MachInfo::setRunningAddresses(mach_vm_address_t slide, size_t size) {
	memory_size = size;
	kaslr_slide = slide;
	kaslr_slide_set = true;
	return KERN_SUCCESS;
}

void MachInfo::getRunningPosition(uint8_t * &header, size_t &size) {
	header = reinterpret_cast<uint8_t *>(running_mh);
	size = memory_size > 0 ? memory_size : HeaderSize;
	DBGLOG("mach", "getRunningPosition %p of memory %lu size", header, size);
}

uint64_t *MachInfo::getUUID(void *header) {
	if (!header) return nullptr;
	
	auto mh = static_cast<mach_header_64 *>(header);
	size_t size = sizeof(mach_header_64);
	
	auto *addr = static_cast<uint8_t *>(header) + size;
	auto endaddr = static_cast<uint8_t *>(header) + HeaderSize;
	for (uint32_t i = 0; i < mh->ncmds; i++) {
		auto loadCmd = reinterpret_cast<load_command *>(addr);

		if (addr + sizeof(load_command) > endaddr || addr + loadCmd->cmdsize > endaddr) {
			SYSLOG("mach", "uuid command %u of info %s exceeds header size", i, objectId ? objectId : "(null)");
			return nullptr;
		}

		if (loadCmd->cmd == LC_UUID)
			return reinterpret_cast<uint64_t *>((reinterpret_cast<uuid_command *>(loadCmd))->uuid);
		
		addr += loadCmd->cmdsize;
	}
	
	return nullptr;
}

bool MachInfo::isCurrentKernel(void *kernelHeader) {
	mach_vm_address_t kernelBase = findKernelBase();
	
	uint64_t *uuid1 = getUUID(kernelHeader);
	uint64_t *uuid2 = getUUID(reinterpret_cast<void *>(kernelBase));

	return uuid1 && uuid2 && uuid1[0] == uuid2[0] && uuid1[1] == uuid2[1];
}

kern_return_t MachInfo::setWPBit(bool enable) {
	static bool writeProtectionDisabled = false;
	
	uintptr_t cr0 = get_cr0();

	if (enable && !writeProtectionDisabled) {
		// Set the WP bit
		cr0 = cr0 | CR0_WP;
		set_cr0(cr0);
		return (get_cr0() & CR0_WP) != 0 ? KERN_SUCCESS : KERN_FAILURE;
	}
	
	if (!enable) {
		// Remove the WP bit
		writeProtectionDisabled = (cr0 & CR0_WP) == 0;
		if (!writeProtectionDisabled) {
			cr0 = cr0 & ~CR0_WP;
			set_cr0(cr0);
		}
	}
	
	return (get_cr0() & CR0_WP) == 0 ? KERN_SUCCESS : KERN_FAILURE;
}
