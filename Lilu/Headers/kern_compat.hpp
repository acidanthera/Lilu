//
//  kern_compat.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_compat_hpp
#define kern_compat_hpp

#include <Headers/kern_config.hpp>
#include <Availability.h>
#include <string.h>

// Please do not use memcpy and similar functions, since they compile
// to macros unsupported by any other system with 10.13 SDK unless
// Availability.h header is included.
#define lilu_os_memcpy(dest, src, len)  (memcpy)((dest), (src), (len))
#define lilu_os_memmove(dest, src, len) (memmove)((dest), (src), (len))
#define lilu_os_strncpy(dest, src, len) (strncpy)((dest), (src), (len))
#define lilu_os_strncat(dest, src, len) (strncat)((dest), (src), (len))
#define lilu_os_strlcat(dest, src, len) (strlcat)((dest), (src), (len))
#define lilu_os_strlcpy(dest, src, len) (strlcpy)((dest), (src), (len))
#define lilu_os_strcat(dest, src)       (strcat)((dest), (src))
#define lilu_os_bcopy(dest, src, len)   (bcopy)((dest), (src), (len))

// Additionally disallow the use of the original functions
#ifndef LILU_DISABLE_MEMFUNC_REDEFINE

#ifdef memcpy
#undef memcpy
#define memcpy(dest, src, len) _Pragma("GCC error \"Avoid memcpy due to 10.13 SDK bugs!\"")
#endif

#ifdef memmove
#undef memmove
#define memmove(dest, src, len) _Pragma("GCC error \"Avoid memmove due to 10.13 SDK bugs!\"")
#endif

#ifdef strncpy
#undef strncpy
#define strncpy(dest, src, len) _Pragma("GCC error \"Avoid strncpy due to 10.13 SDK bugs!\"")
#endif

#ifdef strncat
#undef strncat
#define strncat(dest, src, len) _Pragma("GCC error \"Avoid strncat due to 10.13 SDK bugs!\"")
#endif

#ifdef strlcat
#undef strlcat
#define strlcat(dest, src, len) _Pragma("GCC error \"Avoid strlcat due to 10.13 SDK bugs!\"")
#endif

#ifdef strlcpy
#undef strlcpy
#define strlcpy(dest, src, len) _Pragma("GCC error \"Avoid strlcpy due to 10.13 SDK bugs!\"")
#endif

#ifdef strcat
#undef strcat
#define strcat(dest, src) _Pragma("GCC error \"Avoid strcat due to 10.13 SDK bugs!\"")
#endif

#ifdef bcopy
#undef bcopy
#define bcopy(dest, src, len) _Pragma("GCC error \"Avoid bcopy due to 10.13 SDK bugs!\"")
#endif

#endif /* LILU_DISABLE_MEMFUNC_REDEFINE */

// This may not be nice but will protect users from changes in KernInfo strcture.
#ifndef LILU_DISABLE_BRACE_WARNINGS
#pragma clang diagnostic error "-Wmissing-braces"
#endif

#endif /* kern_compat_hpp */
