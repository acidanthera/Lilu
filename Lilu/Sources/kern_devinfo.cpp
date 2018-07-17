//
//  kern_devinfo.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/kern_cpu.hpp>
#include <IOKit/IODeviceTreeSupport.h>

void DeviceInfo::updateLayoutId() {
	reportedLayoutId = DefaultReportedLayoutId;
	if (PE_parse_boot_argn(ReportedLayoutIdArg, &reportedLayoutId, sizeof(reportedLayoutId))) {
		DBGLOG("dev", "found boot-arg layout id override to %d", reportedLayoutId);
	} else if (audioBuiltinAnalog && WIOKit::getOSDataValue(audioBuiltinAnalog, ReportedLayoutIdName, reportedLayoutId)) {
		DBGLOG("dev", "found property layout id override to %d", reportedLayoutId);
	}
}

void DeviceInfo::updateFramebufferId() {
	if (!videoBuiltin)
		return;

	auto gen = CPUInfo::getGeneration();
	if (gen != CPUInfo::CpuGeneration::SandyBridge)
		reportedFramebufferName = ReportedFrameIdName;
	else
		reportedFramebufferName = ReportedFrameIdLegacyName;

	if (PE_parse_boot_argn(ReportedFrameIdArg, &reportedFramebufferId, sizeof(reportedFramebufferId))) {
		DBGLOG("dev", "found boot-arg frame id override to %08X", reportedFramebufferId);
	} else if (WIOKit::getOSDataValue(videoBuiltin, reportedFramebufferName, reportedFramebufferId)) {
		DBGLOG("dev", "found property frame id override to %d", reportedFramebufferId);
	} else {
		auto legacy = getLegacyFramebufferId();
		if (gen == CPUInfo::CpuGeneration::SandyBridge && legacy != DefaultInvalidPlatformId) {
			reportedFramebufferId = getLegacyFramebufferId();
		} else {
			if (videoExternal.size() > 0 && gen != CPUInfo::CpuGeneration::Broadwell) {
				DBGLOG("igfx", "discovered external GPU, using frame without connectors");
				// Note, that setting non-standard connector-less frame may result in 2 GPUs visible
				// in System Report for whatever reason (at least on KabyLake).
				if (gen == CPUInfo::CpuGeneration::SandyBridge)
					reportedFramebufferId = ConnectorLessSandyBridgePlatformId2;
				else if (gen == CPUInfo::CpuGeneration::IvyBridge)
					reportedFramebufferId = ConnectorLessIvyBridgePlatformId2;
				else if (gen == CPUInfo::CpuGeneration::Haswell)
					reportedFramebufferId = ConnectorLessHaswellPlatformId1;
				else if (gen == CPUInfo::CpuGeneration::Skylake)
					reportedFramebufferId = ConnectorLessSkylakePlatformId3;
				else if (gen == CPUInfo::CpuGeneration::KabyLake)
					reportedFramebufferId = ConnectorLessKabyLakePlatformId2;
				else
					reportedFramebufferId = DefaultInvalidPlatformId;
			} else {
				// These are really failsafe defaults, you should NOT rely on them.
				auto model = WIOKit::getComputerModel();
				if (model == WIOKit::ComputerModel::ComputerLaptop) {
					if (gen == CPUInfo::CpuGeneration::SandyBridge)
						reportedFramebufferId = 0x00010000;
					else if (gen == CPUInfo::CpuGeneration::IvyBridge)
						reportedFramebufferId = 0x01660003;
					else if (gen == CPUInfo::CpuGeneration::Haswell)
						reportedFramebufferId = 0x0A160000;
					else if (gen == CPUInfo::CpuGeneration::Broadwell)
						reportedFramebufferId = 0x16260006;
					else if (gen == CPUInfo::CpuGeneration::Skylake)
						reportedFramebufferId = 0x19160000;
					else if (gen == CPUInfo::CpuGeneration::KabyLake)
						reportedFramebufferId = 0x591B0000;
					else
						reportedFramebufferId = DefaultInvalidPlatformId;
				} else {
					if (gen == CPUInfo::CpuGeneration::SandyBridge)
						reportedFramebufferId = 0x00030010;
					else if (gen == CPUInfo::CpuGeneration::IvyBridge)
						reportedFramebufferId = 0x0166000A;
					else if (gen == CPUInfo::CpuGeneration::Haswell)
						reportedFramebufferId = 0x0D220003;
					else if (gen == CPUInfo::CpuGeneration::Broadwell)
						reportedFramebufferId = 0x16220007;  /* for now */
					else if (gen == CPUInfo::CpuGeneration::Skylake)
						reportedFramebufferId = DefaultSkylakePlatformId;
					else if (gen == CPUInfo::CpuGeneration::KabyLake)
						reportedFramebufferId = DefaultKabyLakePlatformId;
					else
						reportedFramebufferId = DefaultInvalidPlatformId;
				}
			}
		}
	}

	reportedFramebufferIsConnectorLess = isConnectorLessPlatformId(reportedFramebufferId);
}

