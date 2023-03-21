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
#include <libkern/c++/OSSerialize.h>

kern_return_t MachInfo::init(const char * const paths[], size_t num, MachInfo *prelink, bool fsfallback) {
	kern_return_t error = KERN_FAILURE;

	allow_decompress = ADDPR(config).allowDecompress;

	// Attempt to load directly from the filesystem
	(void)fsfallback;

	// Use in-memory init on modern operating systems when launched in KC mode.
	error = initFromMemory();
	if (kernel_collection && strstr(paths[0], ".kext") != NULL)
		return error;

#if defined (__x86_64__)
	// Check if we have a proper credential, prevents a race-condition panic on 10.11.4 Beta
	// When calling kauth_cred_get() for the current_thread.
	//TODO: Try to find a better solution...
	if (!kernproc || !current_thread() || !vfs_context_current() || !vfs_context_ucred(vfs_context_current())) {
		SYSLOG("mach", "current context has no credential, it's too early %d %d %d %d",
			   !kernproc, !current_thread(), !vfs_context_current(), !vfs_context_ucred(vfs_context_current()));
		return error;
	}
#endif

	//TODO: There still is a chance of booting with outdated prelink cache, so we cannot optimise it currently.
	// We are fine to detect prelinked usage (#27), but prelinked may not contain certain kexts and even more
	// it may contain different kexts (#30). In theory we should be able to compare _PrelinkInterfaceUUID with
	// LC_UUID like OSKext::registerIdentifier does, but this would overcomplicate the logic with no practical
	// performance gain.
	// For this reason we always try to read the kext from the filesystem and if we failed, then we fallback
	// to prelinked. This does not solve the main problem of distinguishing kexts, but the only practical cases
	// of our failure are missing kexts in both places and AirPort drivers in installer/recovery only present
	// in prelinked. For this reason we are fine.
	if (!sym_buf)
		error = initFromFileSystem(paths, num);

	// Attempt to get linkedit from prelink
	if (!sym_buf)
		error = initFromPrelinked(prelink);

	return error;
}

void MachInfo::deinit() {
	freeFileBufferResources();

	if (sym_buf) {
		if (!sym_buf_ro)
			Buffer::deleter(sym_buf);
		sym_buf = nullptr;
	}
}

kern_return_t MachInfo::initFromKCBuffer(uint8_t * kcBuf, uint32_t bufSize, uint32_t origKCSize) {
	kernel_collection = true;
	file_buf = kcBuf;
	file_buf_size = bufSize;
	file_buf_free_start = origKCSize;

	updatePrelinkInfo();
	return KERN_SUCCESS;
}

kern_return_t MachInfo::overwritePrelinkInfo() {
	vm_address_t tmpSeg, tmpSect;
	void *tmpSectPtr;
	size_t tmpSectSize;
	void *tmpSectionCmdPtr;
	findSectionBounds(file_buf, file_buf_size, tmpSeg, tmpSect, tmpSectPtr, tmpSectSize, tmpSectionCmdPtr, "__PRELINK_INFO", "__info");

	OSSerialize *newPrelinkInfo = OSSerialize::withCapacity(1024 * 1024);
	prelink_dict->serialize(newPrelinkInfo);
	uint32_t infoLength = newPrelinkInfo->getLength();

	if (file_buf_free_start + infoLength >= file_buf_size) {
		SYSLOG("mach", "overwritePrelinkInfo: Ran out of free space");
		return KERN_FAILURE;
	}

	memcpy(file_buf + file_buf_free_start, newPrelinkInfo->text(), infoLength);
	section_64 *sectionCmdPtr = (section_64*)tmpSectionCmdPtr;
	sectionCmdPtr->offset = file_buf_free_start;
	sectionCmdPtr->size = infoLength;
	file_buf_free_start += infoLength;
	DBGLOG("mach", "overwritePrelinkInfo: Wrote %d bytes of prelink info", infoLength);
	return KERN_SUCCESS;
}

