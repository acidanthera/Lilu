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

#define lilu_os_memcpy  (memcpy)
#define lilu_os_strlcpy (strlcpy)
#define lilu_os_memmove (memmove)

#endif /* kern_compat_hpp */
