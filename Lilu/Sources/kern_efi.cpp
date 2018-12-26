//
//  kern_efi.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_util.hpp>
#include <Headers/Guid/LiluVariables.h>
#include <Library/LegacyIOService.h>
#include <pexpert/i386/efi.h>
#include <IOKit/IODeviceTreeSupport.h>

#include "kern_efi.hpp"

EfiRuntimeServices *EfiRuntimeServices::instance;

const EFI_GUID EfiRuntimeServices::LiluNormalGuid = LILU_NORMAL_VARIABLE_GUID;
const EFI_GUID EfiRuntimeServices::LiluReadOnlyGuid = LILU_READ_ONLY_VARIABLE_GUID;
const EFI_GUID EfiRuntimeServices::LiluWriteOnlyGuid = LILU_WRITE_ONLY_VARIABLE_GUID;

/**
 * Load registers with these values.
 */
struct pal_efi_registers {
	uint64_t rcx;
	uint64_t rdx;
	uint64_t r8;
	uint64_t r9;
	uint64_t rax;
};

/**
 *  Exported gRT and gST pointers (from Unsupported)
 */
extern void *gPEEFIRuntimeServices;
extern void *gPEEFISystemTable;

/**
 *  EFI call function wrapper
 */
extern "C" void performEfiCallAsm(uint64_t func, pal_efi_registers *efi_reg, void *stack_contents, size_t stack_contents_size);

/**
 *  This is a slightly simplified pal_efi_call_in_64bit_mode function, since it is a private export.
 */
static kern_return_t performEfiCall(uint64_t func, pal_efi_registers *efi_reg, void *stack_contents, size_t stack_contents_size, /* 16-byte multiple */  uint64_t *efi_status) {
	if (func == 0)
		return KERN_INVALID_ADDRESS;

	if (efi_reg == NULL || stack_contents == NULL || stack_contents_size % 16 != 0)
		return KERN_INVALID_ARGUMENT;

	if (!gPEEFISystemTable || !gPEEFIRuntimeServices)
		return KERN_NOT_SUPPORTED;

	performEfiCallAsm(func, efi_reg, stack_contents, stack_contents_size);

	*efi_status = efi_reg->rax;

	return KERN_SUCCESS;
}

void EfiRuntimeServices::activate() {
	EfiRuntimeServices *services = nullptr;
	auto efi = IORegistryEntry::fromPath("/efi", gIODTPlane);
	if (efi) {
		auto abi = OSDynamicCast(OSData, efi->getProperty("firmware-abi"));
		if (abi && abi->isEqualTo("EFI64", sizeof("EFI64")))
			services = new EfiRuntimeServices;
		else
			SYSLOG("efi", "invalid or unsupported firmware abi");
		efi->release();

		if (services) {
			services->accessLock = IOLockAlloc();
			if (services->accessLock) {
				instance = services;
			} else {
				SYSLOG("efi", "failed to allocate efi services lock");
				delete services;
			}
		}

	} else {
		SYSLOG("efi", "missing efi device");
	}
}

EfiRuntimeServices *EfiRuntimeServices::get(bool lock) {
	//TODO: To be completely honest we should lock gAppleEFIRuntimeLock here, but it is not public :/
	// The current approach is that EfiRuntimeServices are only allowed to be used before AppleEFIRuntime is loaded.
	if (instance && lock)
		IOLockLock(instance->accessLock);
	return instance;
}

void EfiRuntimeServices::put() {
	if (instance)
		IOLockUnlock(instance->accessLock);
}

void EfiRuntimeServices::resetSystem(EFI_RESET_TYPE type) {
	uint64_t function = static_cast<EFI_RUNTIME_SERVICES_64 *>(gPEEFIRuntimeServices)->ResetSystem;
	pal_efi_registers regs {};
	regs.rcx = type;
	regs.rdx = EFI_SUCCESS;
	uint8_t stack[48] {};
	uint64_t status = EFI_SUCCESS;
	auto code = performEfiCall(function, &regs, stack, sizeof(stack), &status);
	if (code == KERN_SUCCESS)
		DBGLOG("efi", "successful efi call with response %08llX", status);
	else
		DBGLOG("efi", "efi call failure %d", code);
}

uint64_t EfiRuntimeServices::getVariable(const char16_t *name, const EFI_GUID *guid, uint32_t *attr, uint64_t *size, void *data) {
	uint64_t function = static_cast<EFI_RUNTIME_SERVICES_64 *>(gPEEFIRuntimeServices)->GetVariable;
	pal_efi_registers regs {};
	regs.rcx = reinterpret_cast<uint64_t>(name);
	regs.rdx = reinterpret_cast<uint64_t>(guid);
	regs.r8  = reinterpret_cast<uint64_t>(attr);
	regs.r9  = reinterpret_cast<uint64_t>(size);
	uint64_t stack[6] {0, 0, 0, 0, reinterpret_cast<uint64_t>(data), 0};

	uint64_t status = EFI_SUCCESS;
	auto code = performEfiCall(function, &regs, stack, sizeof(stack), &status);
	if (code == KERN_SUCCESS)
		DBGLOG("efi", "successful efi call GetVariable with response %08llX", status);
	else
		DBGLOG("efi", "efi call GetVariable failure %d", code);

	return status;
}

uint64_t EfiRuntimeServices::setVariable(const char16_t *name, const EFI_GUID *guid, uint32_t attr, uint64_t size, void *data) {
	uint64_t function = static_cast<EFI_RUNTIME_SERVICES_64 *>(gPEEFIRuntimeServices)->SetVariable;
	pal_efi_registers regs {};
	regs.rcx = reinterpret_cast<uint64_t>(name);
	regs.rdx = reinterpret_cast<uint64_t>(guid);
	regs.r8  = attr;
	regs.r9  = size;
	uint64_t stack[6] {0, 0, 0, 0, reinterpret_cast<uint64_t>(data), 0};

	uint64_t status = EFI_SUCCESS;
	auto code = performEfiCall(function, &regs, stack, sizeof(stack), &status);
	if (code == KERN_SUCCESS)
		DBGLOG("efi", "successful efi call SetVariable with response %08llX", status);
	else
		DBGLOG("efi", "efi call SetVariable failure %d", code);

	return status;
}
