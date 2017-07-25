//
//  kern_patcher_private.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_patcher_private_h
#define kern_patcher_private_h

#include <Headers/kern_config.hpp>
#include <Headers/kern_util.hpp>

#include <stdint.h>
#include <sys/types.h>
#include <uuid/uuid.h>
#include <mach/mach_types.h>

// Where are my type_traits :(
template<bool B, class T, class F>
struct conditional { typedef T type; };
template<class T, class F>
struct conditional<false, T, F> { typedef F type; };

namespace Patch {
	enum class Variant {
		U8,
		U16,
		U32,
		U64,
		U128
	};

	template <Variant P>
	using VV =
		typename conditional<P == Variant::U8, uint8_t,
		typename conditional<P == Variant::U16, uint16_t,
		typename conditional<P == Variant::U32, uint32_t,
		typename conditional<P == Variant::U64, uint64_t,
		typename conditional<P == Variant::U128, unsigned __int128, void
	>::type>::type>::type>::type>::type;

	template <typename T>
	static void writeType(mach_vm_address_t addr, T value) {
		DBGLOG("private @ writing to %X value of %lu which is %X", (unsigned int)addr, sizeof(T), (unsigned int)value);
		*reinterpret_cast<T *>(addr) = value;
	}

	template <Variant T>
	struct P {
		const Variant type {T};
		const mach_vm_address_t address;
		const VV<T> original;
		const VV<T> replaced;
		P(mach_vm_address_t addr, VV<T> rep) :
			address(addr), original(*reinterpret_cast<VV<T> *>(addr)), replaced(rep) {}
		P(mach_vm_address_t addr, VV<T> org, VV<T> rep) :
			address(addr), original(org), replaced(rep) {}
		void patch() {
			writeType(address, replaced);
		}
		void restore() {
			writeType(address, original);
		}
	};

	union All {
		All(P<Variant::U8> &&v) : u8(v) {}
		All(P<Variant::U16> &&v) : u16(v) {}
		All(P<Variant::U32> &&v) : u32(v) {}
		All(P<Variant::U64> &&v) : u64(v) {}
		All(P<Variant::U128> &&v) : u128(v) {}
		
		P<Variant::U8> u8;
		P<Variant::U16> u16;
		P<Variant::U32> u32;
		P<Variant::U64> u64;
		P<Variant::U128> u128;
		
		void patch() {
			switch (u8.type) {
				case Variant::U8: return u8.patch();
				case Variant::U16: return u16.patch();
				case Variant::U32: return u32.patch();
				case Variant::U64: return u64.patch();
				case Variant::U128: return u128.patch();
				default: SYSLOG("patcher @ unsupported patch type %d, cannot patch", static_cast<int>(u8.type));
			}
		}
		
		void restore() {
			switch (u8.type) {
				case Variant::U8: return u8.restore();
				case Variant::U16: return u16.restore();
				case Variant::U32: return u32.restore();
				case Variant::U64: return u64.restore();
				case Variant::U128: return u128.restore();
				default: SYSLOG("patcher @ unsupported patch type %d, cannot restore", static_cast<int>(u8.type));
			}
		}
	};
	
	template <Variant T>
	static All *create(mach_vm_address_t addr, VV<T> rep) {
		return new All(P<T>(addr, rep));
	}
	
	template <Variant T>
	static All *create(mach_vm_address_t addr, VV<T> org, VV<T> rep) {
		return new All(P<T>(addr, org, rep));
	}
	
	static void deleter(All *i) {
		delete i;
	}
}

#ifdef KEXTPATCH_SUPPORT

/**
 *  Taken from libkern/libkern/OSKextLibPrivate.h
 */
struct OSKextLoadedKextSummary {
	char        name[KMOD_MAX_NAME];
	uuid_t      uuid;
	uint64_t    address;
	uint64_t    size;
	uint64_t    version;
	uint32_t    loadTag;
	uint32_t    flags;
	uint64_t    reference_list;
};

struct OSKextLoadedKextSummaryHeader {
	uint32_t version;
	uint32_t entry_size;
	uint32_t numSummaries;
	uint32_t reserved; /* explicit alignment for gdb  */
	OSKextLoadedKextSummary summaries[0];
};

#endif /* KEXTPATCH_SUPPORT */

#endif /* kern_patcher_private_h */
