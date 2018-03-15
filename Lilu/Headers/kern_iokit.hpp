//
//  kern_iokit.hpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#ifndef kern_iokit_hpp
#define kern_iokit_hpp

#include <Headers/kern_config.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_patcher.hpp>

#include <libkern/c++/OSSerialize.h>
#include <IOKit/IORegistryEntry.h>

namespace WIOKit {

	/**
	 *  AppleHDAEngine::getLocation teaches us to use while(1) when talking to IOReg
	 *  This feels mad and insane, since it may prevent the system from booting.
	 *  Although this had never happened, we will use a far bigger fail-safe stop value.
	 */
	static constexpr size_t bruteMax {0x10000000};

	/**
	 *  Read typed OSData
	 *
	 *  @param obj    read object
	 *  @param value  read value
	 *  @param name   propert name
	 *
	 *  @return true on success
	 */
	template <typename T>
	inline bool getOSDataValue(const OSObject *obj, const char *name, T &value) {
		if (obj) {
			auto data = OSDynamicCast(OSData, obj);
			if (data && data->getLength() == sizeof(T)) {
				value = *static_cast<const T *>(data->getBytesNoCopy());
				DBGLOG("iokit", "getOSData %s has %llX value", name, static_cast<uint64_t>(value));
				return true;
			} else {
				SYSLOG("iokit", "getOSData %s has unexpected format", name);
			}
		} else {
			DBGLOG("iokit", "getOSData %s was not found", name);
		}
		return false;
	}

	/**
	 *  Read typed OSData from IORegistryEntry
	 *
	 *  @see getOSDataValue
	 */
	template <typename T>
	inline bool getOSDataValue(const IORegistryEntry *sect, const char *name, T &value) {
		return getOSDataValue(sect->getProperty(name), name, value);
	}

	/**
	 *  Read typed OSData from IORegistryEntry
	 *
	 *  @see getOSDataValue
	 */
	template <typename T>
	inline bool getOSDataValue(const OSDictionary *dict, const char *name, T &value) {
		return getOSDataValue(dict->getObject(name), name, value);
	}

	/**
	 *  Retrieve property object
	 *
	 *  @param entry    IORegistry entry
	 *  @param property property name
	 *
	 *  @return property object (must be released) or nullptr
	 */
	EXPORT OSSerialize *getProperty(IORegistryEntry *entry, const char *property);

	/**
	 *  Model variants
	 */
	struct ComputerModel {
		enum {
			ComputerInvalid = 0x0,
			ComputerLaptop  = 0x1,
			ComputerDesktop = 0x2,
			ComputerAny = ComputerLaptop | ComputerDesktop
		};
	};

	/**
	 *  PCI GPU Vendor identifiers
	 */
	struct VendorID {
		enum : uint16_t {
			ATIAMD = 0x1002,
			NVIDIA = 0x10de,
			Intel = 0x8086
		};
	};

	/**
	 *  PCI class codes
	 */
	struct ClassCode {
		enum : uint32_t {
			VGAController = 0x30000,
			DisplayController = 0x38000,
			PCIBridge = 0x60400,
			HDADevice = 0x040300,
			// This does not seem to be documented. It works on Haswell at least.
			IMEI = 0x78000
		};
	};

	/**
	 *  Definitions of PCI Config Registers
	 */
	enum PCIRegister : uint8_t {
		kIOPCIConfigVendorID                = 0x00,
		kIOPCIConfigDeviceID                = 0x02,
		kIOPCIConfigCommand                 = 0x04,
		kIOPCIConfigStatus                  = 0x06,
		kIOPCIConfigRevisionID              = 0x08,
		kIOPCIConfigClassCode               = 0x09,
		kIOPCIConfigCacheLineSize           = 0x0C,
		kIOPCIConfigLatencyTimer            = 0x0D,
		kIOPCIConfigHeaderType              = 0x0E,
		kIOPCIConfigBIST                    = 0x0F,
		kIOPCIConfigBaseAddress0            = 0x10,
		kIOPCIConfigBaseAddress1            = 0x14,
		kIOPCIConfigBaseAddress2            = 0x18,
		kIOPCIConfigBaseAddress3            = 0x1C,
		kIOPCIConfigBaseAddress4            = 0x20,
		kIOPCIConfigBaseAddress5            = 0x24,
		kIOPCIConfigCardBusCISPtr           = 0x28,
		kIOPCIConfigSubSystemVendorID       = 0x2C,
		kIOPCIConfigSubSystemID             = 0x2E,
		kIOPCIConfigExpansionROMBase        = 0x30,
		kIOPCIConfigCapabilitiesPtr         = 0x34,
		kIOPCIConfigInterruptLine           = 0x3C,
		kIOPCIConfigInterruptPin            = 0x3D,
		kIOPCIConfigMinimumGrant            = 0x3E,
		kIOPCIConfigMaximumLatency          = 0x3F
	};

