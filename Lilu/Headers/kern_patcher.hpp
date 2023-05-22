//
//  kern_patcher.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_patcher_hpp
#define kern_patcher_hpp

#include <Headers/kern_config.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_mach.hpp>
#include <Headers/kern_disasm.hpp>
#include <Headers/kern_nvram.hpp>

#include <mach/mach_types.h>

namespace Patch { union All; void deleter(All * NONNULL); }
#ifdef LILU_KEXTPATCH_SUPPORT
union OSKextLoadedKextSummaryHeaderAny;
#endif /* LILU_KEXTPATCH_SUPPORT */

#ifdef LILU_KCINJECT_SUPPORT
#include <IOKit/IOMemoryDescriptor.h>

/**
 *  Taken from pexpert/pexpert/pexpert.h
 */
typedef enum kc_kind {
	KCKindNone      = -1,
	KCKindUnknown   = 0,
	KCKindPrimary   = 1,
	KCKindPageable  = 2,
	KCKindAuxiliary = 3,
	KCNumKinds      = 4,
} kc_kind_t;

/**
 *  Taken from osfmk/mach/vm_statistics.h
 */
typedef struct {
	unsigned int
	    vmkf_atomic_entry:1,
	    vmkf_permanent:1,
	    vmkf_guard_after:1,
	    vmkf_guard_before:1,
	    vmkf_submap:1,
	    vmkf_already:1,
	    vmkf_beyond_max:1,
	    vmkf_no_pmap_check:1,
	    vmkf_map_jit:1,
	    vmkf_iokit_acct:1,
	    vmkf_keep_map_locked:1,
	    vmkf_fourk:1,
	    vmkf_overwrite_immutable:1,
	    vmkf_remap_prot_copy:1,
	    vmkf_cs_enforcement_override:1,
	    vmkf_cs_enforcement:1,
	    vmkf_nested_pmap:1,
	    vmkf_no_copy_on_read:1,
	    vmkf_32bit_map_va:1,
	    vmkf_copy_single_object:1,
	    vmkf_copy_pageable:1,
	    vmkf_copy_same_map:1,
	    vmkf_translated_allow_execute:1,
	    __vmkf_unused:9;
} vm_map_kernel_flags_t;

typedef uint16_t vm_tag_t;


typedef struct {
	uint8_t HeaderVersion;
	uint32_t HeaderSize;
	uint32_t SymbolCount;
} PACKED LILU_PRELINKED_SYMBOLS_HEADER;

typedef struct {
	uint64_t SymbolValue;
	uint32_t EntryLength;
	uint32_t SymbolNameLength;
	char SymbolName[];
} PACKED LILU_PRELINKED_SYMBOLS_ENTRY;

typedef struct {
	LILU_PRELINKED_SYMBOLS_HEADER Header;
	LILU_PRELINKED_SYMBOLS_ENTRY Entries[];
} PACKED LILU_PRELINKED_SYMBOLS;


typedef struct {
	uint8_t Version;
	uint32_t EntryLength;
	uint8_t KCKind;
	char BundlePath[128];
	uint32_t InfoPlistOffset;
	uint32_t InfoPlistSize;
	char ExecutablePath[512];
	uint32_t ExecutableOffset;
	uint32_t ExecutableSize;
} PACKED LILU_INJECTION_INFO;


typedef struct {
	uint8_t Version;
	uint32_t Size;
	uint32_t KextCount;
} PACKED LILU_BLOCK_INFO_HEADER;

typedef struct {
	char Identifier[128];
	bool Exclude;
	uint8_t KCKind;
} PACKED LILU_BLOCK_INFO_ENTRY;

typedef struct {
	LILU_BLOCK_INFO_HEADER Header;
	LILU_BLOCK_INFO_ENTRY Entries[];
} PACKED LILU_BLOCK_INFO;

// The maximize size of LILU_BLOCK_INFO allowed on version 0
#define LILU_BLOCK_INFO_SIZE_LIMIT_VERSION_0 16384

typedef struct {
	uint32_t Magic;
	uint32_t KextCount;
	uint64_t PrelinkedSymbolsAddr;
	uint64_t BlockInfoAddr;
} PACKED LILU_INFO;

// The magic header of LILU_INFO
#define LILU_INFO_MAGIC  0xC4EF7155
#endif /* LILU_KCINJECT_SUPPORT */

class KernelPatcher {
public:

	/**
	 *  Errors set by functions
	 */
	enum class Error {
		NoError,
		NoKinfoFound,
		NoSymbolFound,
		KernInitFailure,
		KernRunningInitFailure,
		KextListeningFailure,
		DisasmFailure,
		MemoryIssue,
		MemoryProtection,
		PointerRange,
		AlreadyDone,
		LockError,
		Unsupported,
		InvalidSymbolFound
	};

	/**
	 *  Get last error
	 *
	 *  @return error code
	 */
	EXPORT Error getError();

	/**
	 *  Reset all the previous errors
	 */
	EXPORT void clearError();

