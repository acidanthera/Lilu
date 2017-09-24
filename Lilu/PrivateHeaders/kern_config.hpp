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

class Configuration {
	/**
	 *  Possible boot arguments
	 */
	static constexpr const char *bootargOff {"-liluoff"};			// Disable the kext
	static constexpr const char *bootargBeta {"-lilubeta"};			// Force enable the kext on unsupported os
	static constexpr const char *bootargBetaAll {"-lilubetaall"};	// Force enable the kext and all plugins on unsupported os
	static constexpr const char *bootargForce {"-liluforce"};		// Force enable the kext (including safe mode)
	static constexpr const char *bootargDebug {"-liludbg"};			// Enable debug logging
	static constexpr const char *bootargSlow {"-liluslow"};			// Prefer less destructive userspace measures
	static constexpr const char *bootargFast {"-lilufast"};			// Prefer faster userspace measures
	static constexpr const char *bootargLowMem {"-lilulowmem"};		// Disable decompression
	
	/**
	 * Minimal required kernel version
	 */
	static constexpr KernelVersion minKernel {KernelVersion::MountainLion};
	
	/**
	 * Maxmimum supported kernel version
	 */
	static constexpr KernelVersion maxKernel {KernelVersion::HighSierra};
	
	/**
	 *  Set once the arguments are parsed
	 */
	bool readArguments {false};
	
	/**
	 *  Initialise kernel and user patchers if necessary
	 *
	 *  @return true on success
	 */
	bool performInit();
	
	/**
	 *  TrustedBSD policy called at exec
	 *
	 *  @param old Existing subject credential
	 *  @param vp  File being executed
	 *
	 *  @return 0 on success
	 */
	static int policyCredCheckLabelUpdateExecve(kauth_cred_t old, vnode_t vp, ...);
	
	
	/**
	 *  TrustedBSD policy called before remounting
	 *
	 *  @param cred     auth credential
	 *  @param mp       mount point
	 *  @param mlabel    mount point label
	 */
	static int policyCheckRemount(kauth_cred_t cred, mount *mp, label *mlabel);
	
	/**
	 *  TrustedBSD policy options
	 */
	mac_policy_ops policyOps {
		.mpo_policy_initbsd					= Policy::dummyPolicyInitBSD
	};
	
public:
	/**
	 *  Retrieve boot arguments
	 *
	 *  @return true if allowed to continue
	 */
	bool getBootArguments();
	
	/**
	 *  Disable the extension by default
	 */
	bool isDisabled {true};
	
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
	 *  Initialisation status
	 */
	bool initialised {false};
	
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
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION) " DEBUG build"};
#else
	static constexpr const char *fullName {xStringify(PRODUCT_NAME) " Kernel Extension " xStringify(MODULE_VERSION)};
#endif
	
	Configuration() : policy(xStringify(PRODUCT_NAME), fullName, &policyOps) {}
};

extern Configuration config;

#endif /* kern_config_private_h */
