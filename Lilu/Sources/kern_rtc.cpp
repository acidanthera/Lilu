//
//  kern_rtc.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_rtc.hpp>
#include <IOKit/IOUserClient.h>

bool RTCStorage::init(bool wait) {
	auto matching = IOService::serviceMatching("AppleRTC");
	if (matching) {
		if (wait)
			rtcSrv = IOService::waitForMatchingService(matching);
		else
			rtcSrv = IOService::copyMatchingService(matching);
		matching->release();
	} else {
		SYSLOG("rtc", "failed to allocate rtc matching");
	}
	return rtcSrv != nullptr;
}

void RTCStorage::deinit() {
	if (rtcSrv) {
		rtcSrv->release();
		rtcSrv = nullptr;
	}
}

bool RTCStorage::checkExtendedMemory() {
	uint8_t off = 0xB4; /* APPLERTC_POWER_BYTE_PM_ADDR */
	uint8_t dst = 0;
	return read(off, 1, &dst);
}

bool RTCStorage::read(uint64_t off, uint32_t size, uint8_t *buffer) {
	if (!rtcSrv)
		return false;

	IOUserClient *rtcHandler = nullptr;
	auto ret = rtcSrv->newUserClient(current_task(), current_task(), 0x101beef, &rtcHandler);
	if (ret == kIOReturnSuccess) {
		DBGLOG("rtc", "successful rtc read client obtain");
		IOExternalMethodArguments args {};

		args.version = kIOExternalMethodArgumentsCurrentVersion;
		args.selector = 0;
		args.asyncWakePort = MACH_PORT_NULL;
		args.scalarInput = &off;
		args.scalarInputCount = 1;
		args.structureOutput = buffer;
		args.structureOutputSize = size;

		ret = rtcHandler->externalMethod(0, &args);
		rtcHandler->release();
		if (ret == kIOReturnSuccess)
			return true;
		SYSLOG("rtc", "rtc read failure %d bytes from %d %X", size, static_cast<uint32_t>(off), ret);
	} else {
		SYSLOG("rtc", "rtc read client obtain failure %X", ret);
	}

	return false;
}

bool RTCStorage::write(uint64_t off, uint32_t size, uint8_t *buffer) {
	if (!rtcSrv)
		return false;

	IOUserClient *rtcHandler = nullptr;
	auto ret = rtcSrv->newUserClient(current_task(), current_task(), 0x101beef, &rtcHandler);
	if (ret == kIOReturnSuccess) {
		DBGLOG("rtc", "successful rtc read client obtain");
		IOExternalMethodArguments args {};

		args.version = kIOExternalMethodArgumentsCurrentVersion;
		args.selector = 1;
		args.asyncWakePort = MACH_PORT_NULL;
		args.scalarInput = &off;
		args.scalarInputCount = 1;
		args.structureInput = buffer;
		args.structureInputSize = size;

		ret = rtcHandler->externalMethod(1, &args);
		rtcHandler->release();
		if (ret == kIOReturnSuccess)
			return true;
		SYSLOG("rtc", "rtc write failure %d bytes from %d %X", size, static_cast<uint32_t>(off), ret);
	} else {
		SYSLOG("rtc", "rtc write client obtain failure %X", ret);
	}

	return false;
}