	/**
	 *  Initialise KernelPatcher, prepare for modifications
	 */
	void init();

	/**
	 *  Deinitialise KernelPatcher, must be called regardless of the init error
	 */
	void deinit();

	/**
	 *  Kernel write lock used for performing kernel & kext writes to disable cpu preemption
	 *  See MachInfo::setKernelWriting
	 */
	EXPORT static IOSimpleLock *kernelWriteLock;

	/**
	 *  Kext information
	 */
	struct KextInfo;

#ifdef LILU_KEXTPATCH_SUPPORT
	struct KextInfo {
		static constexpr size_t Unloaded {0};
		enum SysFlags : uint64_t {
			Loaded,      // invoke for kext if it is already loaded
			Reloadable,  // allow the kext to unload and get patched again
			Disabled,    // do not load this kext (formerly achieved pathNum = 0, this no longer works)
			FSOnly,      // do not use prelinkedkernel (kextcache) as a symbol source
			FSFallback,  // perform fs fallback if kextcache failed
			Reserved,
			SysFlagNum,
		};
		static constexpr uint64_t UserFlagNum {sizeof(uint64_t)-SysFlagNum};
		static_assert(UserFlagNum > 0, "There should be at least one user flag");
		const char *id {nullptr};
		const char **paths {nullptr};
		size_t pathNum {0};
		bool sys[SysFlagNum] {};
		bool user[UserFlagNum] {};
		size_t loadIndex {Unloaded}; // Updated after loading

		/**
		 *  Disable this info from being used
		 *  May be called from onPatcherLoad callbacks to disable certain kexts
		 */
		void switchOff() {
			sys[KernelPatcher::KextInfo::Disabled] = true;
		}
	};

	static_assert(sizeof(KextInfo) == 4 * sizeof(size_t) + sizeof(uint64_t), "KextInfo is no longer ABI compatible");
#endif /* LILU_KEXTPATCH_SUPPORT */

	/**
	 *  Loads and stores kinfo information locally
	 *
	 *  @param id         kernel item identifier
	 *  @param paths      item filesystem path array
	 *  @param num        number of path entries
	 *  @param isKernel   kinfo is kernel info
	 *  @param fsonly     avoid using prelinkedkernel for kexts
	 *  @param fsfallback fallback to reading from filesystem if prelink failed
	 *
	 *  @return loaded kinfo id
	 */
	EXPORT size_t loadKinfo(const char *id, const char * const paths[], size_t num=1, bool isKernel=false, bool fsonly=false, bool fsfallback=false);

#ifdef LILU_KEXTPATCH_SUPPORT
	/**
	 *  Loads and stores kinfo information locally
	 *
	 *  @param info kext to load, updated on success
	 *
	 *  @return loaded kinfo id
	 */
	EXPORT size_t loadKinfo(KextInfo *info);
#endif /* LILU_KEXTPATCH_SUPPORT */

	/**
	 *  Kernel kinfo id
	 */
	static constexpr size_t KernelID {0};

	/**
	 *  Update running information
	 *
	 *  @param id    loaded kinfo id
	 *  @param slide loaded slide
	 *  @param size  loaded memory size
	 *  @param force force recalculatiob
	 *
	 *  @return new size
	 */
	EXPORT size_t updateRunningInfo(size_t id, mach_vm_address_t slide=0, size_t size=0, bool force=false);

	/**
	 *  Any kernel
	 */
	static constexpr uint32_t KernelAny {0};

	/**
	 *  Check kernel compatibility
	 *
	 *  @param min minimal requested version or KernelAny
	 *  @param max maximum supported version or KernelAny
	 *
	 *  @return true on success
	 */
	EXPORT static bool compatibleKernel(uint32_t min, uint32_t max);

	/**
	 *  Erase coverage instruction prefix (like inc qword ptr[]), that causes function routing to fail
	 *
	 *  @param addr   address to valid instruction code
	 *  @param count  amount of instructions to inspect
	 */
	EXPORT void eraseCoverageInstPrefix(mach_vm_address_t addr, size_t count=5);

	/**
	 *  Erase coverage instruction prefix (like inc qword ptr[]), that causes function routing to fail
	 *
	 *  @param addr   address to valid instruction code
	 *  @param count  amount of instructions to inspect
	 *  @param limit  amount of bytes to inspect
	 */
	EXPORT void eraseCoverageInstPrefix(mach_vm_address_t addr, size_t count, off_t limit);

	/**
	 *  Solve a kinfo symbol
	 *
	 *  @param id      loaded kinfo id
	 *  @param symbol  symbol to solve
	 *
	 *  @return running symbol address or 0
	 */
	EXPORT mach_vm_address_t solveSymbol(size_t id, const char *symbol);

