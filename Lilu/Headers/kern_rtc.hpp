//
//  kern_rtc.hpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#ifndef kern_rtc_h
#define kern_rtc_h

#include <Headers/kern_util.hpp>
#include <Library/LegacyIOService.h>

class RTCStorage {

	/**
	 *  AppleRTC service handle
	 */
	IOService *rtcSrv {nullptr};

public:
	/**
	 *  Attempt to connect to active RTC service
	 *
	 *  @param wait  wait for service availability
	 *
	 *  @return true on success
	 */
	EXPORT bool init(bool wait=true);

	/**
	 *  Release obtained RTC service
	 */
	EXPORT void deinit();

	/**
	 *  Check whether extended (higher 128 bytes) is available
	 *
	 *  @return true on success
	 */
	EXPORT bool checkExtendedMemory();

	/**
	 *  Read memory from RTC
	 *
	 *  @param off     offset to read data from
	 *  @param size    data size
	 *  @param buffer  data buffer to read to
	 *
	 *  @return true on success
	 */
	EXPORT bool read(uint64_t off, uint32_t size, uint8_t *buffer);

	/**
	 *  Write memory to RTC
	 *
	 *  @param off     offset to write data to
	 *  @param size    data size
	 *  @param buffer  data buffer to write from
	 *
	 *  @return true on success
	 */
	EXPORT bool write(uint64_t off, uint32_t size, uint8_t *buffer);
};

#endif /* kern_rtc_h */
