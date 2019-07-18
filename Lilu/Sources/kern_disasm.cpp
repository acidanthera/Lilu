//
//  kern_disasm.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_disasm.hpp>
#include <Headers/kern_util.hpp>

#include <hde64.h>

#ifdef LILU_ADVANCED_DISASSEMBLY

#include <umm_malloc.h>
#include <Headers/capstone/capstone.h>

bool Disassembler::init(bool detailed) {
	if (initialised) return true;

	// Do not set global options twice
	static bool capstoneInitialised = false;
	if (!capstoneInitialised) {
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
			SYSLOG("disasm", "capstone memory management failed (%d)", err);
			return false;
		}

		capstoneInitialised = true;
	}

	cs_err err = cs_open(CS_ARCH_X86, CS_MODE_64, &handle);

	if (err != CS_ERR_OK) {
		SYSLOG("disasm", "capstone cs_open failed (%d)", err);
		return false;
	}

	initialised = true;

	if (detailed) {
		err = cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
		if (err != CS_ERR_OK) {
			SYSLOG("disasm", "capstone instruction detalisation unsupported (%d)", err);
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

#endif /* LILU_ADVANCED_DISASSEMBLY */

size_t Disassembler::quickInstructionSize(mach_vm_address_t addr, size_t min) {
	size_t total = 0;

	do {
		hde64s hs;
		auto len = hde64_disasm(reinterpret_cast<void *>(addr), &hs);

		if (hs.flags & F_ERROR) {
			SYSLOG("disasm", "hde decoding failure");
			return 0;
		}

		addr += len;
		total += len;
	} while (total < min);

	return total;
}

#ifdef LILU_ADVANCED_DISASSEMBLY

size_t Disassembler::disasmBuf(mach_vm_address_t addr, size_t size, cs_insn **result) {
	*result = nullptr;
	size_t insts = cs_disasm(handle, reinterpret_cast<uint8_t *>(addr), size, 0, 0, result);

	cs_err err = cs_errno(handle);
	if (err != CS_ERR_OK) {
		SYSLOG("disasm", "buf capstone failed to disasemble memory (%lu, %p)", insts, result);
		if (*result) {
			cs_free(*result, insts);
			*result = nullptr;
			return 0;
		}
	}

	return insts;
}

size_t Disassembler::instructionSize(mach_vm_address_t addr, size_t min) {
	cs_insn *result {nullptr};
	size_t insts = disasmBuf(addr, min+MaxInstruction, &result);

	if (result) {
		size_t size {0};

		for (size_t i = 0; i < insts && size < min; i++)
			size += result[i].size;

		cs_free(result, insts);

		if (size >= min)
			return size;
	}

	SYSLOG("disasm", "capstone failed to disasemble enough memory (%lu), was %llX address valid?", min, addr);
	return 0;
}

mach_vm_address_t Disassembler::disasmNthSub(mach_vm_address_t addr, size_t num, size_t lookup_size) {
	cs_insn *result {nullptr};
	size_t disasm_size = disasmBuf(addr, lookup_size, &result);

	if (disasm_size > 0) {
		size_t counter = 0;
		mach_vm_address_t sub_addr = 0;

		for (size_t i = 0; i < disasm_size; i++) {
			if (result[i].id == X86_INS_CALL) {
				if (result[i].detail) {
					if (result[i].detail->x86.op_count == 1 &&
						result[i].detail->x86.operands[0].type == X86_OP_IMM) {
						sub_addr = result[i].detail->x86.operands[0].imm + addr;
						counter++;
					}
				} else {
					break;
				}
			}

			if (counter == num)
				break;
			else
				sub_addr = 0;
		}

		cs_free(result, disasm_size);
		return sub_addr;
	}

	return 0;
}

mach_vm_address_t Disassembler::disasmNthJmp(mach_vm_address_t addr, size_t num, size_t lookup_size) {
	cs_insn *result {nullptr};
	size_t disasm_size = disasmBuf(addr, lookup_size, &result);

	if (disasm_size > 0) {
		size_t counter = 0;
		mach_vm_address_t sub_addr = 0;

		for (size_t i = 0; i < disasm_size; i++) {
			if (result[i].id == X86_INS_JMP) {
				if (result[i].detail) {
					if (result[i].detail->x86.op_count == 1 &&
						result[i].detail->x86.operands[0].type == X86_OP_IMM) {
						sub_addr = result[i].detail->x86.operands[0].imm + addr;
						counter++;
					}
				} else {
					break;
				}
			}

			if (counter == num)
				break;
			else
				sub_addr = 0;
		}

		cs_free(result, disasm_size);
		return sub_addr;
	}

	return 0;
}

mach_vm_address_t Disassembler::disasmNthIns(mach_vm_address_t addr, x86_insn ins, size_t num, size_t lookup_size) {
	cs_insn *result {nullptr};
	size_t disasm_size = disasmBuf(addr, lookup_size, &result);

	if (disasm_size > 0) {
		size_t counter = 0;
		mach_vm_address_t sub_addr = 0;

		for (size_t i = 0; i < disasm_size; i++) {
			if (result[i].id == ins) {
				sub_addr = result[i].address + addr;
				counter++;
			}

			if (counter == num)
				break;
			else
				sub_addr = 0;
		}

		cs_free(result, disasm_size);
		return sub_addr;
	}

	return 0;
}

mach_vm_address_t Disassembler::disasmSig(mach_vm_address_t addr, evector<DisasmSig *, DisasmSig::deleter> &sig, size_t num, size_t lookup_size) {
	cs_insn *result {nullptr};
	size_t disasm_size = disasmBuf(addr, lookup_size, &result);

	if (disasm_size > 0) {
		size_t counter = 0;
		size_t sig_count = sig.size();
		mach_vm_address_t sub_addr = 0;
		mach_vm_address_t needed_addr = 0;

		for (size_t i = 0; i < disasm_size; i++) {
			bool sig_found = true;
			for (size_t j = 0; j < sig_count; j++) {
				if (i+sig_count > disasm_size) {
					sig_found = false;
					break;
				}

				if (result[i+j].id == sig[j]->ins && (sig[j]->ins != X86_INS_CALL ||
					(sig[j]->ins == X86_INS_CALL && (!sig[j]->sub || (sig[j]->sub && result[i+j].detail->x86.operands[0].type == X86_OP_IMM))))) {
					if (sig[j]->addr || j == 0)
						needed_addr = result[i+j].address;
				} else {
					sig_found = false;
					break;
				}
			}

			if (sig_found) {
				sub_addr = needed_addr + addr;
				counter++;
			}

			if (counter == num) {
				break;
			} else {
				sub_addr = 0;
			}
		}

		cs_free(result, disasm_size);
		return sub_addr;
	}

	return 0;
}

#endif /* LILU_ADVANCED_DISASSEMBLY */