	/**
	 *  Solve a kinfo symbol in range with designated type
	 *
	 *  @param id      loaded kinfo id
	 *  @param symbol  symbol to solve
	 *  @param start   start address range
	 *  @param size    address range size
	 *  @param crash   kernel panic on invalid non-zero address
	 *
	 *  @return running symbol address or 0 casted to type T (mach_vm_address_t)
	 */
	template <typename T = mach_vm_address_t>
	inline T solveSymbol(size_t id, const char *symbol, mach_vm_address_t start, size_t size, bool crash=false) {
		auto addr = solveSymbol(id, symbol);
		if (addr) {
			if (addr >= start && addr < start + size)
				return (T)addr;

			code = Error::InvalidSymbolFound;
			SYSTRACE("patcher", "address " PRIKADDR " is out of range " PRIKADDR " with size %lX",
				CASTKADDR(addr), CASTKADDR(start), size);

			PANIC_COND(crash, "patcher", "address " PRIKADDR " is out of range " PRIKADDR " with size %lX",
				CASTKADDR(addr), CASTKADDR(start), size);
		}

		return (T)nullptr;
	}

    /**
     *  Solve request to resolve multiple symbols in one shot and simplify error handling
     *
     *  @seealso solveMultiple().
     */
    struct SolveRequest {
        /**
         *  The symbol to solve
         */
        const char *symbol {nullptr};

        /**
         *  The symbol address on success, otherwise NULL.
         */
        mach_vm_address_t *address {nullptr};

        /**
         *  Construct a solve request conveniently
         */
        template <typename T>
        SolveRequest(const char *s, T &addr) :
			symbol(s), address(reinterpret_cast<mach_vm_address_t*>(&addr)) { }
    };

	/**
	 *  Solve multiple functions with basic error handling
	 *
	 *  @param id        loaded kinfo id
	 *  @param requests  an array of requests to solve
	 *  @param num       requests array size
	 *  @param start     start address range
	 *  @param size      address range size
	 *  @param crash     kernel panic on invalid non-zero address
	 *  @param force     continue on first error
	 *
	 *  @return false if at least one symbol cannot be solved.
	 */
	inline bool solveMultiple(size_t id, SolveRequest *requests, size_t num, mach_vm_address_t start, size_t size, bool crash=false, bool force=false) {
		for (size_t index = 0; index < num; index++) {
			auto result = solveSymbol(id, requests[index].symbol, start, size, crash);
			if (result) {
				*requests[index].address = result;
			} else {
				clearError();
				if (!force) return false;
			}
		}
		return true;
	}

	/**
	 *  Solve multiple functions with basic error handling
	 *
	 *  @param id        loaded kinfo id
	 *  @param requests  an array of requests to solve
	 *  @param start     start address range
	 *  @param size      address range size
	 *  @param crash     kernel panic on invalid non-zero address
	 *  @param force     continue on first error
	 *
	 *  @return false if at least one symbol cannot be solved.
	 */
	template <size_t N>
	inline bool solveMultiple(size_t id, SolveRequest (&requests)[N], mach_vm_address_t start, size_t size, bool crash=false, bool force=false) {
		return solveMultiple(id, requests, N, start, size, crash, force);
	}

	/**
	 *  Hook kext loading and unloading to access kexts at early stage
	 */
	EXPORT void setupKextListening();

	/**
	 *  Free file buffer resources and effectively make prelinked kext loading impossible
	 */
	void freeFileBufferResources();

	/**
	 *  Activates monitoring functions if necessary
	 */
	void activate();

	/**
	 *  Load handling structure
	 */
	class KextHandler {
		using t_handler = void (*)(KextHandler *);
		KextHandler(const char * const i, size_t idx, t_handler h, bool l, bool r) :
			id(i), index(idx), handler(h), loaded(l), reloadable(r) {}
	public:
		static KextHandler *create(const char * const i, size_t idx, t_handler h, bool l=false, bool r=false) {
			return new KextHandler(i, idx, h, l, r);
		}
		static void deleter(KextHandler *i NONNULL) {
			delete i;
		}

		void *self {nullptr};
		const char * const id {nullptr};
		size_t index {0};
		mach_vm_address_t address {0};
		size_t size {0};
		t_handler handler {nullptr};
		bool loaded {false};
		bool reloadable {false};
	};

#ifdef LILU_KEXTPATCH_SUPPORT
	/**
	 *  Enqueue handler processing at kext loading
	 *
	 *  @param handler  handler to process
	 */
	EXPORT void waitOnKext(KextHandler *handler);

	/**
	 *  Update kext handler features
	 *
	 *  @param info  loaded kext info with features
	 */
	void updateKextHandlerFeatures(KextInfo *info);

	/**
	 *  Arbitrary kext find/replace patch
	 */
	struct LookupPatch {
		KextInfo *kext;
		const uint8_t *find;
		const uint8_t *replace;
		size_t size;
		size_t count;
	};

	/**
	 *  Apply a find/replace patch
	 *
	 *  @param patch patch to apply
	 */
	EXPORT void applyLookupPatch(const LookupPatch *patch);

