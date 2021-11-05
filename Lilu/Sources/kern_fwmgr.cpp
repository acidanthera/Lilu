//
//  kern_fwmgr.cpp
//  Lilu
//
//  Copyright © 2021 cjiang. All rights reserved.
//

#include <Headers/kern_fwmgr.hpp>
#include <libkern/zlib.h>

extern "C"
{
	static void * zalloc(void * opaque, UInt32 items, UInt32 size);
	static void zfree(void * opaque, void * ptr);

	typedef struct z_mem
	{
		UInt32 allocSize;
		UInt8 data[0];
	} z_mem;

	void * zalloc(void * opaque, UInt32 items, UInt32 size)
	{
	   void * result = NULL;
	   z_mem * zmem = NULL;
	   UInt32 allocSize =  items * size + sizeof(zmem);

	   zmem = (z_mem *) IOMalloc(allocSize);

	   if (zmem)
	   {
		   zmem->allocSize = allocSize;
		   result = (void *) &(zmem->data);
	   }

	   return result;
	}

	void zfree(void * opaque, void * ptr)
	{
	   UInt32 * skipper = (UInt32 *) ptr - 1;
	   z_mem * zmem = (z_mem *) skipper;
	   IOFree((void *) zmem, zmem->allocSize);
	}
}

#define super IOService
OSDefineMetaClassAndStructors(FirmwareManager, super)

bool FirmwareManager::init(OSDictionary * dictionary)
{
	if ( !super::init() )
		return false;

	mFirmwareName = NULL;
	mUncompressedFirmwareLock = IOLockAlloc();
	mUncompressedFirmwareData = NULL;
	return true;
}

void FirmwareManager::free()
{
	removeFirmware();
	IOLockFree(mUncompressedFirmwareLock);
	super::free();
}

int FirmwareManager::decompressFirmware(OSData * firmware)
{
	z_stream zstream;
	int zlib_result;
	void * buffer = NULL;
	UInt32 bufferSize = 0;

	if ( !isFirmwareCompressed(firmware) )
	{
		firmware->retain();
		IOLockLock(mUncompressedFirmwareLock);
		mUncompressedFirmwareData = firmware;
		IOLockUnlock(mUncompressedFirmwareLock);
		return Z_OK;
	}

	bufferSize = firmware->getLength() * 4;
	buffer = IOMalloc(bufferSize);

	bzero(&zstream, sizeof(zstream));
	zstream.next_in   = (UInt8 *) firmware->getBytesNoCopy();
	zstream.avail_in  = firmware->getLength();
	zstream.next_out  = (UInt8 *) buffer;
	zstream.avail_out = bufferSize;
	zstream.zalloc    = zalloc;
	zstream.zfree     = zfree;

	zlib_result = inflateInit(&zstream);
	if (zlib_result != Z_OK)
	{
		IOFree(buffer, bufferSize);
		return zlib_result;
	}

	zlib_result = inflate(&zstream, Z_FINISH);
	if (zlib_result == Z_STREAM_END || zlib_result == Z_OK)
	{
		IOLockLock(mUncompressedFirmwareLock);
		mUncompressedFirmwareData = OSData::withBytes(buffer, (unsigned int) zstream.total_out);
		IOLockUnlock(mUncompressedFirmwareLock);
	}

	inflateEnd(&zstream);
	IOFree(buffer, bufferSize);

	return zlib_result;
}

IOReturn FirmwareManager::setFirmwareWithName(char * name, FirmwareDescriptor * firmwareCandidates, int numFirmwares)
{
	OSData * fwData;

	for (int i = 0; i < numFirmwares; ++i)
	{
		if (firmwareCandidates[i].name == name)
		{
			mFirmwareName = name;
			fwData = OSData::withBytes(firmwareCandidates[i].firmwareData, firmwareCandidates[i].firmwareSize);
			if ( isFirmwareCompressed(fwData) )
			{
				if ( !decompressFirmware(fwData) )
				{
					OSSafeReleaseNULL(fwData);
					return kIOReturnSuccess;
				}
				OSSafeReleaseNULL(fwData);
				return kIOReturnError;
			}
			IOLockLock(mUncompressedFirmwareLock);
			mUncompressedFirmwareData = fwData;
			IOLockUnlock(mUncompressedFirmwareLock);
			return kIOReturnSuccess;
		}
	}
	return kIOReturnUnsupported;
}

IOReturn FirmwareManager::removeFirmware()
{
	mFirmwareName = NULL;

	IOLockLock(mUncompressedFirmwareLock);
	OSSafeReleaseNULL(mUncompressedFirmwareData);
	IOLockUnlock(mUncompressedFirmwareLock);
	return kIOReturnSuccess;
}

OSData * FirmwareManager::getFirmwareUncompressed()
{
	return mUncompressedFirmwareData;
}

char * FirmwareManager::getFirmwareName()
{
	return mFirmwareName;
}

IOReturn FirmwareManager::setFirmwareWithDescriptor(FirmwareDescriptor firmware)
{
	OSData * fwData = OSData::withBytes(firmware.firmwareData, firmware.firmwareSize);
	mFirmwareName = firmware.name;

	if ( isFirmwareCompressed(fwData) )
	{
		if ( !decompressFirmware(fwData) )
		{
			OSSafeReleaseNULL(fwData);
			return kIOReturnSuccess;
		}
		OSSafeReleaseNULL(fwData);
		return kIOReturnError;
	}

	IOLockLock(mUncompressedFirmwareLock);
	mUncompressedFirmwareData = fwData;
	IOLockUnlock(mUncompressedFirmwareLock);
	return kIOReturnSuccess;
}

bool FirmwareManager::isFirmwareCompressed(OSData * firmware)
{
	UInt16 * magic = (UInt16 *) firmware->getBytesNoCopy();

	if ( *magic == 0x0178   // Zlib no compression
	  || *magic == 0x9c78   // Zlib default compression
	  || *magic == 0xda78 ) // Zlib maximum compression
		return true;
	return false;
}

FirmwareManager * FirmwareManager::withName(char * name, FirmwareDescriptor * firmwareList, int numFirmwares)
{
	FirmwareManager * me = OSTypeAlloc(FirmwareManager);

	if ( !me )
		return NULL;
	if ( me->setFirmwareWithName(name, firmwareList, numFirmwares) )
	{
		OSSafeReleaseNULL(me);
		return NULL;
	}
	return me;
}

FirmwareManager * FirmwareManager::withDescriptor(FirmwareDescriptor firmware)
{
	FirmwareManager * me = OSTypeAlloc(FirmwareManager);

	if ( !me )
		return NULL;
	if ( me->setFirmwareWithDescriptor(firmware) )
	{
		OSSafeReleaseNULL(me);
		return NULL;
	}
	return me;
}
