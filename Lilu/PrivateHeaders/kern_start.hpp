//
//  kern_start_private.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_start_private_hpp
#define kern_start_private_hpp

#include <Headers/kern_config.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_policy.hpp>

#include "Library/LegacyIOService.h"

class EXPORT PRODUCT_NAME : public IOService {
	OSDeclareDefaultStructors(PRODUCT_NAME)
public:
	bool init(OSDictionary *dict) override;
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
};

#endif /* kern_start_private_hpp */
