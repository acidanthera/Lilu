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
#include <umm_malloc.h>

bool Disassembler::init(bool detailed) {
	if (initialised) return true;
	
	// This is necessary to allow using capstone with interrupts disabled.
	cs_opt_mem setup {
		umm_malloc,
		umm_calloc,
		umm_realloc,
		umm_free,
		vsnprintf
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
		if (result)
			cs_free(result, insts);
		return 0;
	}
	
	size_t size {0};
	
	for (size_t i = 0; i < insts && size < min; i++)
		size += result[i].size;
	
	cs_free(result, insts);
	
	if (size >= min)
		return size;
	
	SYSLOG("disasm @ capstone failed to disasemble enough memory (%zu), was %llX address valid?", min, addr);
	return 0;
}
