//
//  kern_util.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_util.hpp>

#include <sys/types.h>
#include <libkern/libkern.h>
#include <mach/vm_map.h>

bool ADDPR(debugEnabled) = false;

const char *strstr(const char *stack, const char *needle, size_t len) {
	if (!len && !(len = strlen(needle)))
		return stack;
	
	const char *i = needle;

	while (*stack) {
		if (*stack == *i) {
			i++;
			if (i - needle == len)
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

extern "C" void kern_os_cfree(void *addr) {
	// kern_os_free does not check its argument for nullptr
	if (addr) kern_os_free(addr);
}

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
	if (i) {
		if (i->p)
			vm_deallocate(kernel_map, reinterpret_cast<vm_address_t>(i->p), PAGE_SIZE);
		delete i;
	}
}