	/**
	 *  Apply a find/replace patch with additional constraints
	 *
	 *  @param patch              patch to apply
	 *  @param startingAddress    start with this address (or kext/kernel lowest address)
	 *  @param maxSize            maximum size to lookup (or kext/kernel max size)
	 */
	EXPORT void applyLookupPatch(const LookupPatch *patch, uint8_t *startingAddress, size_t maxSize);
#endif /* LILU_KEXTPATCH_SUPPORT */

#ifdef LILU_KCINJECT_SUPPORT
	/**
	 *  Hook KC FileSet loading and access functions to allow injecting into SysKC/AuxKC
	 */
	EXPORT void setupKCListening();

	/**
	 *  A pointer to OSKext::loadKCFileSet()
	 */
	mach_vm_address_t orgOSKextLoadKCFileSet {};

	/**
	 *  Called at KC FileSet loading if KC listening is enabled
	 */
	static OSReturn onOSKextLoadKCFileSet(const char *filepath, kc_kind_t type);

	/**
	 *  A pointer to ubc_getobject_from_filename()
	 */
	mach_vm_address_t orgUbcGetobjectFromFilename {};

	/**
	 *  Called during KC FileSet loading if KC listening is enabled
	 */
	static void * onUbcGetobjectFromFilename(const char *filename, struct vnode **vpp, off_t *file_size);

	/**
	 *  A pointer to vm_map_enter_mem_object_control()
	 */
	mach_vm_address_t orgVmMapEnterMemObjectControl {};

	/**
	 *  Called during KC FileSet content access if KC listening is enabled
	 */
	static kern_return_t onVmMapEnterMemObjectControlLegacy(
		vm_map_t                target_map,
		vm_map_offset_t         *address,
		vm_map_size_t           initial_size,
		vm_map_offset_t         mask,
		int                     flags,
		vm_map_kernel_flags_t   vmk_flags,
		vm_tag_t                tag,
		memory_object_control_t control,
		vm_object_offset_t      offset,
		boolean_t               copy,
		vm_prot_t               cur_protection,
		vm_prot_t               max_protection,
		vm_inherit_t            inheritance);

	static kern_return_t onVmMapEnterMemObjectControlVer22Point4(
		vm_map_t                target_map,
		vm_map_offset_t         *address,
		vm_map_size_t           initial_size,
		vm_map_offset_t         mask,
		vm_map_kernel_flags_t   vmk_flags,
		memory_object_control_t control,
		vm_object_offset_t      offset,
		boolean_t               copy,
		vm_prot_t               cur_protection,
		vm_prot_t               max_protection,
		vm_inherit_t            inheritance);

	static void onVmMapEnterMemObjectControlPreCall(
		vm_map_t                target_map,
		memory_object_control_t control,
		vm_map_size_t           initial_size,
		kc_kind                 &kcKind,
		vm_object_offset_t      &offset,
		vm_object_offset_t      &realOffset,
		bool                    &doOverride,
		bool                    &rangeOverlaps);

	static void onVmMapEnterMemObjectControlPostCall(
		vm_map_offset_t         *address,
		vm_map_size_t           initial_size,
		kc_kind                 kcKind,
		vm_object_offset_t      realOffset,
		bool                    doOverride
	);

	/**
	 *  Initialize kcSymbols, kcInjectInfos, and kcBlockInfos with info from OpenCore
	 */
	bool fetchInfoFromOpenCore();

	/**
	 *  Initialize kcSymbols with info from OpenCore
	 */
	bool fetchPrelinkedSymbolsFromOpenCore(uint64_t prelinkedSymbolsAddr);

	/**
	 *  Initialize kcInjectInfos with info from OpenCore
	 */
	bool fetchInjectionInfoFromOpenCore(NVStorage *nvram, uint32_t liluKextCount);

	/**
	 *  Initialize kcBlockInfos with info from OpenCore
	 */
	bool fetchBlockInfoFromOpenCore(uint64_t liluBlockInfoAddr);
#endif /* LILU_KCINJECT_SUPPORT */

	/**
	 *  Route function to function
	 *
	 *  @param from         function to route
	 *  @param to           routed function
	 *  @param buildWrapper create entrance wrapper
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param revertible   patches could be reverted
	 *
	 *  @return wrapper pointer or 0 on success
	 */
	EXPORT mach_vm_address_t routeFunction(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper=false, bool kernelRoute=true, bool revertible=true) DEPRECATE("Use routeMultiple where possible");

	/**
	 *  Route function to function with long jump
	 *
	 *  @param from         function to route
	 *  @param to           routed function
	 *  @param buildWrapper create entrance wrapper
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param revertible   patches could be reverted
	 *
	 *  @return wrapper pointer or 0 on success
	 */
	EXPORT mach_vm_address_t routeFunctionLong(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper=false, bool kernelRoute=true, bool revertible=true) DEPRECATE("Use routeMultiple where possible");