void DeviceInfo::updateFirmwareVendor() {
	auto entry = IORegistryEntry::fromPath("/efi", gIODTPlane);
	if (entry) {
		auto ven = OSDynamicCast(OSData, entry->getProperty("firmware-vendor"));
		if (ven) {
			auto bytes = ven->getBytesNoCopy();
			size_t len = ven->getLength();
			if (bytes && len > 0) {
				struct Matching {
					FirmwareVendor ven;
					const char16_t *str;
					size_t len;
				};

				//TODO: Add more vendors here (like branded Phoenix or Intel).
				Matching matching[] {
					{FirmwareVendor::Apple, u"Apple", sizeof(u"Apple")},
					{FirmwareVendor::Parallels, u"Parallels Software International Inc.", sizeof(u"Parallels Software International Inc.")},
					{FirmwareVendor::VMware, u"VMware, Inc.", sizeof(u"VMware, Inc.")},
					{FirmwareVendor::EDKII, u"EDK II", sizeof(u"EDK II")},
					{FirmwareVendor::AMI, u"American Megatrends", sizeof(u"American Megatrends")},
					{FirmwareVendor::AMI, u"American Megatrends Inc.", sizeof(u"American Megatrends Inc.")},
					{FirmwareVendor::Insyde, u"INSYDE Corp.", sizeof(u"INSYDE Corp.")},
					{FirmwareVendor::Phoenix, u"Phoenix Technologies Ltd.", sizeof(u"Phoenix Technologies Ltd.")},
					{FirmwareVendor::HP, u"HPQ", sizeof(u"HPQ")}
				};

				for (size_t i = 0; i < arrsize(matching); i++) {
					if (len == matching[i].len &&
						!memcmp(bytes, matching[i].str, len)) {
						firmwareVendor = matching[i].ven;
						DBGLOG("dev", "detected %d firmware", firmwareVendor);
						break;
					}
				}
			}
		} else {
			SYSLOG("dev", "failed to obtain firmware vendor");
		}

		entry->release();
	} else {
		SYSLOG("dev", "failed to obtain efi tree");
	}
}

uint32_t DeviceInfo::getLegacyFramebufferId() {
	char boardIdentifier[64] {};
	if (WIOKit::getComputerInfo(nullptr, 0, boardIdentifier, sizeof(boardIdentifier))) {
		struct {
			const char *boardId;
			uint32_t platformId;
		} sandyBoards[] = {
			{"Mac-94245B3640C91C81", 0x10000},
			{"Mac-94245AF5819B141B", 0x10000},
			{"Mac-94245A3940C91C80", 0x10000},
			{"Mac-942459F5819B171B", 0x10000},
			{"Mac-8ED6AF5B48C039E1", 0x30010}, // or 0x30020
			{"Mac-7BA5B2794B2CDB12", 0x30010}, // or 0x30020
			{"Mac-4BC72D62AD45599E", 0x30030},
			{"Mac-742912EFDBEE19B3", 0x40000},
			{"Mac-C08A6BB70A942AC2", 0x40000},
			{"Mac-942B5BF58194151B", 0x50000},
			{"Mac-942B5B3A40C91381", 0x50000},
			{"Mac-942B59F58194171B", 0x50000}
		};
		for (size_t i = 0; i < arrsize(sandyBoards); i++)
			if (!strcmp(sandyBoards[i].boardId, boardIdentifier))
				return sandyBoards[i].platformId;
	} else {
		SYSLOG("dev", "failed to obtain board-id");
	}

	return DefaultInvalidPlatformId;
}

bool DeviceInfo::isConnectorLessPlatformId(uint32_t id) {
	return
	id == ConnectorLessSandyBridgePlatformId1 ||
	id == ConnectorLessSandyBridgePlatformId2 ||
	id == ConnectorLessIvyBridgePlatformId1 ||
	id == ConnectorLessIvyBridgePlatformId2 ||
	id == ConnectorLessHaswellPlatformId1 ||
	id == ConnectorLessHaswellPlatformId2 ||
	id == ConnectorLessSkylakePlatformId1 ||
	id == ConnectorLessSkylakePlatformId2 ||
	id == ConnectorLessSkylakePlatformId3 ||
	id == ConnectorLessSkylakePlatformId4 ||
	id == ConnectorLessKabyLakePlatformId1 ||
	id == ConnectorLessKabyLakePlatformId2;
}

