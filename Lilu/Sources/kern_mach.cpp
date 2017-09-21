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

extern proc_t kernproc;

kern_return_t MachInfo::init(const char * const paths[], size_t num, MachInfo *prelink) {
	kern_return_t error = KERN_FAILURE;
	
	allow_decompress = config.allowDecompress;

	// Check if we have a proper credential, prevents a race-condition panic on 10.11.4 Beta
	// When calling kauth_cred_get() for the current_thread.
	// This probably wants a better solution...
	if (!kernproc || !current_thread() || !vfs_context_current() || !vfs_context_ucred(vfs_context_current())) {
		SYSLOG("mach @ current context has no credential, it's too early");
		return error;
	}

	// Attempt to get linkedit from prelink
	if (prelink && objectId) {
		/*
		 We need to obtain the following from prelink:
		 linkedit_buf
		 linkedit_size
		 linkedit_fileoff
		 stringtable_fileoff
		 symboltable_fileoff
		 */
	}

	// Load directly from the filesystem
	if (!linkedit_buf) {
		// Allocate some data for header
		auto machHeader = Buffer::create<uint8_t>(HeaderSize);
		if (!machHeader) {
			SYSLOG("mach @ can't allocate header memory.");
			return error;
		}

		vnode_t vnode = NULLVP;
		vfs_context_t ctxt = nullptr;
		bool found = false;

		for (size_t i = 0; i < num; i++) {
			vnode = NULLVP;
			ctxt = vfs_context_create(nullptr);

			errno_t err = vnode_lookup(paths[i], 0, &vnode, ctxt);
			if (!err) {
				kern_return_t readError = readMachHeader(machHeader, vnode, ctxt);
				if (readError == KERN_SUCCESS) {
					if (isKernel && !isCurrentKernel(machHeader)) {
						vnode_put(vnode);
					} else {
						DBGLOG("mach @ Found executable at path: %s", paths[i]);
						found = true;
						break;
					}
				}
			}

			vfs_context_rele(ctxt);
		}

		if (!found) {
			DBGLOG("mach @ couldn't find a suitable executable");
			Buffer::deleter(machHeader);
			return error;
		}

		processMachHeader(machHeader);
		if (linkedit_fileoff && symboltable_fileoff) {
			// read linkedit from filesystem
			error = readLinkedit(vnode, ctxt);
			if (error != KERN_SUCCESS) {
				SYSLOG("mach @ could not read the linkedit segment");
			}
		} else {
			SYSLOG("mach @ couldn't find the necessary mach segments or sections (linkedit %llX, sym %X)",
				   linkedit_fileoff, symboltable_fileoff);
		}

		vfs_context_rele(ctxt);
		// drop the iocount due to vnode_lookup()
		// we must do this or the machine gets stuck on shutdown/reboot
		vnode_put(vnode);

		Buffer::deleter(machHeader);
	}
	
	return error;
}

void MachInfo::deinit() {
	freeFileBufferResources();

	if (linkedit_buf) {
		Buffer::deleter(linkedit_buf);
		linkedit_buf = nullptr;
	}
}

mach_vm_address_t MachInfo::findKernelBase() {
	// calculate the address of the int80 handler
	mach_vm_address_t tmp = calculateInt80Address();
	
	// search backwards for the kernel base address (mach-o header)
	segment_command_64 *segmentCommand;
	while (tmp > 0) {
		if (*reinterpret_cast<uint32_t *>(tmp) == MH_MAGIC_64) {
			// make sure it's the header and not some reference to the MAGIC number
			segmentCommand = reinterpret_cast<segment_command_64 *>(tmp + sizeof(mach_header_64));
			if (strncmp(segmentCommand->segname, "__TEXT", strlen("__TEXT")) == 0) {
				DBGLOG("mach @ Found kernel mach-o header address at %p", (void*)(tmp));
				return tmp;
			}
		}
		tmp--;
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
		SYSLOG("mach @ failed to set wp bit to %d", !enable);
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
		SYSLOG("mach @ no loaded linkedit buffer found");
		return 0;
	}
	
	if (!symboltable_fileoff) {
		SYSLOG("mach @ no symtable offsets found");
		return 0;
	}
	
	if (!kaslr_slide_set) {
		SYSLOG("mach @ no slide is present");
		return 0;
	}
	
	// symbols and strings offsets into LINKEDIT
	// we just read the __LINKEDIT but fileoff values are relative to the full Mach-O
	// subtract the base of LINKEDIT to fix the value into our buffer
	uint64_t symbolOff = symboltable_fileoff - linkedit_fileoff;
	if (symbolOff > symboltable_fileoff) return 0;
	uint64_t stringOff = stringtable_fileoff - linkedit_fileoff;
	if (stringOff > stringtable_fileoff) return 0;
	
	nlist_64 *nlist64 = nullptr;
	// search for the symbol and get its location if found
	for (uint32_t i = 0; i < symboltable_nr_symbols; i++) {
		// get the pointer to the symbol entry and extract its symbol string
		nlist64 = reinterpret_cast<nlist_64 *>(linkedit_buf + symbolOff + i * sizeof(nlist_64));
		char *symbolStr = reinterpret_cast<char *>(linkedit_buf + stringOff + nlist64->n_un.n_strx);
		// find if symbol matches
		if (strncmp(symbol, symbolStr, strlen(symbol)+1) == 0) {
			DBGLOG("mach @ Found symbol %s at 0x%llx (non-aslr 0x%llx)", symbol, nlist64->n_value + kaslr_slide, nlist64->n_value);
			// the symbol values are without kernel ASLR so we need to add it
			return nlist64->n_value + kaslr_slide;
		}
	}
	// failure
	return 0;
}

