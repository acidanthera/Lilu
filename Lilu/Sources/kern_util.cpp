//
//  kern_util.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_util.hpp>
#include <PrivateHeaders/kern_config.hpp>

#include <sys/types.h>
#include <libkern/libkern.h>
#include <mach/vm_map.h>
#include <Availability.h>

bool ADDPR(debugEnabled) = false;
uint32_t ADDPR(debugPrintDelay) = 0;

void lilu_os_log(const char *format, ...) {
	char tmp[1024];
	tmp[0] = '\0';
	va_list va;
	va_start(va, format);
	vsnprintf(tmp, sizeof(tmp), format, va);
	va_end(va);

	if (lilu_get_interrupts_enabled())
		IOLog("%s", tmp);

#ifdef DEBUG
	if (ADDPR(config).debugLock && ADDPR(config).debugBuffer) {
		size_t len = strlen(tmp);
		if (len > 0) {
			IOSimpleLockLock(ADDPR(config).debugLock);
			size_t current = ADDPR(config).debugBufferLength;
			size_t left = Configuration::MaxDebugBufferSize - current;
			if (left > 0) {
				if (len > left) len = left;
				lilu_os_memcpy(ADDPR(config).debugBuffer + current, tmp, len);
				ADDPR(config).debugBufferLength += len;
			}
			IOSimpleLockUnlock(ADDPR(config).debugLock);
		}
	}
#endif

	if (lilu_get_interrupts_enabled() && ADDPR(debugPrintDelay) > 0)
		IOSleep(ADDPR(debugPrintDelay));
}

const char *strstr(const char *stack, const char *needle, size_t len) {
	if (len == 0) {
		len = strlen(needle);
		if (len == 0) return stack;
	}

	const char *i = needle;

	while (*stack) {
		if (*stack == *i) {
			i++;
			if (static_cast<size_t>(i - needle) == len)
				return stack - len + 1;
		} else {
			i = needle;
		}
		stack++;
	}

	return nullptr;
}

char *strrchr(const char *stack, int ch) {
	if (!stack)
		return nullptr;

	char *rtnval = nullptr;
	do {
		if (*stack == ch)
			rtnval = const_cast<char *>(stack);
	} while (*stack++);

	return rtnval;
}

extern "C" void *kern_os_calloc(size_t num, size_t size) {
	return kern_os_malloc(num * size); // malloc bzeroes the buffer
}

extern "C" void lilu_os_free(void *addr) {
	// kern_os_free does not check its argument for nullptr
	if (addr) kern_os_free(addr);
}

#if defined(__i386__)
size_t lilu_strlcpy(char *dst, const char *src, size_t siz) {
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	// Copy as many bytes as will fit.
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0);
	}

	// Not enough room in dst, add null terminator and traverse rest of src.
	if (n == 0) {
		if (siz != 0)
			*d = '\0'; // null terminate dst.
		while (*s++);
	}

	return(s - src - 1); // count does not include null terminator.
}
#endif

bool Page::alloc() {
	if (p && vm_deallocate(kernel_map, reinterpret_cast<vm_address_t>(p), PAGE_SIZE) != KERN_SUCCESS)
		return false;
	return vm_allocate(kernel_map, reinterpret_cast<vm_address_t *>(&p), PAGE_SIZE, VM_FLAGS_ANYWHERE) == KERN_SUCCESS;
}

bool Page::protect(vm_prot_t prot) {
	if (!p) return false;

	return vm_protect(kernel_map, reinterpret_cast<vm_address_t>(p), PAGE_SIZE, FALSE, prot) == KERN_SUCCESS;
}

Page *Page::create() {
	return new Page;
}

void Page::deleter(Page *i) {
	if (i->p)
		vm_deallocate(kernel_map, reinterpret_cast<vm_address_t>(i->p), PAGE_SIZE);
	delete i;
}

/**
 *  Meta Class Definitions
 */
OSDefineMetaClassAndStructors(OSObjectWrapper, OSObject);

/**
 *  Initialize the wrapper with the given object
 *
 *  @param object The wrapped object that is not an `OSObject`
 *  @return `true` on success, `false` otherwise.
 */
bool OSObjectWrapper::init(void *object) {
	if (!super::init())
		return false;
	
	this->object = object;
	return true;
}

/**
 *  Create a wrapper for the given object that is not an `OSObject`
 *
 *  @param object A non-null object
 *  @return A non-null wrapper on success, `nullptr` otherwise.
 *  @warning The caller is responsbile for managing the lifecycle of the given object.
 */
OSObjectWrapper *OSObjectWrapper::with(void *object) {
	auto instance = new OSObjectWrapper;
	if (instance == nullptr)
		return nullptr;
	
	if (!instance->init(object)) {
		instance->release();
		return nullptr;
	}
	
	return instance;
}
