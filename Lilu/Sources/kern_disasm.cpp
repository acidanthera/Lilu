//
//  kern_disasm.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_disasm.hpp>
#include <Headers/kern_util.hpp>

#include <capstone.h>

bool Disassembler::init(bool detailed) {
	if (initialised) return true;
	
	cs_opt_mem setup {
		.malloc    = &kern_os_malloc,
		.calloc    = &kern_os_calloc,
		.realloc   = &kern_os_realloc,
		.free      = &kern_os_free,
		.vsnprintf = &vsnprintf,
	};
	
	cs_err err = cs_option(0, CS_OPT_MEM, reinterpret_cast<size_t>(&setup));
	
	if (err != CS_ERR_OK) {
		SYSLOG("disasm @ capstone memory management failed (%d)", err);
		return false;
	}
	
	err = cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
	
	if (err != CS_ERR_OK) {
		SYSLOG("disasm @ capstone cs_open failed (%d)", err);
		return false;
	}
	
	initialised = true;
	
	if (detailed) {
		err = cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
		if (err != CS_ERR_OK) {
			SYSLOG("disasm @ capstone instruction detalisation unsupported (%d)", err);
			return false;
		}
	}
	
	return true;
}

void Disassembler::deinit() {
	if (initialised) {
		cs_close(&handle);
		initialised = false;
	}
}

size_t Disassembler::instructionSize(mach_vm_address_t addr, size_t min) {
	cs_insn *result {nullptr};
	size_t insts = cs_disasm(handle, reinterpret_cast<uint8_t *>(addr), min+MaxInstruction, 0, 0, &result);
	
	cs_err err = cs_errno(handle);
	if (err != CS_ERR_OK) {
		SYSLOG("disasm @ capstone failed to disasemble memory (%zu, %p)", insts, result);
		if (insts != 0 && result)
			kern_os_free(result);
		return 0;
	}
	
	size_t size {0};
	
	for (size_t i = 0; i < insts && size < min; i++) {
		size += result[i].size;
	}
	
	kern_os_free(result);
	
	if (size >= min) {
		return size;
	}
	
	SYSLOG("disasm @ capstone failed to disasemble enough memory (%zu), was %llX address valid?", min, addr);
	return 0;
}