	/**
	 *  Route function to function with short jump
	 *
	 *  @param from         function to route
	 *  @param to           routed function
	 *  @param buildWrapper create entrance wrapper
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param revertible   patches could be reverted
	 *
	 *  @return wrapper pointer or 0 on success
	 */
	EXPORT mach_vm_address_t routeFunctionShort(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper=false, bool kernelRoute=true, bool revertible=true) DEPRECATE("Use routeMultiple where possible");

	/**
	 *  Route block at assembly level
	 *
	 *  @param from         address to route
	 *  @param opcodes      opcodes to insert
	 *  @param opnum        number of opcodes
	 *  @param buildWrapper create entrance wrapper
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *
	 *  @return wrapper pointer or 0 on success
	 */
	EXPORT mach_vm_address_t routeBlock(mach_vm_address_t from, const uint8_t *opcodes, size_t opnum, bool buildWrapper=false, bool kernelRoute=true);

	/**
	 *  Route virtual function to function
	 *
	 *  @param obj      OSObject-compatible instance
	 *  @param off      function offset in a virtual table (arch-neutral, i.e. divided by sizeof(uintptr_t)
	 *  @param func     function to replace with
	 *  @param orgFunc  pointer to store the original function
	 *
	 *  @return true on success
	 */
	template <typename T>
	static inline bool routeVirtual(void *obj, size_t off, T func, T *orgFunc=nullptr) {
		// First OSObject (and similar) field is its virtual table.
		auto vt = obj ? reinterpret_cast<T **>(obj)[0] : nullptr;
		if (vt) {
			// Do not try to replace twice!
			if (vt[off] == func)
				return false;
			if (orgFunc) *orgFunc = vt[off];
			vt[off] = func;
			return true;
		}
		return false;
	}

	/**
	 *  Route request to simplify casting and error handling
	 *  See routeMultiple.
	 *
	 *  symbol  symbol to lookup
	 *  from    solved symbol (assigned by routeMultiple)
	 *  to      destination address
	 *  org     trampoline storage to the original symbol
	 */
	struct RouteRequest {
		const char *symbol {nullptr};
		mach_vm_address_t from {0};
		const mach_vm_address_t to {0};
		mach_vm_address_t *org {nullptr};

		/**
		 *  Construct RouteRequest for wrapping a function
		 *  @param s  symbol to lookup
		 *  @param t  destination address
		 *  @param o  trampoline storage to the original symbol
		 */
		template <typename T>
		RouteRequest(const char *s, T t, mach_vm_address_t &o) :
			symbol(s), to(reinterpret_cast<mach_vm_address_t>(t)), org(&o) { }

		/**
		 *  Construct RouteRequest for wrapping a function
		 *  @param s  symbol to lookup
		 *  @param t  destination address
		 *  @param o  trampoline storage to the original symbol
		 */
		template <typename T, typename O>
		RouteRequest(const char *s, T t, O &o) :
			RouteRequest(s, t, reinterpret_cast<mach_vm_address_t&>(o)) { }

		/**
		 *  Construct RouteRequest for routing a function
		 *  @param s  symbol to lookup
		 *  @param t  destination address
		 */
		template <typename T>
		RouteRequest(const char *s, T t) :
			symbol(s), to(reinterpret_cast<mach_vm_address_t>(t)) { }
	};

	/**
	 *  Simple route multiple functions with basic error handling
	 *
	 *  @param id           kernel item identifier
	 *  @param requests     an array of requests to replace
	 *  @param num          requests array size
	 *  @param start        start address range
	 *  @param size         address range size
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param force        continue on first error
	 *
	 *  @return false if it at least one error happened
	 */
	EXPORT bool routeMultiple(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start=0, size_t size=0, bool kernelRoute=true, bool force=false);

	/**
	 *  Simple route multiple functions with basic error handling with long routes
	 *
	 *  @param id           kernel item identifier
	 *  @param requests     an array of requests to replace
	 *  @param num          requests array size
	 *  @param start        start address range
	 *  @param size         address range size
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param force        continue on first error
	 *
	 *  @return false if it at least one error happened
	 */
	EXPORT bool routeMultipleLong(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start=0, size_t size=0, bool kernelRoute=true, bool force=false);

	/**
	 *  Simple route multiple functions with basic error handling with short routes
	 *
	 *  @param id           kernel item identifier
	 *  @param requests     an array of requests to replace
	 *  @param num          requests array size
	 *  @param start        start address range
	 *  @param size         address range size
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param force        continue on first error
	 *
	 *  @return false if it at least one error happened
	 */
	EXPORT bool routeMultipleShort(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start=0, size_t size=0, bool kernelRoute=true, bool force=false);

	/**
	 *  Simple route multiple functions with basic error handling
	 *
	 *  @param id           kernel item identifier
	 *  @param requests     an array of requests to replace
	 *  @param start        start address range
	 *  @param size         address range size
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param force        continue on first error
	 *
	 *  @return false if it at least one error happened
	 */
	template <size_t N>
	inline bool routeMultiple(size_t id, RouteRequest (&requests)[N], mach_vm_address_t start=0, size_t size=0, bool kernelRoute=true, bool force=false) {
		return routeMultiple(id, requests, N, start, size, kernelRoute, force);
	}