kern_return_t MachInfo::readMachHeader(uint8_t *buffer, vnode_t vnode, vfs_context_t ctxt, off_t off) {
	int error = FileIO::readFileData(buffer, off, HeaderSize, vnode, ctxt);
	if (error) {
		SYSLOG("mach @ mach header read failed with %d error", error);
		return KERN_FAILURE;
	}
	
	while (1) {
		auto magic = *reinterpret_cast<uint32_t *>(buffer);
		switch (magic) {
			case MH_MAGIC_64:
				fat_offset = off;
				return KERN_SUCCESS;
			case FAT_MAGIC: {
				uint32_t num = OSSwapInt32(reinterpret_cast<fat_header *>(buffer)->nfat_arch);
				for (uint32_t i = 0; i < num; i++) {
					auto arch = reinterpret_cast<fat_arch *>(buffer + i*sizeof(fat_arch) + sizeof(fat_header));
					if (OSSwapInt32(arch->cputype) == CPU_TYPE_X86_64)
						return readMachHeader(buffer, vnode, ctxt, OSSwapInt32(arch->offset));
				}
				SYSLOG("mach @ magic failed to find a x86_64 mach");
				return KERN_FAILURE;
			}
			case FAT_CIGAM: {
				uint32_t num = reinterpret_cast<fat_header *>(buffer)->nfat_arch;
				for (uint32_t i = 0; i < num; i++) {
					auto arch = reinterpret_cast<fat_arch *>(buffer + i*sizeof(fat_arch) + sizeof(fat_header));
					if (arch->cputype == CPU_TYPE_X86_64)
						return readMachHeader(buffer, vnode, ctxt, OSSwapInt32(arch->offset));
				}
				SYSLOG("mach @ cigam failed to find a x86_64 mach");
				return KERN_FAILURE;
			}
#ifdef LILU_COMPRESSION_SUPPORT
			case Compression::Magic: { // comp
				auto header = reinterpret_cast<Compression::Header *>(buffer);
				auto compressedBuf = Buffer::create<uint8_t>(OSSwapInt32(header->compressed));
				if (!compressedBuf) {
					SYSLOG("mach @ failed to allocate memory for reading mach binary");
				} else if (FileIO::readFileData(compressedBuf, off+sizeof(Compression::Header), OSSwapInt32(header->compressed),
										vnode, ctxt) != KERN_SUCCESS) {
					SYSLOG("mach @ failed to read compressed binary");
				} else {
					DBGLOG("mach @ decompressing %d bytes (estimated %d bytes) with %X compression mode",
						   OSSwapInt32(header->compressed), OSSwapInt32(header->decompressed), header->compression);
					
					if (allow_decompress) {
						file_buf = Compression::decompress(header->compression, OSSwapInt32(header->decompressed),
														   compressedBuf, OSSwapInt32(header->compressed));
						
						// Try again
						if (file_buf) {
							lilu_os_memcpy(buffer, file_buf, HeaderSize);
							Buffer::deleter(compressedBuf);
							continue;
						}
					} else {
						SYSLOG("compression @ disabled due to low memory flag");
					}
				}
				
				Buffer::deleter(compressedBuf);
				return KERN_FAILURE;
			}
#endif /* LILU_COMPRESSION_SUPPORT */
			default:
				SYSLOG("mach @ read mach has unsupported %X magic", magic);
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
		SYSLOG("mach @ Could not allocate enough memory (%lld) for __LINKEDIT segment", linkedit_size);
		return KERN_FAILURE;
	}

#ifdef LILU_COMPRESSION_SUPPORT
	if (file_buf) {
		lilu_os_memcpy(linkedit_buf, file_buf+linkedit_fileoff, linkedit_size);
	} else
#endif /* LILU_COMPRESSION_SUPPORT */
	{
		int error = FileIO::readFileData(linkedit_buf, fat_offset+linkedit_fileoff, linkedit_size, vnode, ctxt);
		if (error) {
			SYSLOG("mach @ linkedit read failed with %d error", error);
			return KERN_FAILURE;
		}
	}

	return KERN_SUCCESS;
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
						DBGLOG("mach @ found section %lu size %lu in segment %lu\n", vmsection, vmsegment, size);
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
						DBGLOG("mach @ found section %lu size %lu in segment %lu\n", vmsection, vmsegment, size);
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
			SYSLOG("mach @ header command %u of info %s exceeds header size", i, objectId ? objectId : "(null)");
			return;
		}

		if (loadCmd->cmd == LC_SEGMENT_64) {
			segment_command_64 *segCmd = reinterpret_cast<segment_command_64 *>(loadCmd);
			// use this one to retrieve the original vm address of __TEXT so we can compute kernel aslr slide
			if (strncmp(segCmd->segname, "__TEXT", strlen("__TEXT")) == 0) {
				DBGLOG("mach @ header processing found TEXT");
				disk_text_addr = segCmd->vmaddr;
			} else if (strncmp(segCmd->segname, "__LINKEDIT", strlen("__LINKEDIT")) == 0) {
				DBGLOG("mach @ header processing found LINKEDIT");
				linkedit_fileoff = segCmd->fileoff;
				linkedit_size = segCmd->filesize;
			}
		}
		// table information available at LC_SYMTAB command
		else if (loadCmd->cmd == LC_SYMTAB) {
			DBGLOG("mach @ header processing found SYMTAB");
			auto symtab_cmd = reinterpret_cast<symtab_command *>(loadCmd);
			symboltable_fileoff = symtab_cmd->symoff;
			symboltable_nr_symbols = symtab_cmd->nsyms;
			stringtable_fileoff = symtab_cmd->stroff;
		}
		addr += loadCmd->cmdsize;
	}
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
				SYSLOG("mach @ running command %u of info %s exceeds header size", i, objectId ? objectId : "(null)");
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
		if (!slide) {
			kaslr_slide = running_text_addr - disk_text_addr;
		} else {
			kaslr_slide = slide;
		}
		kaslr_slide_set = true;
		
		DBGLOG("mach @ aslr/load slide is 0x%llx", kaslr_slide);
	} else {
		SYSLOG("mach @ Couldn't find the running addresses");
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
	DBGLOG("mach @ getRunningPosition %p of memory %lu size", header, size);
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
			SYSLOG("mach @ uuid command %u of info %s exceeds header size", i, objectId ? objectId : "(null)");
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

mach_vm_address_t MachInfo::getIDTAddress() {
	uint8_t idtr[10];
	asm volatile("sidt %0" : "=m"(idtr));
	return *reinterpret_cast<mach_vm_address_t *>(idtr+2);
}

mach_vm_address_t MachInfo::calculateInt80Address() {
	// retrieve the address of the IDT
	mach_vm_address_t idtAddr = getIDTAddress();
	
	// find the address of interrupt 0x80 - EXCEP64_SPC_USR(0x80,hi64_unix_scall) @ osfmk/i386/idt64.s
	descriptor_idt *int80Descr = nullptr;
	mach_vm_address_t int80Addr = 0;
	// we need to compute the address, it's not direct
	// extract the stub address
	
	// retrieve the descriptor for interrupt 0x80
	// the IDT is an array of descriptors
	int80Descr = reinterpret_cast<descriptor_idt *>(idtAddr+sizeof(descriptor_idt)*0x80);
	uint64_t high = static_cast<uint64_t>(int80Descr->offset_high) << 32;
	uint32_t middle = static_cast<uint32_t>(int80Descr->offset_middle) << 16;
	int80Addr = static_cast<mach_vm_address_t>(high + middle + int80Descr->offset_low);
	DBGLOG("mach @ Address of interrupt 80 stub is 0x%llx", int80Addr);
	
	return int80Addr;
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
