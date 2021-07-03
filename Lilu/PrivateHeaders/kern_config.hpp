//
//  kern_config_private.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_config_private_h
#define kern_config_private_h

#include <Headers/kern_patcher.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_policy.hpp>
#include <Headers/kern_util.hpp>
#include <kern/thread_call.h>
#include <stdatomic.h>

class Configuration {
	/**
	 *  Possible boot arguments
	 */
	static constexpr const char *bootargOff {"-liluoff"};           // Disable the kext
	static constexpr const char *bootargUserOff {"-liluuseroff"};   // Disable kext user patcher
	static constexpr const char *bootargBeta {"-lilubeta"};         // Force enable the kext on unsupported os
	static constexpr const char *bootargBetaAll {"-lilubetaall"};   // Force enable the kext and all plugins on unsupported os
	static constexpr const char *bootargForce {"-liluforce"};       // Force enable the kext (including single user mode)
	static constexpr const char *bootargDebug {"-liludbg"};         // Enable debug logging
	static constexpr const char *bootargDebugAll {"-liludbgall"};   // Enable debug logging (for Lilu and all the plugins)
	static constexpr const char *bootargSlow {"-liluslow"};         // Prefer less destructive userspace measures
	static constexpr const char *bootargFast {"-lilufast"};         // Prefer faster userspace measures
	static constexpr const char *bootargLowMem {"-lilulowmem"};     // Disable decompression
	static constexpr const char *bootargDelay {"liludelay"};        // Extra delay timeout after each printed message
	static constexpr const char *bootargDump {"liludump"};          // Dump lilu log to /Lilu...txt after N seconds

public:
	/**
	 *  Externally handled boot arguments
	 */
	static constexpr const char *bootargCpu {"lilucpu"};            // Simulate this CPU generation, handled in kern_cpu.cpp
	
	/**
	 *	Current architecture name
	 */
#if defined(__i386__)
	static constexpr const char *currentArch {"i386"};
#elif defined(__x86_64__)
	static constexpr const char *currentArch {"x86_64"};
#else
#error Unsupported arch.
#endif

private:
	/**
	 * Minimal required kernel version
	 */
	static constexpr KernelVersion minKernel {KernelVersion::SnowLeopard};

	/**
	 * Maxmimum supported kernel version
	 */
	static constexpr KernelVersion maxKernel {KernelVersion::Monterey};

	/**
	 *  Set once the arguments are parsed
	 */
	bool readArguments {false};

	/**
	 *  Initialise kernel patcher in two-stage mode
	 *
	 *  @return true on success
	 */
	bool performEarlyInit();

	/**
	 *  Initialise kernel and user patchers if necessary
	 *
	 *  @return true on success
	 */
	bool performInit();

	/**
	 *  Initialise second stage kernel patcher
	 *
	 *  @return true on success
	 */
	bool performCommonInit();

	/**
	 *  Initialise kernel and user patchers from policy handler
	 */
	void policyInit(const char *name) {
		(void)name;

		// Outer check is used here to avoid unnecessary locking after we initialise
		if (!atomic_load_explicit(&initialised, memory_order_relaxed)) {
			IOLockLock(policyLock);
			if (!atomic_load_explicit(&initialised, memory_order_relaxed)) {
				DBGLOG("config", "init via %s", name);
				performInit();
			}
			IOLockUnlock(policyLock);
		}
	}

	/**
	 *  TrustedBSD policy called at exec
	 *
	 *  @param old Existing subject credential
	 *  @param vp  File being executed
	 *
	 *  @return 0 on success
	 */
	static int policyCredCheckLabelUpdateExecve(kauth_cred_t, vnode_t, ...);

	/**
	 *  TrustedBSD policy called before remounting
	 *
	 *  @param cred     auth credential
	 *  @param mp       mount point
	 *  @param mlabel    mount point label
	 */
	static int policyCheckRemount(kauth_cred_t, mount *, label *);