void DeviceInfo::grabDevicesFromPciRoot(IORegistryEntry *pciRoot) {
	auto iterator = pciRoot->getChildIterator(gIODTPlane);
	if (iterator) {
		IORegistryEntry *obj = nullptr;
		while ((obj = OSDynamicCast(IORegistryEntry, iterator->getNextObject())) != nullptr) {
			uint32_t vendor = 0, code = 0;
			//TODO: AMD support if at all?
			if (!WIOKit::getOSDataValue(obj, "vendor-id", vendor) || vendor != WIOKit::VendorID::Intel ||
				!WIOKit::getOSDataValue(obj, "class-code", code))
				continue;

			auto name = obj->getName();
			if (code == WIOKit::ClassCode::DisplayController || code == WIOKit::ClassCode::VGAController) {
				DBGLOG("dev", "found IGPU device %s", safeString(name));
				videoBuiltin = obj;
			} else if (code == WIOKit::ClassCode::HDADevice) {
				if (name && (!strcmp(name, "HDAU") || !strcmp(name, "B0D3"))) {
					DBGLOG("dev", "found HDAU device %s", safeString(name));
					audioBuiltinDigital = obj;
				} else {
					DBGLOG("dev", "found HDEF device %s", safeString(name));
					audioBuiltinAnalog = obj;
				}
			} else if (code == WIOKit::ClassCode::IMEI || (name &&
				(!strcmp(name, "IMEI") || !strcmp(name, "HECI") || !strcmp(name, "MEI")))) {
				// Fortunately IMEI is always made by Intel
				DBGLOG("dev", "found IMEI device %s", safeString(name));
				managementEngine = obj;
			} else if (code == WIOKit::ClassCode::PCIBridge) {
				DBGLOG("dev", "found pci bridge %s", safeString(name));
				auto pciiterator = IORegistryIterator::iterateOver(obj, gIOServicePlane, kIORegistryIterateRecursively);
				if (pciiterator) {
					ExternalVideo v {};
					IORegistryEntry *pciobj = nullptr;
					while ((pciobj = OSDynamicCast(IORegistryEntry, pciiterator->getNextObject())) != nullptr) {
						uint32_t pcivendor = 0, pcicode = 0;
						DBGLOG("dev", "found %s on pci bridge", safeString(pciobj->getName()));
						if (WIOKit::getOSDataValue(pciobj, "vendor-id", pcivendor) &&
							WIOKit::getOSDataValue(pciobj, "class-code", pcicode)) {

							if (pcicode == WIOKit::ClassCode::DisplayController ||
								pcicode == WIOKit::ClassCode::VGAController) {
								DBGLOG("dev", "found GFX0 device %s at %s by %04X",
									   safeString(pciobj->getName()), safeString(name),  pcivendor);
								v.video = pciobj;
								v.vendor = pcivendor;
							} else if (pcicode == WIOKit::ClassCode::HDADevice) {
								DBGLOG("dev", "found HDAU device %s at %s by %04X",
									   safeString(pciobj->getName()), safeString(name), pcivendor);
								v.audio = pciobj;
							}
						}
					}

					pciiterator->release();

					if (v.video) {
						if (!videoExternal.push_back(v))
							SYSLOG("dev", "failed to push video gpu");
					}
				}
			}
		}

		iterator->release();
	} else {
		SYSLOG("dev", "failed to obtain PCI devices iterator from %s", safeString(pciRoot->getName()));
	}
}

DeviceInfo *DeviceInfo::create() {
	auto list = new DeviceInfo;
	if (!list) {
		SYSLOG("dev", "failed to allocate device list");
		return nullptr;
	}

	auto rootSect = IORegistryEntry::fromPath("/", gIODTPlane);
	if (rootSect) {
		// Find every PCI root, X299 may have many
		auto lookupIterator = rootSect->getChildIterator(gIODTPlane);
		if (lookupIterator) {
			IORegistryEntry *pciRootObj = nullptr;
			while ((pciRootObj = OSDynamicCast(IORegistryEntry, lookupIterator->getNextObject())) != nullptr) {
				auto compat = OSDynamicCast(OSData, pciRootObj->getProperty("compatible"));

				bool isPciRoot = compat && compat->getLength() == sizeof("PNP0A03") &&
					!strcmp(static_cast<const char *>(compat->getBytesNoCopy()), "PNP0A03");

				// This is just a safeguard really, the upper check should find every value.
				if (!isPciRoot && compat) {
					auto name = pciRootObj->getName();
					isPciRoot = name && (!strncmp(name, "PCI", 3) || !strncmp(name, "PC0", 3));
				}

				if (isPciRoot) {
					DBGLOG("dev", "found PCI root %s", safeString(pciRootObj->getName()));
					list->grabDevicesFromPciRoot(pciRootObj);
				}
			}

			lookupIterator->release();

			list->updateLayoutId();
			list->updateFramebufferId();
			list->updateFirmwareVendor();
		} else {
			SYSLOG("dev", "failed to obtain PCI lookup iterator");
		}

		rootSect->release();
	} else {
		SYSLOG("dev", "failed to find PCI devices");
	}

	return list;
}

void DeviceInfo::deleter(DeviceInfo *d) {
	if (d) {
		d->videoExternal.deinit();
		delete d;
	}
}