kern_return_t MachInfo::excludeKextFromKC(const char * kextName) {
	DBGLOG("mach", "excludeKextFromKC: Blocking %s", kextName);

	// Remove the related LC_FILESET_ENTRY command
	auto header = (mach_header*)(file_buf);
	auto orgCmd = (load_command*)(file_buf);

	if (header->magic == MH_MAGIC_64) {
		reinterpret_cast<uintptr_t &>(orgCmd) += sizeof(mach_header_64);
	} else if (header->magic == MH_MAGIC) {
		reinterpret_cast<uintptr_t &>(orgCmd) += sizeof(mach_header);
	}

	uint8_t *endaddr = file_buf + file_buf_free_start;
	auto dstCmd = orgCmd;
	for (uint32_t no = 0; no < header->ncmds; no++) {
		if (!isAligned(orgCmd)) {
			SYSLOG("mach", "excludeKextFromKC: Invalid command %u position for section lookup", no);
			return;
		}

		if (reinterpret_cast<uint8_t *>(orgCmd) + sizeof(load_command) > endaddr || reinterpret_cast<uint8_t *>(orgCmd) + orgCmd->cmdsize > endaddr) {
			SYSLOG("mach", "excludeKextFromKC: Header command %u exceeds header size for section lookup", no);
			return;
		}

		// LC_FILESET_ENTRY
		if (orgCmd->cmd == 0x80000035) {
			auto fcmd = reinterpret_cast<fileset_entry_command *>(orgCmd);
			// If this the kext we are trying to exclude
			if (!strncmp(kextName, (char*)fcmd + fcmd->stringOffset, fcmd->commandSize - fcmd->stringOffset)) {
				SYSLOG("mach", "excludeKextFromKC: Skipping related LC_FILESET_ENTRY");
				goto skipCommand;
			}
		}

		if (orgCmd != dstCmd) {
			memcpy(dstCmd, orgCmd, orgCmd->cmdsize);
		}
		reinterpret_cast<uintptr_t &>(dstCmd) += orgCmd->cmdsize;

		skipCommand:
		reinterpret_cast<uintptr_t &>(orgCmd) += orgCmd->cmdsize;
	}

	if (orgCmd == dstCmd) {
		SYSLOG("mach", "excludeKextFromKC: Failed to locate related LC_FILESET_ENTRY");
		return KERN_FAILURE;
	}
	header->ncmds--;

	// Remove the kext from the prelink info
	uint32_t imageIndex;
	uint32_t imageSize;
	mach_vm_address_t slide;
	bool missing;
	uint8_t *imagePtr = findImage(kextName, imageIndex, imageSize, slide, missing);
	if (imagePtr == nullptr) return KERN_FAILURE;

	static OSArray *imageArr = nullptr;
	if (!imageArr) imageArr = OSDynamicCast(OSArray, prelink_dict->getObject("_PrelinkInfoDictionary"));
	if (!imageArr) return KERN_FAILURE;
	imageArr->removeObject(imageIndex);

	// Overwrite the kext image with zero
	memset(imagePtr, 0, imageSize);

	DBGLOG("mach", "excludeKextFromKC: %s is now blocked", kextName);
	return KERN_SUCCESS;
}

kern_return_t MachInfo::initFromMemory() {
	// Before 11.0 __LINKEDIT is dropped from memory unless keepsyms=1 argument is specified.
	// With 11.0 for all kernel collections (KC) __LINKEDIT is preserved for both kexts and kernels.
	// See OSKext::removeKextBootstrap and OSKext::jettisonLinkeditSegment.
	if (getKernelVersion() < KernelVersion::BigSur)
		return KERN_FAILURE;

	// We can still launch macOS 11 with prelinkedkernel, in which case memory init will not be available.
	findKernelBase();
	DBGLOG_COND(machType != MachType::Kext, "mach", "memory init mode - %d", kernel_collection);
	if (!kernel_collection)
		return KERN_FAILURE;

	return KERN_SUCCESS;
}