	/**
	 *  May be used at TrustedBSD policy initialisation
	 *
	 *  @param conf policy configuration
	 */
	static void policyInitBSD(mac_policy_conf *conf);

	/**
	 *  Console initialisation wrapper used for signaling Lilu to end plugin loading.
	 *
	 *  @param info   video information
	 *  @param op     operation to perform
	 *
	 *  @return 0 on success
	 */
	static int initConsole(PE_Video *info, int op);

	/**
	 *  TrustedBSD policy options
	 */
	mac_policy_ops policyOps {
		.mpo_policy_initbsd = policyInitBSD
	};

	/**
	 *  TrustedBSD policy handlers are not thread safe
	 */
	IOLock *policyLock {nullptr};

	/**
	 *  Original function pointer for PE_initialize_console.
	 */
	mach_vm_address_t orgInitConsole {0};

#ifdef DEBUG
	/**
	 *  Debug buffer dump timeout in seconds
	 */
	uint32_t debugDumpTimeout {0};

	/**
	 *  Debug buffer dump thread call
	 */
	thread_call_t debugDumpCall {nullptr};

	/**
	 *  Initialise log to custom buffer support
	 *  You may call it from a debugger if you need to save the log once again.
	 */
	void initCustomDebugSupport();

	/**
	 *  Stores debug log on disk
	 *
	 *  @param param0 unused
	 *  @param param1 unused
	 */
	static void saveCustomDebugOnDisk(thread_call_param_t param0, thread_call_param_t param1);
#endif

public:
	/**
	 *  Retrieve boot arguments
	 *
	 *  @return true if allowed to continue
	 */
	bool getBootArguments();

	/**
	 *  Register TrustedBSD policy
	 *
	 *  @return true on success
	 */
	bool registerPolicy();

	/**
	 *  Disable the extension by default
	 */
	bool isDisabled {true};

	/**
	 *  User patcher is disabled on request
	 */
	bool isUserDisabled {false};

	/**
	 *  Do not patch dyld shared cache unless asked
	 */
	bool preferSlowMode {false};

	/**
	 *  Allow decompression
	 */
	bool allowDecompress {true};

	/**
	 *  Install or recovery
	 */
	bool installOrRecovery {false};

	/**
	 *  Safe mode
	 */
	bool safeMode {false};

	/**
	 *  Beta for all plugins and Lilu itself
	 */
	bool betaForAll {false};

	/**
	 *  Debug for all plugins and Lilu itself
	 */
	bool debugForAll {false};

	/**
	 *  Load status (are we allowed to do anything?)
	 */
	bool startSuccess {false};

	/**
	 *  Initialisation status (are we done initialising?)
	 */
	_Atomic(bool) initialised = ATOMIC_VAR_INIT(false);

	/**
	 *  User patcher
	 */
	UserPatcher userPatcher;

	/**
	 *  Kernel patcher
	 */
	KernelPatcher kernelPatcher;

	/**
	 *  Policy controller
	 */
	Policy policy;

#ifdef DEBUG
	/**
	 *  Full policy name
	 */
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION) " DEBUG build"};

	/**
	 *  Maximum amount of data we can via the internal buffer (8 MB)
	 */
	static constexpr size_t MaxDebugBufferSize {1024*1024*8};

	/**
	 *  Custom logging lock
	 */
	IOSimpleLock *debugLock {nullptr};

	/**
	 *  Debug buffer with logged data, intentionally disabled in RELEASE mode
	 *  to avoid sensitive information leak.
	 *  Contains debugBufferLength symbols, not null-terminated.
	 */
	uint8_t *debugBuffer {nullptr};

	/**
	 *  Debug buffer current length
	 */
	size_t debugBufferLength {0};
#else

	/**
	 *  Full policy name
	 */
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION)};
#endif

	Configuration() : policy(xStringify(PRODUCT_NAME), fullName, &policyOps) {}
};

extern Configuration ADDPR(config);

#endif /* kern_config_private_h */