	/**
	 *  Fixed offsets for PCI Config I/O virtual methods
	 */
	struct PCIConfigOffset {
		enum : size_t {
			ConfigRead32      = 0x10A,
			ConfigWrite32     = 0x10B,
			ConfigRead16      = 0x10C,
			ConfigWrite16     = 0x10D,
			ConfigRead8       = 0x10E,
			ConfigWrite8      = 0x10F,
			GetBusNumber      = 0x11D,
			GetDeviceNumber   = 0x11E,
			GetFunctionNumber = 0x11F
		};
	};

	/**
	 *  PCI Config I/O method prototypes
	 */
	using t_PCIConfigRead32 = uint32_t (*)(IORegistryEntry *service, uint32_t space, uint8_t offset);
	using t_PCIConfigRead16 = uint16_t (*)(IORegistryEntry *service, uint32_t space, uint8_t offset);
	using t_PCIConfigRead8  = uint8_t  (*)(IORegistryEntry *service, uint32_t space, uint8_t offset);
	using t_PCIConfigWrite32 = void (*)(IORegistryEntry *service, uint32_t space, uint8_t offset, uint32_t data);
	using t_PCIConfigWrite16 = void (*)(IORegistryEntry *service, uint32_t space, uint8_t offset, uint16_t data);
	using t_PCIConfigWrite8  = void (*)(IORegistryEntry *service, uint32_t space, uint8_t offset, uint8_t data);
	using t_PCIGetBusNumber = uint8_t (*)(IORegistryEntry *service);
	using t_PCIGetDeviceNumber = uint8_t (*)(IORegistryEntry *service);
	using t_PCIGetFunctionNumber = uint8_t (*)(IORegistryEntry *service);

	/**
	 *  Read PCI Config register
	 *
	 *  @param service  IOPCIDevice-compatible service.
	 *  @param reg      PCI config register
	 *  @param space    adress space
	 *  @param size     read size for reading custom registers
	 */
	inline uint32_t readPCIConfigValue(IORegistryEntry *service, uint32_t reg, uint32_t space = 0, uint32_t size = 0) {
		auto read32 = reinterpret_cast<t_PCIConfigRead32 **>(service)[0][PCIConfigOffset::ConfigRead32];
		auto read16 = reinterpret_cast<t_PCIConfigRead16 **>(service)[0][PCIConfigOffset::ConfigRead16];
		auto read8  = reinterpret_cast<t_PCIConfigRead8  **>(service)[0][PCIConfigOffset::ConfigRead8];

		if (space == 0) {
			space = getMember<uint32_t>(service, 0xA8);
			DBGLOG("igfx", "read pci config discovered %s space to be 0x%08X", safeString(service->getName()), space);
		}

		if (size != 0) {
			switch (size) {
				case 8:
					return read8(service, space, reg);
				case 16:
					return read16(service, space, reg);
				case 32:
				default:
					return read32(service, space, reg);
			}
		}

		switch (reg) {
			case kIOPCIConfigVendorID:
				return read16(service, space, reg);
			case kIOPCIConfigDeviceID:
				return read16(service, space, reg);
			case kIOPCIConfigCommand:
				return read16(service, space, reg);
			case kIOPCIConfigStatus:
				return read16(service, space, reg);
			case kIOPCIConfigRevisionID:
				return read8(service, space, reg);
			case kIOPCIConfigClassCode:
				return read32(service, space, reg);
			case kIOPCIConfigCacheLineSize:
				return read8(service, space, reg);
			case kIOPCIConfigLatencyTimer:
				return read8(service, space, reg);
			case kIOPCIConfigHeaderType:
				return read8(service, space, reg);
			case kIOPCIConfigBIST:
				return read8(service, space, reg);
			case kIOPCIConfigBaseAddress0:
				return read32(service, space, reg);
			case kIOPCIConfigBaseAddress1:
				return read32(service, space, reg);
			case kIOPCIConfigBaseAddress2:
				return read32(service, space, reg);
			case kIOPCIConfigBaseAddress3:
				return read32(service, space, reg);
			case kIOPCIConfigBaseAddress4:
				return read32(service, space, reg);
			case kIOPCIConfigBaseAddress5:
				return read32(service, space, reg);
			case kIOPCIConfigCardBusCISPtr:
				return read32(service, space, reg);
			case kIOPCIConfigSubSystemVendorID:
				return read16(service, space, reg);
			case kIOPCIConfigSubSystemID:
				return read16(service, space, reg);
			case kIOPCIConfigExpansionROMBase:
				return read32(service, space, reg);
			case kIOPCIConfigCapabilitiesPtr:
				return read32(service, space, reg);
			case kIOPCIConfigInterruptLine:
				return read8(service, space, reg);
			case kIOPCIConfigInterruptPin:
				return read8(service, space, reg);
			case kIOPCIConfigMinimumGrant:
				return read8(service, space, reg);
			case kIOPCIConfigMaximumLatency:
				return read8(service, space, reg);
			default:
				return read32(service, space, reg);
		}
	}