	/**
	 *  Simple route multiple functions with basic error handling with long routes
	 *
	 *  @param id           kernel item identifier
	 *  @param requests     an array of requests to replace
	 *  @param start        start address range
	 *  @param size         address range size
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param force        continue on first error
	 *
	 *  @return false if it at least one error happened
	 */
	template <size_t N>
	inline bool routeMultipleLong(size_t id, RouteRequest (&requests)[N], mach_vm_address_t start=0, size_t size=0, bool kernelRoute=true, bool force=false) {
		return routeMultipleLong(id, requests, N, start, size, kernelRoute, force);
	}

	/**
	 *  Simple route multiple functions with basic error handling with long routes
	 *
	 *  @param id           kernel item identifier
	 *  @param requests     an array of requests to replace
	 *  @param start        start address range
	 *  @param size         address range size
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param force        continue on first error
	 *
	 *  @return false if it at least one error happened
	 */
	template <size_t N>
	inline bool routeMultipleShort(size_t id, RouteRequest (&requests)[N], mach_vm_address_t start=0, size_t size=0, bool kernelRoute=true, bool force=false) {
		return routeMultipleShort(id, requests, N, start, size, kernelRoute, force);
	}

	/**
	 *  Find one pattern with optional masking within a block of memory
	 *
	 *  @param pattern           pattern to search
	 *  @param patternMask           pattern mask
	 *  @param patternSize           size of pattern
	 *  @param data           a block of memory
	 *  @param dataSize           size of memory
	 *  @param dataOffset           data offset, to be set by this function
	 *
	 *  @return true if pattern is found in data
	 */
	EXPORT static bool findPattern(const void *pattern, const void *patternMask, size_t patternSize, const void *data, size_t dataSize, size_t *dataOffset);

	/**
	 *  Simple find and replace with masking in kernel memory.
	 */
	EXPORT static bool findAndReplaceWithMask(void *data, size_t dataSize, const void *find, size_t findSize, const void *findMask, size_t findMaskSize, const void *replace, size_t replaceSize, const void *replaceMask, size_t replaceMaskSize, size_t count=0, size_t skip=0);

	/**
	 *  Simple find and replace in kernel memory.
	 */
	static inline bool findAndReplace(void *data, size_t dataSize, const void *find, size_t findSize, const void *replace, size_t replaceSize) {
		return findAndReplaceWithMask(data, dataSize, find, findSize, nullptr, 0, replace, replaceSize, nullptr, 0, 0, 0);
	}

	/**
	 *  Simple find and replace in kernel memory but require both `find` and `replace` buffers to have the same length
	 */
	template <size_t N>
	static inline bool findAndReplace(void *data, size_t dataSize, const uint8_t (&find)[N], const uint8_t (&replace)[N]) {
		return findAndReplace(data, dataSize, find, N, replace, N);
	}

	/**
	 *  Simple find and replace with masking in kernel memory but require both `find` and `replace` buffers and masking buffers to have the same length
	 */
	template <size_t N>
	static inline bool findAndReplaceWithMask(void *data, size_t dataSize, const uint8_t (&find)[N], const uint8_t (&findMask)[N], const uint8_t (&replace)[N], const uint8_t (&replaceMask)[N], size_t count, size_t skip) {
		return findAndReplaceWithMask(data, dataSize, find, N, findMask, N, replace, N, replaceMask, N, count, skip);
	}

private:
	/**
	 *  Jump type for routing
	 */
	enum class JumpType {
		Auto,
		Long,
		Short,
		Medium
	};

	/**
	 *  The minimal reasonable memory requirement
	 */
	static constexpr size_t TempExecutableMemorySize {4096};

	/**
	 *  As of 10.12 we seem to be not allowed to call vm_ functions from several places including onKextSummariesUpdated.
	 */
	static uint8_t tempExecutableMemory[TempExecutableMemorySize];

	/**
	 *  Offset to tempExecutableMemory that is safe to use
	 */
	size_t tempExecutableMemoryOff {0};

	/**
	 *  Patcher status
	 */
	_Atomic(bool) activated = false;

	/**
	 *  Read previous jump destination from function
	 *
	 *  @param from          formerly routed function
	 *  @param jumpType previous jump type
	 *
	 *  @return wrapper pointer on success or 0
	 */
	mach_vm_address_t readChain(mach_vm_address_t from, JumpType &jumpType);

	/**
	 *  Created routed trampoline page
	 *
	 *  @param func     original area
	 *  @param min      minimal amount of bytes that will be overwritten
	 *  @param opcodes  opcodes to insert before function
	 *  @param opnum    number of opcodes
	 *
	 *  @return trampoline pointer or 0
	 */
	mach_vm_address_t createTrampoline(mach_vm_address_t func, size_t min, const uint8_t *opcodes=nullptr, size_t opnum=0);

