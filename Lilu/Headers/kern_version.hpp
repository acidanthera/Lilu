//
//  kern_version.hpp
//  Lilu
//
//  Copyright © 2016-2020 vit9696. All rights reserved.
//

#ifndef kern_version_hpp
#define kern_version_hpp

#include <Headers/kern_util.hpp>

#include <stdint.h>
#include <sys/types.h>

/**
 *  Slightly non-standard helpers to get the date in a YYYY-MM-DD format.
 */
template <size_t i>
inline constexpr char getBuildYear() {
	static_assert(i < 4, "Year consists of four digits");
	return __DATE__[7+i];
}

template <size_t i>
inline constexpr char getBuildMonth() {
	static_assert(i < 2, "Month consists of two digits");
	auto mon = static_cast<uint32_t>(__DATE__[0])
		| (static_cast<uint32_t>(__DATE__[1]) << 8U)
		| (static_cast<uint32_t>(__DATE__[2]) << 16U)
		| (static_cast<uint32_t>(__DATE__[3]) << 24U);
	switch (mon) {
		case ' naJ':
			return "01"[i];
		case ' beF':
			return "02"[i];
		case ' raM':
			return "03"[i];
		case ' rpA':
			return "04"[i];
		case ' yaM':
			return "05"[i];
		case ' nuJ':
			return "06"[i];
		case ' luJ':
			return "07"[i];
		case ' guA':
			return "08"[i];
		case ' peS':
			return "09"[i];
		case ' tcO':
			return "10"[i];
		case ' voN':
			return "11"[i];
		case ' ceD':
			return "12"[i];
		default:
			return '0';
	}
}

template <size_t i>
inline constexpr char getBuildDay() {
	static_assert(i < 2, "Day consists of two digits");
	if (i == 0 && __DATE__[4+i] == ' ')
		return '0';
	return __DATE__[4+i];
}

#if !defined(LILU_CUSTOM_KMOD_INIT) || !defined(LILU_CUSTOM_IOKIT_INIT) || defined(LILU_USE_KEXT_VERSION)

static const char kextVersion[] {
#ifdef DEBUG
	'D', 'B', 'G', '-',
#else
	'R', 'E', 'L', '-',
#endif
	xStringify(MODULE_VERSION)[0], xStringify(MODULE_VERSION)[2], xStringify(MODULE_VERSION)[4], '-',
	getBuildYear<0>(), getBuildYear<1>(), getBuildYear<2>(), getBuildYear<3>(), '-',
	getBuildMonth<0>(), getBuildMonth<1>(), '-', getBuildDay<0>(), getBuildDay<1>(), '\0'
};

#endif

#endif /* kern_version_hpp */
