//
//  kern_policy.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_policy.hpp>
#include <Headers/kern_util.hpp>

#if defined(__x86_64__)
bool Policy::registerPolicy() {
	return mac_policy_register(&policyConf, &policyHandle, nullptr) == KERN_SUCCESS;
}

bool Policy::unregisterPolicy() {
	if (!(policyConf.mpc_loadtime_flags & MPC_LOADTIME_FLAG_UNLOADOK)) {
		SYSLOG("policy", "this policy is not allowed to be unregistered");
	}

	return mac_policy_unregister(policyHandle) == KERN_SUCCESS;
}
#endif