	/**
	 *  Retrieve PCI device address
	 *
	 *  @param service   IOPCIDevice-compatible service.
	 *  @param bus       bus address
	 *  @param device    device address
	 *  @param function  function address
	 */
	inline void getDeviceAddress(IORegistryEntry *service, uint8_t &bus, uint8_t &device, uint8_t &function) {
		auto getBus = reinterpret_cast<t_PCIGetBusNumber **>(service)[0][PCIConfigOffset::GetBusNumber];
		auto getDevice = reinterpret_cast<t_PCIGetDeviceNumber **>(service)[0][PCIConfigOffset::GetDeviceNumber];
		auto getFunction = reinterpret_cast<t_PCIGetFunctionNumber **>(service)[0][PCIConfigOffset::GetFunctionNumber];

		bus = getBus(service);
		device = getDevice(service);
		function = getFunction(service);
	}

	/**
	 *  Retrieve the computer type
	 *
	 *  @return valid computer type or ComputerAny
	 */
	EXPORT int getComputerModel();

	/**
	 *  Retrieve computer model and/or board-id properties
	 *
	 *  @param model    model name output buffer or null
	 *  @param modelsz  model name output buffer size
	 *  @param board    board identifier output buffer or null
	 *  @param boardsz  board identifier output buffer size
	 *
	 *  @return true if relevant properties already are available, otherwise buffers are unchanged
	 */
	EXPORT bool getComputerInfo(char *model, size_t modelsz, char *board, size_t boardsz);

	/**
	 *  Retrieve an ioreg entry by path/prefix
	 *
	 *  @param path    an exact lookup path
	 *  @param prefix  entry prefix at path
	 *  @param plane   plane to lookup in
	 *  @param proc    process every found entry with the method
	 *  @param brute   kick ioreg until a value is found
	 *  @param user    pass some value to the callback function
	 *
	 *  @return entry pointer (must NOT be released) or nullptr (on failure or in proc mode)
	 */
	EXPORT IORegistryEntry *findEntryByPrefix(const char *path, const char *prefix, const IORegistryPlane *plane, bool (*proc)(void *, IORegistryEntry *)=nullptr, bool brute=false, void *user=nullptr);

	/**
	 *  Retrieve an ioreg entry by path/prefix
	 *
	 *  @param entry   an ioreg entry to look in
	 *  @param prefix  entry prefix at path
	 *  @param plane   plane to lookup in
	 *  @param proc    process every found entry with the method
	 *  @param brute   kick ioreg until a value is found
	 *  @param user    pass some value to the callback function
	 *
	 *  @return entry pointer (must NOT be released) or nullptr (on failure or in proc mode)
	 */
	EXPORT IORegistryEntry *findEntryByPrefix(IORegistryEntry *entry, const char *prefix, const IORegistryPlane *plane, bool (*proc)(void *, IORegistryEntry *)=nullptr, bool brute=false, void *user=nullptr);

	/**
	 *  Check if we are using prelinked kernel/kexts or not
	 *
	 *  @return true when confirmed that we definitely are
	 */
	EXPORT bool usingPrelinkedCache();

	/**
	 *  Properly rename the device
	 *
	 *  @param  entry   device to rename
	 *  @param  name    new name
	 *  @param  compat  correct compatible
	 *
	 *  @return true on success
	 */
	inline bool renameDevice(IORegistryEntry *entry, const char *name, bool compat=true) {
		if (!entry || !name)
			return false;

		entry->setName(name);

		if (!compat)
			return true;

		auto compatibleProp = OSDynamicCast(OSData, entry->getProperty("compatible"));
		if (!compatibleProp)
			return true;

		uint32_t compatibleSz = compatibleProp->getLength();
		auto compatibleStr = static_cast<const char *>(compatibleProp->getBytesNoCopy());
		DBGLOG("iokit", "compatible property starts with %s and is %u bytes", compatibleStr ? compatibleStr : "(null)", compatibleSz);

		if (compatibleStr) {
			for (uint32_t i = 0; i < compatibleSz; i++) {
				if (!strcmp(&compatibleStr[i], name)) {
					DBGLOG("iokit", "found %s in compatible, ignoring", name);
					return true;
				}

				i += strlen(&compatibleStr[i]);
			}

			uint32_t nameSize = static_cast<uint32_t>(strlen(name)+1);
			uint32_t compatibleBufSz = compatibleSz + nameSize;
			uint8_t *compatibleBuf = Buffer::create<uint8_t>(compatibleBufSz);
			if (compatibleBuf) {
				DBGLOG("iokit", "fixing compatible to have %s", name);
				lilu_os_memcpy(&compatibleBuf[0], compatibleStr, compatibleSz);
				lilu_os_memcpy(&compatibleBuf[compatibleSz], name, nameSize);
				entry->setProperty("compatible", OSData::withBytes(compatibleBuf, compatibleBufSz));
				return true;
			} else {
				SYSLOG("iokit", "compatible property memory alloc failure %u for %s", compatibleBufSz, name);
			}
		}

		return false;
	}
}

#endif /* kern_iokit_hpp */
