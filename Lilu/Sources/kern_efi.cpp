//
//  kern_efi.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_util.hpp>
#include <IOKit/IOService.h>
#include <pexpert/i386/efi.h>
#include <IOKit/IODeviceTreeSupport.h>

#include "kern_efi.hpp"

EfiRuntimeServices *EfiRuntimeServices::instance;

//
// 4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102
// This GUID is specifically used for normal variable access by Lilu kernel extension and its plugins.
//
#define OC_VENDOR_VARIABLE_GUID \
  { 0x4D1FDA02, 0x38C7, 0x4A6A, { 0x9C, 0xC6, 0x4B, 0xCC, 0xA8, 0xB3, 0x01, 0x02 } }

//
// E09B9297-7928-4440-9AAB-D1F8536FBF0A
// This GUID is specifically used for reading variables by Lilu kernel extension and its plugins.
// Any writes to this GUID should be prohibited via EFI_RUNTIME_SERVICES after EXIT_BOOT_SERVICES.
// The expected return code on variable write is EFI_SECURITY_VIOLATION.
//
#define OC_READ_ONLY_VARIABLE_GUID \
  { 0xE09B9297, 0x7928, 0x4440, { 0x9A, 0xAB, 0xD1, 0xF8, 0x53, 0x6F, 0xBF, 0x0A } }

//
// F0B9AF8F-2222-4840-8A37-ECF7CC8C12E1
// This GUID is specifically used for reading variables by Lilu and plugins.
// Any reads from this GUID should be prohibited via EFI_RUNTIME_SERVICES after EXIT_BOOT_SERVICES.
// The expected return code on variable read is EFI_SECURITY_VIOLATION.
//
#define OC_WRITE_ONLY_VARIABLE_GUID \
  { 0xF0B9AF8F, 0x2222, 0x4840, { 0x8A, 0x37, 0xEC, 0xF7, 0xCC, 0x8C, 0x12, 0xE1 } }

const EFI_GUID EfiRuntimeServices::LiluVendorGuid = OC_VENDOR_VARIABLE_GUID;
const EFI_GUID EfiRuntimeServices::LiluReadOnlyGuid = OC_READ_ONLY_VARIABLE_GUID;
const EFI_GUID EfiRuntimeServices::LiluWriteOnlyGuid = OC_WRITE_ONLY_VARIABLE_GUID;

/**
 * Load registers with these values. In 32-bit mode,
 * only the low-order half is loaded (if applicable)
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
 *  gRT is not available in 10.4 and cannot be used in 32-bit builds
 */
#if defined(__x86_64__)
extern void *gPEEFIRuntimeServices;
#endif
extern void *gPEEFISystemTable;


#if defined(__i386__)
/**
 *  EFI 32-bit call function wrapper
 */
extern "C" void performEfiCallAsm32(uint32_t func, pal_efi_registers *efi_reg, void *stack_contents, size_t stack_contents_size);

/**
 *  This is a slightly simplified pal_efi_call_in_32bit_mode function, since it is a private export.
 */
static kern_return_t performEfiCall32(uint32_t func, pal_efi_registers *efi_reg, void *stack_contents, size_t stack_contents_size, /* 16-byte multiple */  uint32_t *efi_status) {
	if (func == 0)
		return KERN_INVALID_ADDRESS;

	if (efi_reg == NULL || stack_contents == NULL || stack_contents_size % 16 != 0)
		return KERN_INVALID_ARGUMENT;

	if (!gPEEFISystemTable)
		return KERN_NOT_SUPPORTED;

	performEfiCallAsm32(func, efi_reg, stack_contents, stack_contents_size);
	*efi_status = (uint32_t) efi_reg->rax;

	return KERN_SUCCESS;
}
#endif

/**
 *  EFI 64-bit call function wrapper
 */
extern "C" void performEfiCallAsm64(uint64_t func, pal_efi_registers *efi_reg, void *stack_contents, size_t stack_contents_size);

/**
 *  This is a slightly simplified pal_efi_call_in_64bit_mode function, since it is a private export.
 */
static kern_return_t performEfiCall64(uint64_t func, pal_efi_registers *efi_reg, void *stack_contents, size_t stack_contents_size, /* 16-byte multiple */  uint64_t *efi_status) {
	if (func == 0)
		return KERN_INVALID_ADDRESS;

	if (efi_reg == NULL || stack_contents == NULL || stack_contents_size % 16 != 0)
		return KERN_INVALID_ARGUMENT;

	if (!gPEEFISystemTable)
		return KERN_NOT_SUPPORTED;

	performEfiCallAsm64(func, efi_reg, stack_contents, stack_contents_size);
	*efi_status = efi_reg->rax;

	return KERN_SUCCESS;
}

void EfiRuntimeServices::setRuntimeServices() {
#if defined(__i386__)
	if (is32BitEFI) {
		auto efiSystemTable = static_cast<EFI_SYSTEM_TABLE_32 *>(gPEEFISystemTable);
		efiRuntimeServices = reinterpret_cast<EFI_RUNTIME_SERVICES_32 *>(efiSystemTable->RuntimeServices);
	} else {
		auto efiSystemTable = static_cast<EFI_SYSTEM_TABLE_64 *>(gPEEFISystemTable);
		efiRuntimeServices = reinterpret_cast<EFI_RUNTIME_SERVICES_64 *>(efiSystemTable->RuntimeServices);
	}
			
#elif defined(__x86_64__)
	efiRuntimeServices = gPEEFIRuntimeServices;

#else
#error Unsupported arch
#endif
}