	/**
	 *  Route function to function
	 *
	 *  @param from         function to route
	 *  @param to           routed function
	 *  @param buildWrapper create entrance wrapper
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param revertible   patches could be reverted
	 *  @param jumpType     jump type to use, relative short or absolute long
	 *  @param info         info to access address slots to use for shorter routing
	 *  @param org          write pointer to this variable
	 *
	 *  @return wrapper pointer or 0 on success
	 */
	mach_vm_address_t routeFunctionInternal(mach_vm_address_t from, mach_vm_address_t to, bool buildWrapper=false, bool kernelRoute=true, bool revertible=true, JumpType jumpType=JumpType::Auto, MachInfo *info=nullptr, mach_vm_address_t *org=nullptr);

	/**
	 *  Simple route multiple functions with basic error handling with long routes
	 *
	 *  @param id           kernel item identifier
	 *  @param requests     an array of requests to replace
	 *  @param num          requests array size
	 *  @param start        start address range
	 *  @param size         address range size
	 *  @param kernelRoute  kernel change requiring memory protection changes and patch reverting at unload
	 *  @param force        continue on first error
	 *  @param jumpType     jump type to use, relative short or absolute long
	 *
	 *  @return false if it at least one error happened
	 */
	bool routeMultipleInternal(size_t id, RouteRequest *requests, size_t num, mach_vm_address_t start=0, size_t size=0, bool kernelRoute=true, bool force=false, JumpType jumpType=JumpType::Auto);

	/**
	 *  Simple find and replace with masking in kernel memory
	 *
	 *  @param data           kernel memory
	 *  @param dataSize           size of kernel memory
	 *  @param find           find pattern
	 *  @param findSize           size of find pattern
	 *  @param findMask           find masking pattern
	 *  @param findMaskSize           size of find masking pattern
	 *  @param replace           replace pattern
	 *  @param replaceSize           size of replace pattern
	 *  @param replaceMask           replace masking pattern
	 *  @param replaceMaskSize           repalce masking pattern
	 *  @param count           maximum times of patching
	 *  @param skip           number of skipping times before performing replacement
	 *
	 *  @return true if the finding and replacing performance is successful
	 */
	static bool findAndReplaceWithMaskInternal(void *data, size_t dataSize, const void *find, size_t findSize, const void *findMask, size_t findMaskSize, const void *replace, size_t replaceSize, const void *replaceMask, size_t replaceMaskSize, size_t count, size_t skip);

#ifdef LILU_KEXTPATCH_SUPPORT
	/**
	 *  Process loaded kext
	 */
	void processKext(kmod_info_t *kmod, bool loaded);

	/**
	 *  Process already loaded kexts once at the start
	 *
	 */
	void processAlreadyLoadedKexts();

	/**
	 *  Pointer to loaded kmods for kexts
	 */
	kmod_info_t **kextKmods {nullptr};

	/**
	 *  Called at kext unloading if kext listening is enabled on macOS 10.6 and newer
	 */
	static OSReturn onOSKextUnload(void *thisKext);

	/**
	 *  A pointer to OSKext::unload()
	 */
	mach_vm_address_t orgOSKextUnload {};

	/**
	 *  Called at kext loading and unloading if kext listening is enabled on macOS 10.6 and newer
	 */
	static void onOSKextSaveLoadedKextPanicList();

	/**
	 *  A pointer to OSKext::saveLoadedKextPanicList()
	 */
	mach_vm_address_t orgOSKextSaveLoadedKextPanicList {};

#if defined(__i386__)
	/**
	 *  Called at kext loading if kext listening is enabled on macOS 10.4 and 10.5
	 */
	static kern_return_t onKmodCreateInternal(kmod_info_t *kmod, kmod_t *id);

	/**
	 *  A pointer to kmod_create_internal()
	 */
	mach_vm_address_t orgKmodCreateInternal {};
#endif

#endif /* LILU_KEXTPATCH_SUPPORT */

	/**
	 *  Kernel prelink image in case prelink is used
	 */
	MachInfo *prelinkInfo {nullptr};

	/**
	 *  Loaded kernel items
	 */
	evector<MachInfo *, MachInfo::deleter> kinfos;

	/**
	 *  Applied patches
	 */
	evector<Patch::All *, Patch::deleter> kpatches;

#ifdef LILU_KEXTPATCH_SUPPORT
	/**
	 *  Awaiting kext notificators
	 */
	evector<KextHandler *, KextHandler::deleter> khandlers;

	/**
	 *  Awaiting already loaded kext list
	 */
	bool waitingForAlreadyLoadedKexts {false};

	/**
	 *  Flag to prevent kext processing during an unload
	 */
	bool isKextUnloading {false};

#endif /* LILU_KEXTPATCH_SUPPORT */

#ifdef LILU_KCINJECT_SUPPORT
	/**
	 *  Kernel function prototypes
	 */
	using t_vmMapKcfilesetSegment = kern_return_t (*)(vm_map_offset_t*, vm_map_offset_t, void*, vm_object_offset_t, vm_prot_t);
	using t_getAddressFromKextMap = vm_offset_t (*)(vm_size_t);
	using t_machVmDeallocate = kern_return_t (*)(vm_map_t, vm_map_offset_t, mach_vm_size_t);

