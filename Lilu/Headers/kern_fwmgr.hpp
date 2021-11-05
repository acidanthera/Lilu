//
//  kern_fwmgr.h
//  Lilu
//
//  Copyright © 2021 cjiang. All rights reserved.
//

#ifndef kern_fwmgr_hpp
#define kern_fwmgr_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/OSKextLib.h>

typedef struct FirmwareDescriptor
{
	char * name;
	UInt8 * firmwareData;
	UInt32 firmwareSize;
} FirmwareDescriptor;

class FirmwareManager : public IOService
{
	OSDeclareDefaultStructors(FirmwareManager)

	struct ResourceCallbackContext
	{
		FirmwareManager * me;
		OSData * firmware;
	};

public:
	/*! @function withName
	 *   @abstract Creates an FirmwareManager instance with the name of the firmware in that is requested.
	 *   @discussion After creating the instance, the function calls setFirmwareWithName to set the firmware.
	 *   @param name The name of the requested firmware.
	 *   @param firmwareList A list that consists of all possible firmware candidates.
	 *   @param numFirmwares The number of firmwares in firmwareList.
	 *   @result If the operation is successful, the instance created is returned. */

	static FirmwareManager * withName(char * name, FirmwareDescriptor * firmwareList, int numFirmwares);

	virtual IOReturn setFirmwareWithName(char * name, FirmwareDescriptor * firmwareCandidates, int numFirmwares);

	/*! @function withDescriptor
	 *   @abstract Creates an FirmwareManager instance with a firmware descriptor.
	 *   @discussion After creating the instance, the function calls setFirmwareWithDescriptor to set the firmware.
	 *   @param firmware The firmware descriptor upon which the instance will be generated.
	 *   @result If the operation is successful, the instance created is returned. */

	static FirmwareManager * withDescriptor(FirmwareDescriptor firmware);

	virtual IOReturn setFirmwareWithDescriptor(FirmwareDescriptor firmware);

	virtual bool init( OSDictionary * dictionary = NULL ) APPLE_KEXT_OVERRIDE;
	virtual void free() APPLE_KEXT_OVERRIDE;

	virtual IOReturn removeFirmware();

	virtual OSData * getFirmwareUncompressed();
	virtual char * getFirmwareName();

protected:
	virtual int decompressFirmware(OSData * firmware);
	virtual bool isFirmwareCompressed(OSData * firmware);

protected:
	char * mFirmwareName;
	IOLock * mUncompressedFirmwareLock;
	OSData * mUncompressedFirmwareData;
	IOLock * mCompletionLock;
};

#endif /* kern_fwmgr_hpp */