void EfiRuntimeServices::activate() {
	EfiRuntimeServices *services = nullptr;
	auto efi = IORegistryEntry::fromPath("/efi", gIODTPlane);
	if (efi) {
		auto abi = OSDynamicCast(OSData, efi->getProperty("firmware-abi"));
		if (abi && abi->isEqualTo("EFI64", sizeof("EFI64"))) {
			services = new EfiRuntimeServices;
			services->is32BitEFI = false;
			services->setRuntimeServices();
			
#if defined(__i386__)
		} else if (abi && abi->isEqualTo("EFI32", sizeof("EFI32"))) {
			services = new EfiRuntimeServices;
			services->is32BitEFI = true;
			services->setRuntimeServices();
#endif
			
		} else {
			SYSLOG("efi", "invalid or unsupported firmware abi");
		}
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
	uint64_t function =
#if defined(__i386__)
		is32BitEFI ?
		static_cast<EFI_RUNTIME_SERVICES_32 *>(efiRuntimeServices)->ResetSystem :
#endif
		static_cast<EFI_RUNTIME_SERVICES_64 *>(efiRuntimeServices)->ResetSystem;

	pal_efi_registers regs {};
	regs.rcx = type;
	regs.rdx = EFI_SUCCESS;
	uint8_t stack[48] {};

	uint64_t status = EFI_SUCCESS;
	auto code =
#if defined(__i386__)
		is32BitEFI ?
		performEfiCall32((uint32_t)function, &regs, stack, sizeof(stack), (uint32_t *)&status) :
#endif
		performEfiCall64(function, &regs, stack, sizeof(stack), &status);

	if (code == KERN_SUCCESS)
		DBGLOG("efi", "successful %s call with response %08llX", is32BitEFI ? "efi32" : "efi64", status);
	else
		DBGLOG("efi", "%s call failure %d", is32BitEFI ? "efi32" : "efi64", code);
}

uint64_t EfiRuntimeServices::getVariable(const char16_t *name, const EFI_GUID *guid, uint32_t *attr, uint64_t *size, void *data) {
	uint64_t function =
#if defined(__i386__)
		is32BitEFI ?
		static_cast<EFI_RUNTIME_SERVICES_32 *>(efiRuntimeServices)->GetVariable :
#endif
		static_cast<EFI_RUNTIME_SERVICES_64 *>(efiRuntimeServices)->GetVariable;
	
	pal_efi_registers regs {};
	regs.rcx = reinterpret_cast<uint64_t>(name);
	regs.rdx = reinterpret_cast<uint64_t>(guid);
	regs.r8  = reinterpret_cast<uint64_t>(attr);
	regs.r9  = reinterpret_cast<uint64_t>(size);
	uint64_t stack[6] {0, 0, 0, 0, reinterpret_cast<uint64_t>(data), 0};

	uint64_t status = EFI_SUCCESS;
	auto code =
#if defined(__i386__)
		is32BitEFI ?
		performEfiCall32((uint32_t)function, &regs, stack, sizeof(stack), (uint32_t *)&status) :
#endif
		performEfiCall64(function, &regs, stack, sizeof(stack), &status);

	if (code == KERN_SUCCESS)
		DBGLOG("efi", "successful %s call GetVariable with response %08llX", is32BitEFI ? "efi32" : "efi64", status);
	else
		DBGLOG("efi", "%s call GetVariable failure %d", is32BitEFI ? "efi32" : "efi64", code);

	return status;
}

uint64_t EfiRuntimeServices::setVariable(const char16_t *name, const EFI_GUID *guid, uint32_t attr, uint64_t size, void *data) {
	uint64_t function =
#if defined(__i386__)
		is32BitEFI ?
		static_cast<EFI_RUNTIME_SERVICES_32 *>(efiRuntimeServices)->SetVariable :
#endif
		static_cast<EFI_RUNTIME_SERVICES_64 *>(efiRuntimeServices)->SetVariable;
	
	pal_efi_registers regs {};
	regs.rcx = reinterpret_cast<uint64_t>(name);
	regs.rdx = reinterpret_cast<uint64_t>(guid);
	regs.r8  = attr;
	regs.r9  = size;
	uint64_t stack[6] {0, 0, 0, 0, reinterpret_cast<uint64_t>(data), 0};

	uint64_t status = EFI_SUCCESS;
	auto code =
#if defined(__i386__)
		is32BitEFI ?
		performEfiCall32((uint32_t)function, &regs, stack, sizeof(stack), (uint32_t *)&status) :
#endif
		performEfiCall64(function, &regs, stack, sizeof(stack), &status);

	if (code == KERN_SUCCESS)
		DBGLOG("efi", "successful %s call SetVariable with response %08llX", is32BitEFI ? "efi32" : "efi64", status);
	else
		DBGLOG("efi", "%s call SetVariable failure %d", is32BitEFI ? "efi32" : "efi64", code);

	return status;
}
