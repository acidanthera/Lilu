//
//  kern_ubsan.h
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#ifndef kern_ubsan_h
#define kern_ubsan_h

// This header provides the necessary macros to facilate XNU compatibility
// with NetBSD UBSAN runtime.

#include <Availability.h>
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <sys/cdefs.h>
#include <sys/buf.h>

// Working in kernel mode.
#ifndef _KERNEL
#define _KERNEL
#endif

// Long double is supported on this target
#ifndef __HAVE_LONG_DOUBLE
#define __HAVE_LONG_DOUBLE
#endif

// XNU does support __RCSID
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(x, s) __RCSID(s)
#endif

// XNU does not export KASSERTS
#ifndef KASSERT
#define KASSERT(exp) do { \
  if (__builtin_expect(!(exp), 0)) \
    (panic)("%s:%d KASSERT failed: %s", __FILE__, __LINE__, #exp); \
} while (0)
#endif

// vpanic is not supported
#ifndef vpanic
#define vpanic(fmt, va) do { \
  char buf[1024]; \
  vsnprintf(buf, sizeof(buf), (fmt), (va)); \
  (panic)("%s:%d VPANIC: %s", __FILE__, __LINE__, buf); \
} while (0)
#endif

// redirect vprintf to IOLog and unify log prints
#ifdef vprintf
#undef vprintf
#endif
void lilu_os_log(const char *format, ...);
#define vprintf(fmt, va) do { \
  char buf[1024]; \
  vsnprintf(buf, sizeof(buf), (fmt), (va)); \
  if (buf[0] == 'U' && buf[1] == 'B' && buf[2] == 'S' && buf[3] == 'a' && buf[4] == 'n' && buf[5] == ':') \
    lilu_os_log("Lilu: ubsan @%s", &buf[6]); \
  else \
    lilu_os_log("Lilu: ubsan @ %s", buf); \
} while (0)

// Bit manipulation is not present (aside an ugly BIT macro in IOFireWire header)
#ifndef __BIT
#define __BIT(__n) \
  (((uintmax_t)(__n) >= NBBY * sizeof(uintmax_t)) ? 0 : \
  ((uintmax_t)1 << (uintmax_t)((__n) & (NBBY * sizeof(uintmax_t) - 1))))
#endif

// Extended bit manipulation is also not present
#ifndef __LOWEST_SET_BIT
/* find least significant bit that is set */
#define __LOWEST_SET_BIT(__mask) ((((__mask) - 1) & (__mask)) ^ (__mask))
#define __SHIFTOUT(__x, __mask) (((__x) & (__mask)) / __LOWEST_SET_BIT(__mask))
#define __SHIFTIN(__x, __mask) ((__x) * __LOWEST_SET_BIT(__mask))
#define __SHIFTOUT_MASK(__mask) __SHIFTOUT((__mask), (__mask))
#endif

// vm_types.h should have ARRAY_COUNT, but it is not exported
#ifndef __arraycount
#define __arraycount(a) (sizeof((a)) / sizeof((a)[0]))
#endif

// Printing macros are not defined by libkern
#ifndef PRIx8
#define PRIx8 "hhx"
#define PRIx16 "hx"
#define PRIx32 "x"
#define PRIx64 "llx"
#define PRId32 "d"
#define PRId64 "lld"
#define PRIu32 "u"
#define PRIu64 "llu"
#endif

#define UBSan Lilu

#endif /* kern_ubsan_h */