kern_return_t MachInfo::initFromPrelinked(MachInfo *prelink) {
	kern_return_t error = KERN_FAILURE;

	// Attempt to get linkedit from prelink
	if (prelink && objectId) {
		uint32_t tmpImageIndex;
		bool missing = false;
		file_buf = prelink->findImage(objectId, tmpImageIndex, file_buf_size, prelink_vmaddr, missing);

		if (file_buf && file_buf_size >= HeaderSize) {
			processMachHeader(file_buf);
			if (sym_fileoff && symboltable_fileoff) {
				if (loadUUID(file_buf)) {
					// read linkedit from prelink
					error = readSymbols(NULLVP, nullptr);
					if (error == KERN_SUCCESS) {
						// for prelinked kexts assume that we have slide (this is true for modern os)
						prelink_slid = true;
					} else {
						SYSLOG("mach", "could not read the linkedit segment from prelink");
					}
				} else {
					SYSLOG("mach", "failed to get prelinked uuid");
				}
			} else {
				SYSLOG("mach", "couldn't find the necessary mach segments or sections in prelink (linkedit %llX, sym %X)",
					   sym_fileoff, symboltable_fileoff);
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
	char suffix[32] {};
	size_t suffixnum = getKernelVersion() >= KernelVersion::Yosemite && lilu_get_boot_args("kcsuffix", suffix, sizeof(suffix)) ? 2 : 0;

	for (size_t i = 0; i < num && !found; i++) {
		auto pathlen = static_cast<uint32_t>(strlen(paths[i]));
		if (pathlen == 0 || pathlen >= PATH_MAX) {
			SYSLOG("mach", "invalid path for mach info %s", paths[i]);
			continue;
		}

		for (size_t j = 0; j <= suffixnum; j++) {
			auto path = paths[i];
			char tmppath[PATH_MAX];
			// Prefer the suffixed version if available and fallback to unsuffixed otherwise.
			if (j != suffixnum) {
				// Kexts(?) may use _ for suffixes e.g. IOPCIFamily_development.
				snprintf(tmppath, sizeof(tmppath), "%s%c%s", path, j == 0 ? '.' : '_', suffix);
				path = tmppath;
			}

			vnode = NULLVP;
			errno_t err = vnode_lookup(path, 0, &vnode, ctxt);
			if (!err) {
				DBGLOG("mach", "readMachHeader for %s", path);
				kern_return_t readError = readMachHeader(machHeader, vnode, ctxt);
				if (readError == KERN_SUCCESS && loadUUID(machHeader) &&
					(machType == MachType::Kext || isCurrentBinary())) {
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
	if (sym_fileoff && symboltable_fileoff) {
		// read symbols from filesystem
		error = readSymbols(vnode, ctxt);
		if (error != KERN_SUCCESS)
			SYSLOG("mach", "could not read symbols");
	} else {
		SYSLOG("mach", "couldn't find the necessary mach segments or sections (linkedit %llX, sym %X)",
			   sym_fileoff, symboltable_fileoff);
	}

	vnode_put(vnode);
	vfs_context_rele(ctxt);
	// drop the iocount due to vnode_lookup()
	// we must do this or the machine gets stuck on shutdown/reboot

	Buffer::deleter(machHeader);

	return error;
}

mach_vm_address_t MachInfo::findKernelBase() {
	static mach_vm_address_t m_kernel_base {0};
	static bool m_kernel_collection {false};

	if (m_kernel_base != 0) {
		kernel_collection = m_kernel_collection;
		return m_kernel_base;
	}

	// The function choice is completely random here, yet IOLog often has a low address.
	auto tmp = reinterpret_cast<mach_vm_address_t>(IOLog);

	// Align the address
	tmp &= ~(KASLRAlignment - 1);

	// Search backwards for the kernel base address (mach-o header)
	while (true) {
		auto mh = reinterpret_cast<mach_header_native *>(tmp);
		if (mh->magic == MachMagicNative) {
#if defined (__x86_64__)
			// make sure it's the header and not some reference to the MAGIC number.
			// 0xC is MH_FILESET, available exclusively in newer SDKs.
			if (getKernelVersion() >= KernelVersion::BigSur && mh->filetype == 0xC && mh->flags == 0 && mh->reserved == 0) {
				DBGLOG("mach", "found kernel nouveau mach-o header address at %llx", tmp);
				m_kernel_collection = kernel_collection = true;
				break;
			}
#endif
			
			// Search for __TEXT segment load command.
			// 10.5 and older have __PAGEZERO first and __TEXT second.
			bool foundHeader = false;
			size_t headerSize = sizeof(mach_header_native);

			// point to the first load command
			auto addr    = reinterpret_cast<uint8_t *>(tmp) + headerSize;
			auto endaddr = reinterpret_cast<uint8_t *>(tmp) + HeaderSize;

			for (uint32_t i = 0; i < mh->ncmds; i++) {
				auto loadCmd = reinterpret_cast<load_command *>(addr);
				if (!isAligned(loadCmd) || addr + sizeof(load_command) > endaddr || addr + loadCmd->cmdsize > endaddr) {
					break;
				}

				if (loadCmd->cmd == SegmentTypeNative) {
					auto segCmd = reinterpret_cast<segment_command_native *>(loadCmd);
					if (!strncmp(segCmd->segname, "__TEXT", sizeof(segCmd->segname))) {
						foundHeader = true;
						break;
					}
				}
				addr += loadCmd->cmdsize;
			}
			
			if (foundHeader) {
				DBGLOG("mach", "found kernel mach-o header address at %llx", tmp);
				break;
			}
		}

		// 10.5 and older require 4K granularity for searching
		tmp -= getKernelVersion() >= KernelVersion::SnowLeopard ? KASLRAlignment : PAGE_SIZE;
	}

	m_kernel_base = tmp;
	return tmp;
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
	if (!sym_buf) {
		SYSLOG("mach", "no loaded symbols buffer found");
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
	//
	// 32-bit MH_OBJECT does not have __LINKEDIT
	auto nlist = reinterpret_cast<nlist_native *>(sym_buf + (symboltable_fileoff - sym_fileoff));
	auto strlist = reinterpret_cast<char *>(sym_buf + (stringtable_fileoff - sym_fileoff));
	auto endaddr = sym_buf + sym_size;

	if (reinterpret_cast<uint8_t *>(nlist) >= sym_buf && reinterpret_cast<uint8_t *>(strlist) >= sym_buf) {
		auto symlen = strlen(symbol) + 1;
		for (uint32_t i = 0; i < symboltable_nr_symbols; i++, nlist++) {
			if (reinterpret_cast<uint8_t *>(nlist+1) <= endaddr) {
				// get the pointer to the symbol entry and extract its symbol string
				auto symbolStr = reinterpret_cast<char *>(strlist + nlist->n_un.n_strx);
				// find if symbol matches
				if (reinterpret_cast<uint8_t *>(symbolStr + symlen) <= endaddr && (nlist->n_type & N_STAB) == 0 && !strncmp(symbol, symbolStr, symlen)) {
					DBGLOG("mach", "found symbol %s at 0x%llx (non-aslr 0x%llx), type %x, sect %x, desc %x", symbol, nlist->n_value + kaslr_slide,
						   (uint64_t)nlist->n_value, nlist->n_type, nlist->n_sect, nlist->n_desc);
					// the symbol values are without kernel ASLR so we need to add it
					return nlist->n_value + kaslr_slide;
				}
			} else {
				SYSLOG("mach", "symbol at %u out of %u exceeds symbol table bounds", i, symboltable_nr_symbols);
				break;
			}
		}
	} else {
		SYSLOG("mach", "invalid symbol/string tables point behind symbol table");
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
		auto magicPtr = reinterpret_cast<uint32_t *>(buffer);
		if (!isAligned(magicPtr)) {
			SYSLOG("mach", "invalid mach header positioning");
			return KERN_FAILURE;
		}

		DBGLOG("mach", "readMachHeader got magic %08X", *magicPtr);

		switch (*magicPtr) {
			case MachMagicNative:
				fat_offset = off;
				return KERN_SUCCESS;
			case FAT_CIGAM:
			case FAT_MAGIC: {
				if (off != 0) {
					SYSLOG("mach", "fat found a recursion");
					return KERN_FAILURE;
				}

				bool swapBytes = *magicPtr == FAT_CIGAM;
				uint32_t num = reinterpret_cast<fat_header *>(buffer)->nfat_arch;
				if (swapBytes) num = OSSwapInt32(num);
				if (static_cast<size_t>(num) * sizeof(fat_arch) > HeaderSize - sizeof(fat_arch)) {
					SYSLOG("mach", "invalid fat arch count %u", num);
					return KERN_FAILURE;
				}

				for (uint32_t i = 0; i < num; i++) {
					auto arch = reinterpret_cast<fat_arch *>(buffer + i * sizeof(fat_arch) + sizeof(fat_header));
					cpu_type_t cpu = arch->cputype;
					if (swapBytes) cpu = OSSwapInt32(cpu);
					if (cpu == MachCpuTypeNative) {
						uint32_t offset = arch->offset;
						if (swapBytes) offset = OSSwapInt32(offset);
						return readMachHeader(buffer, vnode, ctxt, offset);
					}
				}

				SYSLOG("mach", "magic failed to find a %s mach", Configuration::currentArch);
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
						DBGLOG("mach", "decompressing %u bytes (estimated %u bytes) with %X compression mode", comp, dec, header->compression);

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
				SYSLOG("mach", "read mach has unsupported %X magic", *magicPtr);
				return KERN_FAILURE;
		}
	}

	return KERN_FAILURE;
}

kern_return_t MachInfo::readSymbols(vnode_t vnode, vfs_context_t ctxt) {
	// we know the location of linkedit and offsets into symbols and their strings
	// now we need to read linkedit into a buffer so we can process it later
	// __LINKEDIT total size is around 1MB
	// we should free this buffer later when we don't need anymore to solve symbols
	//
	// on 32-bit MH_OBJECT, symbols are not contained within a segment
	sym_buf = Buffer::create<uint8_t>(sym_size);
	if (!sym_buf) {
		SYSLOG("mach", "Could not allocate enough memory (%zu) for symbols", sym_size);
		return KERN_FAILURE;
	}

#ifdef LILU_COMPRESSION_SUPPORT
	if (file_buf) {
		if (file_buf_size >= sym_size && file_buf_size - sym_size >= sym_fileoff) {
			lilu_os_memcpy(sym_buf, file_buf + sym_fileoff, sym_size);
			return KERN_SUCCESS;
		}
		SYSLOG("mach", "requested linkedit (%llu %zu) exceeds file buf size (%u)", sym_fileoff, sym_size, file_buf_size);
	} else
#endif /* LILU_COMPRESSION_SUPPORT */
	{
		int error = FileIO::readFileData(sym_buf, fat_offset + sym_fileoff, sym_size, vnode, ctxt);
		if (!error)
			return KERN_SUCCESS;
		SYSLOG("mach", "symbols read failed with %d error", error);
	}

	Buffer::deleter(sym_buf);
	sym_buf = nullptr;

	return KERN_FAILURE;
}

void MachInfo::findSectionBounds(void *ptr, size_t sourceSize, vm_address_t &vmsegment, vm_address_t &vmsection, void *&sectionptr, size_t &sectionSize, void *&sectionCmdPtr, const char *segmentName, const char *sectionName, cpu_type_t cpu) {
	vmsegment = vmsection = 0;
	sectionptr = 0;
	sectionSize = 0;

	auto header = static_cast<mach_header *>(ptr);
	auto cmd = static_cast<load_command *>(ptr);

	if (!isAligned(header) || sourceSize < MachInfo::HeaderSize) {
		SYSLOG("mach", "invalid mach header for section lookup");
		return;
	}

	if (header->magic == MH_MAGIC_64) {
		reinterpret_cast<uintptr_t &>(cmd) += sizeof(mach_header_64);
	} else if (header->magic == MH_MAGIC) {
		reinterpret_cast<uintptr_t &>(cmd) += sizeof(mach_header);
	} else if (header->magic == FAT_CIGAM || header->magic == FAT_MAGIC) {
		if (cpu == 0) {
			SYSLOG("mach", "fat recursion in for section lookup");
			return;
		}

		fat_header *fheader = static_cast<fat_header *>(ptr);
		bool swapBytes = header->magic == FAT_CIGAM;
		uint32_t num = fheader->nfat_arch;
		if (swapBytes) num = OSSwapInt32(num);

		if (static_cast<uint64_t>(num) * sizeof(fat_arch) > HeaderSize - sizeof(fat_arch)) {
			SYSLOG("mach", "invalid fat arch count %u for section lookup", num);
			return;
		}

		fat_arch *farch = reinterpret_cast<fat_arch *>(reinterpret_cast<uintptr_t>(ptr) + sizeof(fat_header));
		for (size_t i = 0; i < num; i++, farch++) {
			cpu_type_t rcpu = farch->cputype;
			if (swapBytes) rcpu = OSSwapInt32(rcpu);
			if (rcpu == cpu) {
				uint32_t off = farch->offset;
				uint32_t sz  = farch->size;
				if (swapBytes) {
					off = OSSwapInt32(off);
					sz = OSSwapInt32(sz);
				}

				if (static_cast<uint64_t>(off) + sz > sourceSize) {
					SYSLOG("mach", "invalid fat offset for section %lu lookup", i);
					return;
				}

				void *tmpSectionCmdPtr;
				findSectionBounds(static_cast<uint8_t *>(ptr) + off, sourceSize - off, vmsegment, vmsection, sectionptr, sectionSize, tmpSectionCmdPtr, segmentName, sectionName, 0);
				break;
			}
		}
		return;
	}

	uint8_t *endaddr = static_cast<uint8_t *>(ptr) + sourceSize;

	for (uint32_t no = 0; no < header->ncmds; no++) {
		if (!isAligned(cmd)) {
			SYSLOG("mach", "invalid command %u position for section lookup", no);
			return;
		}

		if (reinterpret_cast<uint8_t *>(cmd) + sizeof(load_command) > endaddr || reinterpret_cast<uint8_t *>(cmd) + cmd->cmdsize > endaddr) {
			SYSLOG("mach", "header command %u exceeds header size for section lookup", no);
			return;
		}

		if (cmd->cmd == LC_SEGMENT) {
			auto scmd = reinterpret_cast<segment_command *>(cmd);
			if (!strncmp(scmd->segname, segmentName, sizeof(scmd->segname))) {
				auto sect = reinterpret_cast<section *>(cmd);
				reinterpret_cast<uintptr_t &>(sect) += sizeof(*scmd);

				if (reinterpret_cast<uint8_t *>(sect) + sizeof(*sect) * static_cast<uint64_t>(scmd->nsects) > endaddr) {
					SYSLOG("mach", "sections in segment %u exceed header size for section lookup", no);
					return;
				}

				for (uint32_t sno = 0; sno < scmd->nsects; sno++) {
					if (!strncmp(sect->sectname, sectionName, sizeof(sect->sectname))) {
						auto sptr = static_cast<uint8_t *>(ptr) + sect->offset;
						if (sptr + sect->size > endaddr) {
							SYSLOG("mach", "found section %s size %u in segment %llu is invalid", sectionName, sno, (uint64_t)vmsegment);
							return;
						}
						vmsegment = scmd->vmaddr;
						vmsection = sect->addr;
						sectionptr = sptr;
						sectionSize = static_cast<size_t>(sect->size);
						sectionCmdPtr = sect;
						DBGLOG("mach", "found section %s size %u in segment %llu", sectionName, sno, (uint64_t)vmsegment);
						return;
					}

					sect++;
				}
			}
		} else if (cmd->cmd == LC_SEGMENT_64) {
			auto scmd = reinterpret_cast<segment_command_64 *>(cmd);
			if (!strncmp(scmd->segname, segmentName, sizeof(scmd->segname))) {
				auto sect = reinterpret_cast<section_64 *>(cmd);
				reinterpret_cast<uintptr_t &>(sect) += sizeof(*scmd);

				if (reinterpret_cast<uint8_t *>(sect) + sizeof(*sect) * static_cast<uint64_t>(scmd->nsects) > endaddr) {
					SYSLOG("mach", "sections in segment %u exceed header size for section lookup", no);
					return;
				}

				for (uint32_t sno = 0; sno < scmd->nsects; sno++) {
					if (!strncmp(sect->sectname, sectionName, sizeof(sect->sectname))) {
						auto sptr = static_cast<uint8_t *>(ptr) + sect->offset;
						if (sptr + sect->size > endaddr) {
							SYSLOG("mach", "found section %s size %u in segment %llu is invalid", sectionName, sno, (uint64_t)vmsegment);
							return;
						}
						vmsegment = (vm_address_t)scmd->vmaddr;
						vmsection = (vm_address_t)sect->addr;
						sectionptr = sptr;
						sectionSize = static_cast<size_t>(sect->size);
						sectionCmdPtr = sect;
						DBGLOG("mach", "found section %s size %u in segment %llu", sectionName, sno, (uint64_t)vmsegment);
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
	auto mh = static_cast<mach_header_native *>(header);
	size_t headerSize = sizeof(mach_header_native);

	// point to the first load command
	auto addr    = static_cast<uint8_t *>(header) + headerSize;
	auto endaddr = static_cast<uint8_t *>(header) + HeaderSize;
	// iterate over all load cmds and retrieve required info to solve symbols
	// __LINKEDIT location and symbol/string table location
	for (uint32_t i = 0; i < mh->ncmds; i++) {
		auto loadCmd = reinterpret_cast<load_command *>(addr);

		if (!isAligned(loadCmd)) {
			SYSLOG("mach", "invalid command %u position in %s", i, safeString(objectId));
			return;
		}

		if (addr + sizeof(load_command) > endaddr || addr + loadCmd->cmdsize > endaddr) {
			SYSLOG("mach", "header command %u of info %s exceeds header size", i, safeString(objectId));
			return;
		}

		if (loadCmd->cmd == SegmentTypeNative) {
			auto segCmd = reinterpret_cast<segment_command_native *>(loadCmd);
			// use this one to retrieve the original vm address of __TEXT so we can compute kernel aslr slide
			if (!strncmp(segCmd->segname, "__TEXT", sizeof(segCmd->segname))) {
				DBGLOG("mach", "header processing found TEXT");
				disk_text_addr = segCmd->vmaddr;
			} else if (!strncmp(segCmd->segname, "__LINKEDIT", sizeof(segCmd->segname))) {
				DBGLOG("mach", "header processing found LINKEDIT");
				sym_fileoff = segCmd->fileoff;
				sym_size = (size_t)segCmd->filesize;
			}
		}
		// table information available at LC_SYMTAB command
		else if (loadCmd->cmd == LC_SYMTAB) {
			DBGLOG("mach", "header processing found SYMTAB");
			auto symtab_cmd = reinterpret_cast<symtab_command *>(loadCmd);
			symboltable_fileoff = symtab_cmd->symoff;
			symboltable_nr_symbols = symtab_cmd->nsyms;
			stringtable_fileoff = symtab_cmd->stroff;
			stringtable_size = symtab_cmd->strsize;
		}
		addr += loadCmd->cmdsize;
	}
	
#if defined(__i386__)
	// 32-bit MH_OBJECT does not have a __LINKEDIT segment for symbols
	if (mh->filetype == MH_OBJECT && symboltable_fileoff && !sym_fileoff) {
		if (symboltable_fileoff < stringtable_fileoff) {
			sym_fileoff = symboltable_fileoff;
			sym_size = (stringtable_fileoff - symboltable_fileoff) + stringtable_size;
		} else {
			sym_fileoff = stringtable_fileoff;
			sym_size = (symboltable_fileoff - stringtable_fileoff) + (symboltable_nr_symbols * sizeof(struct nlist));
		}
	}
#endif
}

void MachInfo::updatePrelinkInfo() {
	if (!prelink_dict && machType != MachType::Kext && file_buf) {
		vm_address_t tmpSeg, tmpSect;
		void *tmpSectPtr;
		size_t tmpSectSize;
		void *tmpSectionCmdPtr;
		findSectionBounds(file_buf, file_buf_size, tmpSeg, tmpSect, tmpSectPtr, tmpSectSize, tmpSectionCmdPtr, "__PRELINK_INFO", "__info");
		size_t startoff = tmpSectSize && static_cast<uint8_t *>(tmpSectPtr) >= file_buf ?
		                static_cast<uint8_t *>(tmpSectPtr) - file_buf : file_buf_size;
		if (tmpSectSize > 0 && file_buf_size > startoff && file_buf_size - startoff >= tmpSectSize) {
			auto xmlData = static_cast<const char *>(tmpSectPtr);
			auto objData = xmlData[tmpSectSize-1] == '\0' ? OSUnserializeXML(xmlData, nullptr) : nullptr;
			prelink_dict = OSDynamicCast(OSDictionary, objData);
			if (prelink_dict) {
				if (machType == MachType::Kernel) {
					findSectionBounds(file_buf, file_buf_size, tmpSeg, tmpSect, tmpSectPtr, tmpSectSize, tmpSectionCmdPtr, "__PRELINK_TEXT", "__text");
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
				} else if (machType == MachType::KextCollection) {
					prelink_addr = file_buf;
					prelink_vmaddr = 0;
					prelink_slid = 0;
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
	}
}

uint8_t *MachInfo::findImage(const char *identifier, uint32_t &imageIndex, uint32_t &imageSize, mach_vm_address_t &slide, bool &missing) {
	updatePrelinkInfo();

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
								imageIndex = i;
								return imageaddr;
							} else {
								SYSLOG("mach", "invalid addresses of kext %s at %u of %u prelink", identifier, i, imageNum);
							}
						}

						SYSLOG("mach", "unable to obtain addr and size for %s at %u of %u prelink", identifier, i, imageNum);
						return nullptr;
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

kern_return_t MachInfo::kcGetRunningAddresses(mach_vm_address_t slide) {
#if defined (__i386__)
	// KC is not supported on 32-bit.
	return KERN_FAILURE;
	
#elif defined (__x86_64__)
	// We are meant to know the base address of kexts
	mach_vm_address_t base = slide ? slide : findKernelBase();

	if (base == 0) {
		SYSLOG("mach", "unknown base for kc mode, aborting");
		return KERN_FAILURE;
	}

	auto mh = reinterpret_cast<mach_header_64 *>(base);
	mach_header_64 *inner = nullptr;

	// __LINKEDIT is present in the inner kernel only.
	if (machType != MachType::Kext) {
		auto addr = reinterpret_cast<uint8_t *>(mh) + sizeof(mach_header_64);
		DBGLOG("mach", "looking up inner kernel in %u commands at " PRIKADDR, mh->ncmds, CASTKADDR(mh));
		for (uint32_t i = 0; i < mh->ncmds; i++) {
			auto loadCmd = reinterpret_cast<load_command *>(addr);

			if (loadCmd->cmd == LC_SEGMENT_64) {
				segment_command_64 *segCmd = reinterpret_cast<segment_command_64 *>(loadCmd);
				if (!strncmp(segCmd->segname, "__TEXT_EXEC", sizeof(segCmd->segname))) {
					inner = reinterpret_cast<mach_header_64 *>(segCmd->vmaddr);
					break;
				}
			}
			addr += loadCmd->cmdsize;
		}

		if (!inner) {
			SYSLOG("mach", "failed to find inner kernel kc mach-o");
			return KERN_FAILURE;
		}
	} else {
		inner = mh;
	}

	DBGLOG("mach", "got nouveau mach-o for %s at " PRIKADDR, objectId, CASTKADDR(inner));

	mach_vm_address_t last_addr = 0;

	auto addr = reinterpret_cast<uint8_t *>(inner) + sizeof(mach_header_64);
	for (uint32_t i = 0; i < inner->ncmds; i++) {
		load_command *loadCmd = reinterpret_cast<load_command *>(addr);

		if (loadCmd->cmd == LC_SEGMENT_64) {
			auto segCmd = reinterpret_cast<segment_command_64 *>(loadCmd);
			DBGLOG("mach", "%s has segment is %s from " PRIKADDR " to " PRIKADDR, objectId, segCmd->segname,
				   CASTKADDR(segCmd->vmaddr), CASTKADDR(segCmd->vmaddr + segCmd->vmsize));
			if (!sym_buf && !strncmp(segCmd->segname, "__LINKEDIT", sizeof(segCmd->segname))) {
				sym_buf = reinterpret_cast<uint8_t *>(segCmd->vmaddr);
				sym_fileoff = segCmd->fileoff;
				sym_size = (size_t)segCmd->vmsize;
				sym_buf_ro = true;
			} else if (segCmd->vmaddr + segCmd->vmsize > last_addr) {
				// We exclude __LINKEDIT here as it is much farther from the rest of the segments,
				// and we will unlikely need to patch it anyway. Doing this makes it much safer
				// to apply patches, as they will not hit unmapped areas.
				last_addr = segCmd->vmaddr + segCmd->vmsize;
			}
		} else if (!symboltable_fileoff && loadCmd->cmd == LC_SYMTAB) {
			auto symtab_cmd = reinterpret_cast<symtab_command *>(loadCmd);
			symboltable_fileoff = symtab_cmd->symoff;
			symboltable_nr_symbols = symtab_cmd->nsyms;
			stringtable_fileoff = symtab_cmd->stroff;
		}

		addr += loadCmd->cmdsize;
	}

	if (!sym_buf || !symboltable_fileoff) {
		SYSLOG("mach", "failed to find kc linkedit %d symtab %d", sym_buf != nullptr, symboltable_fileoff != 0);
		return KERN_FAILURE;
	}

	if (last_addr < reinterpret_cast<mach_vm_address_t>(inner)) {
		SYSLOG("mach", "invalid last address " PRIKADDR " with header " PRIKADDR, CASTKADDR(last_addr), CASTKADDR(inner));
		return KERN_FAILURE;
	}

	kaslr_slide_set = true;
	prelink_slid = true;
	running_mh = inner;
	memory_size = (size_t)(last_addr - reinterpret_cast<mach_vm_address_t>(inner));
	if (slide != 0 || machType != MachType::Kext) {
		address_slots = reinterpret_cast<mach_vm_address_t>(inner + 1) + inner->sizeofcmds;
		address_slots_end = (address_slots + (PAGE_SIZE - 1)) & ~PAGE_SIZE;
		while (*reinterpret_cast<uint32_t *>(address_slots_end) == 0) {
			address_slots_end += PAGE_SIZE;
		}

		DBGLOG("mach", "activating slots for %s in " PRIKADDR " - " PRIKADDR, objectId, CASTKADDR(address_slots), CASTKADDR(address_slots_end));
	}
	return KERN_SUCCESS;

#else
#error Unsupported arch.
#endif
}

mach_vm_address_t MachInfo::getAddressSlot() {
	if (address_slots && address_slots + sizeof(mach_vm_address_t) <= address_slots_end) {
		auto slot = address_slots;
		address_slots += sizeof(mach_vm_address_t);
		return slot;
	}
	return 0;
}

kern_return_t MachInfo::getRunningAddresses(mach_vm_address_t slide, size_t size, bool force) {
	if (force) {
		kaslr_slide_set = false;
		running_mh = nullptr;
		running_text_addr = 0;
		memory_size = 0;
	}

	if (kaslr_slide_set) return KERN_SUCCESS;

	if (size > 0) memory_size = size;

	// We are meant to know the base address of kexts
	mach_vm_address_t base = slide ? slide : findKernelBase();

	if (kernel_collection && !sym_buf)
		return kcGetRunningAddresses(slide);

	if (base != 0) {
		// get the vm address of __TEXT segment
		auto mh = reinterpret_cast<mach_header_native *>(base);
		auto headerSize = sizeof(mach_header_native);

		load_command *loadCmd;
		auto addr = reinterpret_cast<uint8_t *>(base) + headerSize;
		auto endaddr = reinterpret_cast<uint8_t *>(base) + HeaderSize;
		for (uint32_t i = 0; i < mh->ncmds; i++) {
			loadCmd = reinterpret_cast<load_command *>(addr);

			if (!isAligned(loadCmd)) {
				SYSLOG("mach", "running command %u invalid position in %s", i, safeString(objectId));
				return KERN_FAILURE;
			}

			if (addr + sizeof(load_command) > endaddr || addr + loadCmd->cmdsize > endaddr) {
				SYSLOG("mach", "running command %u of info %s exceeds header size", i, safeString(objectId));
				return KERN_FAILURE;
			}

			if (loadCmd->cmd == SegmentTypeNative) {
				auto segCmd = reinterpret_cast<segment_command_native *>(loadCmd);
				if (!strncmp(segCmd->segname, "__TEXT", sizeof(segCmd->segname))) {
					running_text_addr = segCmd->vmaddr;
					running_mh = mh;
					break;
				}
#if defined(__i386__)
				// MH_OBJECT has a single unnamed segment
				else if (mh->filetype == MH_OBJECT && !strncmp(segCmd->segname, "", sizeof(segCmd->segname))) {
					for (uint32_t j = 0; j < segCmd->nsects; j++) {
						auto sect = reinterpret_cast<section *>(segCmd + 1) + j;
						
						if (!strncmp(sect->sectname, "__text", sizeof(sect->sectname))) {
							running_text_addr = sect->addr;
							running_mh = mh;
							
							// MH_OBJECT may have a file offset, align to the next page and add to the slide.
							slide += alignValue(sect->offset);
							break;
						}
					}
				}
				
				if (running_text_addr)
					break;
#endif
			}
			addr += loadCmd->cmdsize;
		}
	}

	// compute kaslr slide
	if (
#if defined(__x86_64__)
			running_text_addr &&
#endif
			running_mh) {
		if (!slide) // This is kernel image
			kaslr_slide = running_text_addr - disk_text_addr;
		else // This is kext image
			kaslr_slide = prelink_slid ? prelink_vmaddr : slide;
		kaslr_slide_set = true;

		DBGLOG("mach", "aslr/load slide is 0x%llx", kaslr_slide);
				
#if defined(__x86_64__)
		address_slots = reinterpret_cast<mach_vm_address_t>(running_mh + 1) + running_mh->sizeofcmds;
		address_slots_end = (address_slots + (PAGE_SIZE - 1)) & ~PAGE_SIZE;
		while (*reinterpret_cast<uint32_t *>(address_slots_end) == 0) {
			address_slots_end += PAGE_SIZE;
		}
#endif
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

	auto mh = static_cast<mach_header_native *>(header);
	size_t size = sizeof(mach_header_native);

	auto *addr = static_cast<uint8_t *>(header) + size;
	auto endaddr = static_cast<uint8_t *>(header) + HeaderSize;
	for (uint32_t i = 0; i < mh->ncmds; i++) {
		auto loadCmd = reinterpret_cast<load_command *>(addr);

		if (!isAligned(loadCmd)) {
			SYSLOG("mach", "uuid command %u invalid position in %s", i, safeString(objectId));
			return nullptr;
		}

		if (addr + sizeof(load_command) > endaddr || addr + loadCmd->cmdsize > endaddr) {
			SYSLOG("mach", "uuid command %u of info %s exceeds header size", i, safeString(objectId));
			return nullptr;
		}

		if (loadCmd->cmd == LC_UUID)
			return reinterpret_cast<uint64_t *>(reinterpret_cast<uuid_command *>(loadCmd)->uuid);

		addr += loadCmd->cmdsize;
	}

	return nullptr;
}

bool MachInfo::loadUUID(void *header) {
	// Versions older than 10.5 may not have UUIDs on system binaries.
	if (getKernelVersion() < KernelVersion::Leopard)
		return true;
	
	auto p = getUUID(header);
	if (p) {
		self_uuid[0] = p[0];
		self_uuid[1] = p[1];
		return true;
	}
	return false;
}

bool MachInfo::isCurrentBinary(mach_vm_address_t base) {
	if (kernel_collection)
		return true;
	
	// Versions older than 10.5 may not have UUIDs on system binaries.
	if (getKernelVersion() < KernelVersion::Leopard)
		return true;

	auto binaryBase = reinterpret_cast<void *>(base ? base : findKernelBase());
	auto binaryUUID = binaryBase ? getUUID(binaryBase) : nullptr;

	bool match = binaryUUID && self_uuid[0] == binaryUUID[0] && self_uuid[1] == binaryUUID[1];

	if (!match) {
		if (binaryUUID)
			DBGLOG("mach", "binary UUID is " PRIUUID, CASTUUID(binaryUUID));
		else
			DBGLOG("mach", "binary UUID is missing");
		DBGLOG("mach", "self UUID is " PRIUUID, CASTUUID(self_uuid));
	}

	return match;
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