	/**
	 *  Original kernel function trampolines
	 */
	t_vmMapKcfilesetSegment orgVmMapKcfilesetSegment {nullptr};
	t_getAddressFromKextMap orgGetAddressFromKextMap {nullptr};
	t_machVmDeallocate orgMachVmDeallocate {nullptr};

	/**
	 *  The kind of KC OSKext::loadKCFileSet is currently loading, if any
	 */
	kc_kind_t curLoadingKCKind = kc_kind::KCKindNone;

	/**
	 *  The "memory control objects" of KCs
	 */
	void *kcControls[kc_kind::KCNumKinds] = {nullptr};

	/**
	 *  Injection infos of KCs
	 */
	OSArray *kcInjectInfos[kc_kind::KCNumKinds] = {nullptr};

	/**
	 *  Block infos of KCs
	 */
	OSArray *kcBlockInfos[kc_kind::KCNumKinds] = {nullptr};

	/**
	 *  Size of KCs on the disk
	 */
	vm_size_t kcDiskSizes[kc_kind::KCNumKinds] = {0};

	/**
	 *  Patch infos of KCs
	 */
	OSArray *kcPatchInfos[kc_kind::KCNumKinds] = {nullptr};

	/**
	 *  A pointer to g_kext_map, used for calling and wrapping vm_map_remove()
	 */
	vm_map_t *gKextMap = nullptr;

	/**
	 *  Stores exported symbols from various KCs
	 */
	OSDictionary *kcSymbols;
#endif /* LILU_KCINJECT_SUPPORT */

	/**
	 *  Current error code
	 */
	Error code {Error::NoError};
	static constexpr size_t INVALID {0};

	/**
	 *  Jump instruction sizes
	 */
	static constexpr size_t SmallJump {1 + sizeof(int32_t)};
	static constexpr size_t LongJump {6 + sizeof(uintptr_t)};
	static constexpr size_t MediumJump {6};
	static constexpr uint8_t SmallJumpPrefix {0xE9};
	static constexpr uint16_t LongJumpPrefix {0x25FF};

	/**
	 * Atomic trampoline generator, wraps jumper into 64-bit or 128-bit storage
	 */
	union FunctionPatch {
		struct PACKED LongPatch {
			uint16_t  opcode;
			uint32_t  argument;
			uintptr_t disp;
			uint8_t   org[sizeof(uint64_t) - sizeof(uintptr_t) + sizeof(uint16_t)];
		} l;
		static_assert(sizeof(l) == (sizeof(uint64_t) * 2), "Invalid long patch rounding");
		struct PACKED MediumPatch {
			uint16_t opcode;
			uint32_t argument;
			uint8_t  org[2];
		} m;
		static_assert(sizeof(m) == sizeof(uint64_t), "Invalid medium patch rounding");
		struct PACKED SmallPatch {
			uint8_t opcode;
			uint32_t argument;
			uint8_t org[3];
		} s;
		static_assert(sizeof(s) == sizeof(uint64_t), "Invalid small patch rounding");
		template <typename T>
		inline void sourceIt(mach_vm_address_t source) {
			// Note, this one violates strict aliasing, but we play with the memory anyway.
			for (size_t i = 0; i < sizeof(T::org); ++i)
				reinterpret_cast<volatile T *>(this)->org[i] = *reinterpret_cast<uint8_t *>(source + offsetof(T, org) + i);
		}
		uint64_t value64;
#if defined(__x86_64__)
		unsigned __int128 value128;
#endif
	} patch;

	/**
	 *  Possible kernel paths
	 */
#ifdef LILU_COMPRESSION_SUPPORT
	const char *prelinkKernelPaths[7] {
		// This is the usual kernel cache place, which often the best thing to use
		"/System/Library/Caches/com.apple.kext.caches/Startup/kernelcache",
		// Otherwise fallback to one of the prelinked kernels
		// Since we always verify the LC_UUID value, trying the kernels could be done in any order.
		"/System/Library/PrelinkedKernels/prelinkedkernel", // normal
		"/macOS Install Data/Locked Files/Boot Files/prelinkedkernel", // 10.13 installer
		"/com.apple.boot.R/prelinkedkernel", // 10.12+ fusion drive installer
		"/com.apple.boot.S/System/Library/PrelinkedKernels/prelinkedkernel", // 10.11 fusion drive installer
		"/com.apple.recovery.boot/prelinkedkernel", // recovery
		"/kernelcache" // 10.7 installer
	};
#endif

	const char *kernelPaths[2] {
		"/System/Library/Kernels/kernel",	//since 10.10
		"/mach_kernel"
	};
};

#endif /* kern_patcher_hpp */
