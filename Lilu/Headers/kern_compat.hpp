//
//  kern_compat.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_compat_hpp
#define kern_compat_hpp

#include <Headers/kern_config.hpp>

// Legacy compatibility layer created to avoid 10.13 SDK macros
// unsupported in older systems and improperly guarded due to
// Availability.h header not being. Currently these macros
// are left to avoid compilation errors.
#define lilu_os_memcpy  memcpy
#define lilu_os_memmove memmove
#define lilu_os_strncpy strncpy
#define lilu_os_strncat strncat
#define lilu_os_strlcat strlcat
#define lilu_os_strlcpy strlcpy
#define lilu_os_strcat  strcat
#define lilu_os_bcopy   bcopy

// This may not be nice but will protect users from changes in KernInfo strcture.
#ifndef LILU_DISABLE_BRACE_WARNINGS
#pragma clang diagnostic error "-Wmissing-braces"
#endif

#endif /* kern_compat_hpp */
