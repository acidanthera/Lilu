//
//  kern_iokit.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_patcher.hpp>
#include <IOKit/IOService.h>

#include <libkern/c++/OSSerialize.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IODeviceTreeSupport.h>

namespace WIOKit {

	OSSerialize *getProperty(IORegistryEntry *entry, const char *property) {
		auto value = entry->getProperty(property);
		if (value) {
			auto s = OSSerialize::withCapacity(PAGE_SIZE);
			if (!s)
				return nullptr;
			if (value->serialize(s)) {
				return s;
			} else {
				SYSLOG("iokit", "failed to serialise %s property", property);
				s->release();
			}
		} else {
			DBGLOG("iokit", "failed to get %s property", property);
		}
		return nullptr;
	}

	bool awaitPublishing(IORegistryEntry *obj) {
		size_t counter = 0;
		while (counter < 256) {
			if (obj->inPlane(gIOServicePlane)) {
				DBGLOG("dev", "pci device %s is in service plane %lu", safeString(obj->getName()), counter);
				return true;
			}
			DBGLOG("dev", "pci device %s is not in service plane %lu, polling", safeString(obj->getName()), counter);
			++counter;
			IOSleep(20);
		}

		SYSLOG("dev", "found dead pci device %s", safeString(obj->getName()));
		return false;
	}

	uint32_t readPCIConfigValue(IORegistryEntry *service, uint32_t reg, uint32_t space, uint32_t size) {
		if (!awaitPublishing(service))
			return 0xffffffff;

		auto read32 = reinterpret_cast<t_PCIConfigRead32 **>(service)[0][PCIConfigOffset::ConfigRead32];
		auto read16 = reinterpret_cast<t_PCIConfigRead16 **>(service)[0][PCIConfigOffset::ConfigRead16];
		auto read8  = reinterpret_cast<t_PCIConfigRead8  **>(service)[0][PCIConfigOffset::ConfigRead8];

		if (space == 0) {
			space = getMember<uint32_t>(service, 0xA8);
			DBGLOG("iokit", "read pci config discovered %s space to be 0x%08X", safeString(service->getName()), space);
		}

		if (size != 0) {
			switch (size) {
				case 8:
					return read8(service, space, reg);
				case 16:
					return read16(service, space, reg);
				default: /* assume 32-bit otherwise */
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
				return read32(service, space, kIOPCIConfigRevisionID) >> 8;
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
				return read8(service, space, reg);
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

	void getDeviceAddress(IORegistryEntry *service, uint8_t &bus, uint8_t &device, uint8_t &function) {
		auto getBus = reinterpret_cast<t_PCIGetBusNumber **>(service)[0][PCIConfigOffset::GetBusNumber];
		auto getDevice = reinterpret_cast<t_PCIGetDeviceNumber **>(service)[0][PCIConfigOffset::GetDeviceNumber];
		auto getFunction = reinterpret_cast<t_PCIGetFunctionNumber **>(service)[0][PCIConfigOffset::GetFunctionNumber];

		bus = getBus(service);
		device = getDevice(service);
		function = getFunction(service);
	}

	int getComputerModel() {
		return BaseDeviceInfo::get().modelType;
	}

	bool getComputerInfo(char *model, size_t modelsz, char *board, size_t boardsz) {
		if (model && modelsz > 0)
			lilu_strlcpy(model, BaseDeviceInfo::get().modelIdentifier, modelsz);
		if (board && boardsz > 0)
			lilu_strlcpy(board, BaseDeviceInfo::get().boardIdentifier, boardsz);
		return true;
	}

	IORegistryEntry *findEntryByPrefix(const char *path, const char *prefix, const IORegistryPlane *plane, bool (*proc)(void *, IORegistryEntry *), bool brute, void *user) {
		auto entry = IORegistryEntry::fromPath(path, plane);
		if (entry) {
			auto res = findEntryByPrefix(entry, prefix, plane, proc, brute, user);
			entry->release();
			return res;
		}
		DBGLOG("iokit", "failed to get %s entry", path);
		return nullptr;
	}


	IORegistryEntry *findEntryByPrefix(IORegistryEntry *entry, const char *prefix, const IORegistryPlane *plane, bool (*proc)(void *, IORegistryEntry *), bool brute, void *user) {
		bool found {false};
		IORegistryEntry *res {nullptr};

		size_t bruteCount {0};

		do {
			bruteCount++;
			auto iterator = entry->getChildIterator(plane);

			if (iterator) {
				size_t len = strlen(prefix);
				while ((res = OSDynamicCast(IORegistryEntry, iterator->getNextObject())) != nullptr) {
					const char *resname = res->getName();

					if (resname && !strncmp(prefix, resname, len)) {
						if (!proc || proc(user, res)) {
							found = true;
							if (bruteCount > 1)
								DBGLOG("iokit", "bruted %s value in %lu attempts", prefix, bruteCount);
							if (!proc) {
								break;
							}
						}
					}
				}

				iterator->release();
			} else {
				SYSLOG("iokit", "failed to iterate over entry");
				return nullptr;
			}

		} while (brute && bruteCount < bruteMax && !found);

		if (!found)
			DBGLOG("iokit", "failed to find %s", prefix);
		return proc ? nullptr : res;
	}

	bool usingPrelinkedCache() {
		auto root = IORegistryEntry::getRegistryRoot();
		if (root) {
			auto count = OSDynamicCast(OSNumber, root->getProperty("OSPrelinkKextCount"));
			if (count) {
				DBGLOG("iokit", "OSPrelinkKextCount equals to %u", count->unsigned32BitValue());
				return count->unsigned32BitValue() > 0;
			} else {
				DBGLOG("iokit", "missing OSPrelinkKextCount property!");
			}
		} else {
			SYSLOG("iokit", "missing registry root!");
		}

		return false;
	}

	bool renameDevice(IORegistryEntry *entry, const char *name, bool compat) {
		if (!entry || !name)
			return false;

		entry->setName(name);

		if (!compat)
			return true;

		auto compatibleProp = OSDynamicCast(OSData, entry->getProperty("compatible"));
		if (!compatibleProp)
			return true;

		auto size = compatibleProp->getLength();
		auto bytes = static_cast<const char *>(compatibleProp->getBytesNoCopy());
		auto nameLength = strlen(name);
		for (size_t pos = 0; pos < size;) {
			auto item = bytes + pos;
			auto end = static_cast<const char *>(lilu_os_memchr(item, 0, size - pos));
			size_t length = end ? static_cast<size_t>(end - item) : size - pos;
			if (length == nameLength && !memcmp(item, name, length))
				return true;
			if (!end)
				break;
			pos += length + 1;
		}

		// Some firmware omits the last string's NUL separator.
		bool needsSeparator = size && bytes[size - 1] != '\0';
		size_t terminators = needsSeparator ? 2 : 1;
		if (size > Buffer::BufferMax - terminators || nameLength > Buffer::BufferMax - terminators - size)
			return false;
		auto data = OSData::withCapacity(static_cast<uint32_t>(size + nameLength + terminators));
		if (!data)
			return false;
		bool ok = (!size || data->appendBytes(bytes, size)) &&
			(!needsSeparator || data->appendBytes("", 1)) &&
			data->appendBytes(name, static_cast<uint32_t>(nameLength + 1)) &&
			entry->setProperty("compatible", data);
		data->release();
		return ok;
	}
}
