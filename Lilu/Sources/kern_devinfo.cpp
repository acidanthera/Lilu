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

BaseDeviceInfo globalBaseDeviceInfo;
DeviceInfo *globalDeviceInfo;

void DeviceInfo::updateLayoutId() {
	reportedLayoutId = DefaultReportedLayoutId;
	if (PE_parse_boot_argn(ReportedLayoutIdArg, &reportedLayoutId, sizeof(reportedLayoutId))) {
		DBGLOG("dev", "found boot-arg layout id override to %u", reportedLayoutId);
	} else if (audioBuiltinAnalog && WIOKit::getOSDataValue(audioBuiltinAnalog, ReportedLayoutIdName, reportedLayoutId)) {
		DBGLOG("dev", "found property layout id override to %u", reportedLayoutId);
	}
}

void DeviceInfo::updateFramebufferId() {
	if (!videoBuiltin)
		return;

	auto gen = BaseDeviceInfo::get().cpuGeneration;
	if (gen != CPUInfo::CpuGeneration::SandyBridge)
		reportedFramebufferName = ReportedFrameIdName;
	else
		reportedFramebufferName = ReportedFrameIdLegacyName;

	if (PE_parse_boot_argn(ReportedFrameIdArg, &reportedFramebufferId, sizeof(reportedFramebufferId))) {
		DBGLOG("dev", "found boot-arg frame id override to %08X", reportedFramebufferId);
	} else if (checkKernelArgument(ReportedVesaIdArg)) {
		DBGLOG("dev", "found vesa boot-arg frame id");
		reportedFramebufferId = DefaultVesaPlatformId;
	} else if (WIOKit::getOSDataValue(videoBuiltin, reportedFramebufferName, reportedFramebufferId)) {
		DBGLOG("dev", "found property frame id override to %u", reportedFramebufferId);
	} else {
		auto legacy = getLegacyFramebufferId();
		if (gen == CPUInfo::CpuGeneration::SandyBridge && legacy != DefaultVesaPlatformId) {
			reportedFramebufferId = getLegacyFramebufferId();
		} else {
			if (!requestedExternalSwitchOff && videoExternal.size() > 0 && gen != CPUInfo::CpuGeneration::Broadwell &&
				gen != CPUInfo::CpuGeneration::CannonLake && gen != CPUInfo::CpuGeneration::IceLake) {
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
				else if (gen == CPUInfo::CpuGeneration::CoffeeLake)
					reportedFramebufferId = ConnectorLessCoffeeLakePlatformId2;
				else if (gen == CPUInfo::CpuGeneration::CometLake)
					reportedFramebufferId = ConnectorLessCoffeeLakePlatformId4;
				else
					reportedFramebufferId = DefaultVesaPlatformId;
			} else {
				// These are really failsafe defaults, you should NOT rely on them.
				auto model = BaseDeviceInfo::get().modelType;
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
					else if (gen == CPUInfo::CpuGeneration::CoffeeLake || gen == CPUInfo::CpuGeneration::CometLake)
						reportedFramebufferId = 0x3EA50009;
					else if (gen == CPUInfo::CpuGeneration::CannonLake)
						reportedFramebufferId = 0x5A590000;
					else if (gen == CPUInfo::CpuGeneration::IceLake)
						reportedFramebufferId = 0x8A520000;
					else
						reportedFramebufferId = DefaultVesaPlatformId;
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
						reportedFramebufferId = DefaultAppleSkylakePlatformId;
					else if (gen == CPUInfo::CpuGeneration::KabyLake)
						reportedFramebufferId = DefaultAppleKabyLakePlatformId;
					else if (gen == CPUInfo::CpuGeneration::CoffeeLake || gen == CPUInfo::CpuGeneration::CometLake)
						reportedFramebufferId = 0x3E9B0007;
					else if (gen == CPUInfo::CpuGeneration::CannonLake)
						reportedFramebufferId = DefaultAppleCannonLakePlatformId;
					else if (gen == CPUInfo::CpuGeneration::IceLake)
						reportedFramebufferId = DefaultAppleIceLakeRealPlatformId;
					else
						reportedFramebufferId = DefaultVesaPlatformId;
				}
			}
		}
	}

	reportedFramebufferIsConnectorLess = isConnectorLessPlatformId(reportedFramebufferId);
}

uint32_t DeviceInfo::getLegacyFramebufferId() {
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
		if (!strcmp(sandyBoards[i].boardId, BaseDeviceInfo::get().boardIdentifier))
			return sandyBoards[i].platformId;

	return DefaultVesaPlatformId;
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
	id == ConnectorLessKabyLakePlatformId2 ||
	id == ConnectorLessCoffeeLakePlatformId1 ||
	id == ConnectorLessCoffeeLakePlatformId2 ||
	id == ConnectorLessCoffeeLakePlatformId3 ||
	id == ConnectorLessCoffeeLakePlatformId4 ||
	id == ConnectorLessCoffeeLakePlatformId5 ||
	id == ConnectorLessCoffeeLakePlatformId6;
}

void DeviceInfo::awaitPublishing(IORegistryEntry *obj) {
	size_t counter = 0;
	while (counter < 256) {
		if (obj->getProperty("IOPCIConfigured")) {
			DBGLOG("dev", "pci bridge %s is configured %lu", safeString(obj->getName()), counter);
			break;
		}
		SYSLOG("dev", "pci bridge %s is not configured %lu, polling", safeString(obj->getName()), counter);
		++counter;
		IOSleep(20);
	}

	if (counter == 256)
		SYSLOG("dev", "found unconfigured pci bridge %s", safeString(obj->getName()));
}

void DeviceInfo::grabDevicesFromPciRoot(IORegistryEntry *pciRoot) {
	awaitPublishing(pciRoot);

	auto iterator = pciRoot->getChildIterator(gIODTPlane);
	if (iterator) {
		IORegistryEntry *obj = nullptr;
		while ((obj = OSDynamicCast(IORegistryEntry, iterator->getNextObject())) != nullptr) {
			uint32_t vendor = 0, code = 0;
			bool gotVendor = WIOKit::getOSDataValue(obj, "vendor-id", vendor);
			bool gotClass = WIOKit::getOSDataValue(obj, "class-code", code);
			auto name = obj->getName();
			DBGLOG("dev", "found pci device %s 0x%x 0x%x", safeString(name), vendor, code);

			// Strip interface, as we only care about class and subclass
			code &= WIOKit::ClassCode::PCISubclassMask;

			if (!gotVendor || !gotClass || (vendor != WIOKit::VendorID::Intel && vendor != WIOKit::VendorID::ATIAMD &&
			                                vendor != WIOKit::VendorID::AMDZEN && vendor != WIOKit::VendorID::VMware &&
			                                vendor != WIOKit::VendorID::QEMU && vendor != WIOKit::VendorID::NVIDIA))
				continue;

			if (vendor == WIOKit::VendorID::Intel && (code == WIOKit::ClassCode::DisplayController || code == WIOKit::ClassCode::VGAController)) {
				DBGLOG("dev", "found IGPU device %s", safeString(name));
				videoBuiltin = obj;
				requestedExternalSwitchOff |= videoBuiltin->getProperty(RequestedExternalSwitchOffName) != nullptr;
			} else if (code == WIOKit::ClassCode::HDADevice || code == WIOKit::ClassCode::HDAMmDevice) {
				if (vendor == WIOKit::VendorID::Intel && name && (!strcmp(name, "HDAU") || !strcmp(name, "B0D3"))) {
					DBGLOG("dev", "found HDAU device %s", safeString(name));
					audioBuiltinDigital = obj;
				} else {
					DBGLOG("dev", "found HDEF device %s", safeString(name));
					audioBuiltinAnalog = obj;
				}
			} else if (vendor == WIOKit::VendorID::Intel &&
				name && (!strcmp(name, "IMEI") || !strcmp(name, "HECI") || !strcmp(name, "MEI"))) {
				// Fortunately IMEI is always made by Intel
				DBGLOG("dev", "found IMEI device %s", safeString(name));
				managementEngine = obj;
			} else if (vendor == WIOKit::VendorID::Intel && managementEngine == nullptr && code == WIOKit::ClassCode::IMEI) {
				// There can be many devices with IMEI class code.
				// REF: https://github.com/acidanthera/bugtracker/issues/716
				DBGLOG("dev", "found IMEI device candidate %s", safeString(name));
				managementEngine = obj;
			} else if (code == WIOKit::ClassCode::PCIBridge) {
				DBGLOG("dev", "found pci bridge %s", safeString(name));
				awaitPublishing(obj);
				auto pciiterator = IORegistryIterator::iterateOver(obj, gIODTPlane, kIORegistryIterateRecursively);
				if (pciiterator) {
					ExternalVideo v {};
					IORegistryEntry *pciobj = nullptr;
					while ((pciobj = OSDynamicCast(IORegistryEntry, pciiterator->getNextObject())) != nullptr) {
						uint32_t pcivendor = 0, pcicode = 0;
						DBGLOG("dev", "found %s on pci bridge", safeString(pciobj->getName()));
						if (WIOKit::getOSDataValue(pciobj, "vendor-id", pcivendor) &&
							WIOKit::getOSDataValue(pciobj, "class-code", pcicode)) {
							pcicode &= WIOKit::ClassCode::PCISubclassMask;

							if (pcicode == WIOKit::ClassCode::DisplayController ||
								pcicode == WIOKit::ClassCode::VGAController ||
								pcicode == WIOKit::ClassCode::Ex3DController ||
								pcicode == WIOKit::ClassCode::XGAController) {
								DBGLOG("dev", "found GFX0 device %s at %s by %04X",
									   safeString(pciobj->getName()), safeString(name),  pcivendor);
								v.video = pciobj;
								v.vendor = pcivendor;
							} else if (pcicode == WIOKit::ClassCode::HDADevice || pcicode == WIOKit::ClassCode::HDAMmDevice) {
								DBGLOG("dev", "found audio device %s at %s by %04X",
									   safeString(pciobj->getName()), safeString(name), pcivendor);
								v.audio = pciobj;
							}
						}
					}

					pciiterator->release();

					// AZAL audio devices cannot be descrete GPU devices.
					// On several AMD platforms there is an IGPU, which makes AZAL be recognised as a descrete GPU/HDA pair.
					// REF: https://github.com/acidanthera/Lilu/pull/65
					if (((v.audio && strcmp(v.audio->getName(), "AZAL") != 0) || !v.audio) && v.video) {
						DBGLOG_COND(v.audio, "dev", "marking audio device as HDAU at %s", safeString(v.audio->getName()));
						if (!videoExternal.push_back(v))
							SYSLOG("dev", "failed to push video gpu");
					} else if (v.audio && !audioBuiltinAnalog && v.audio->getProperty("external-audio") == nullptr) {
						// On modern AMD platforms or VMware built-in audio devices sits on a PCI bridge just any other device.
						// On AMD it has a distinct Ryzen device-id for the time being, yet on VMware it is just Intel.
						// To distinguish the devices we use audio card presence as a marker.
						DBGLOG("dev", "marking audio device as HDEF at %s", safeString(v.audio->getName()));
						audioBuiltinAnalog = v.audio;
					}
				}
			}
		}

		iterator->release();
	} else {
		SYSLOG("dev", "failed to obtain PCI devices iterator from %s", safeString(pciRoot->getName()));
	}
}

DeviceInfo *DeviceInfo::createCached() {
	if (globalDeviceInfo != nullptr)
		PANIC("dev", "called static globalDeviceInfo building twice");

	globalDeviceInfo = DeviceInfo::create();
	if (globalDeviceInfo == nullptr)
		PANIC("dev", "failed to build globalDeviceInfo");

	return globalDeviceInfo;
}

DeviceInfo *DeviceInfo::create() {
	if (globalDeviceInfo != nullptr)
		return globalDeviceInfo;

	auto list = new DeviceInfo;
	if (!list) {
		SYSLOG("dev", "failed to allocate device list");
		return nullptr;
	}

	DBGLOG("dev", "creating device info");

	list->requestedExternalSwitchOff = checkKernelArgument(RequestedExternalSwitchOffArg);

	auto rootSect = IORegistryEntry::fromPath("/", gIODTPlane);
	if (rootSect) {
		// Find every PCI root, X299 may have many
		auto lookupIterator = rootSect->getChildIterator(gIODTPlane);
		if (lookupIterator) {
			IORegistryEntry *pciRootObj = nullptr;
			while ((pciRootObj = OSDynamicCast(IORegistryEntry, lookupIterator->getNextObject())) != nullptr) {
				auto name = pciRootObj->getName();
				DBGLOG("dev", "found root device %s", safeString(name));

				bool isPciRoot = name && (!strncmp(name, "PCI", 3) || !strncmp(name, "PC0", 3));
				if (!isPciRoot) {
					// Some PCI devices do not have compatible (e.g. H67MA-UD2H-B3), but we should not
					// blindly trust names being the same, as there are no guarantees.
					auto compat = OSDynamicCast(OSData, pciRootObj->getProperty("compatible"));
					isPciRoot = compat && compat->getLength() == sizeof("PNP0A03") &&
						!strcmp(static_cast<const char *>(compat->getBytesNoCopy()), "PNP0A03");
				}

				if (isPciRoot) {
					DBGLOG("dev", "found PCI root %s", safeString(pciRootObj->getName()));

					// PCI is strictly not guaranteed to be ready at this time, check it just in case.
					while (OSDynamicCast(OSBoolean, pciRootObj->getProperty("IOPCIConfigured")) != kOSBooleanTrue) {
						DBGLOG("dev", "waiting on PCI root %s configuration", safeString(pciRootObj->getName()));
						IOSleep(1);
					}

					list->grabDevicesFromPciRoot(pciRootObj);
				}
			}

			lookupIterator->release();

			list->updateLayoutId();
			list->updateFramebufferId();

			list->firmwareVendor = BaseDeviceInfo::get().firmwareVendor;
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
	if (d != globalDeviceInfo) {
		d->videoExternal.deinit();
		delete d;
	}
}

void DeviceInfo::processSwitchOff() {
	DBGLOG("dev", "processing %lu external GPUs to disable - %s", videoExternal.size(), requestedExternalSwitchOff ? "all" : "selective");

	size_t i = 0;
	while (i < videoExternal.size()) {
		auto &v = videoExternal[i];

		// Check whether we want to explicitly disable this GPU.
		if (!requestedExternalSwitchOff) {
			// If there is no requesto to disable, skip.
			if (!v.video->getProperty(RequestedGpuSwitchOffName)) {
				i++;
				continue;
			}
			uint32_t minKernel = 0;
			WIOKit::getOSDataValue(v.video, RequestedGpuSwitchOffMinKernelName, minKernel);
			uint32_t maxKernel = getKernelVersion();
			WIOKit::getOSDataValue(v.video, RequestedGpuSwitchOffMaxKernelName, maxKernel);
			DBGLOG("dev", "disable %s GPU request from %u to %u on %u kernel", safeString(v.video->getName()), minKernel, maxKernel, getKernelVersion());
			if (minKernel > getKernelVersion() || maxKernel < getKernelVersion()) {
				i++;
				continue;
			}
		}

		WIOKit::awaitPublishing(v.video);

		auto gpu = OSDynamicCast(IOService, v.video);
		auto hda = OSDynamicCast(IOService, v.audio);
		auto pci = OSDynamicCast(IOService, v.video->getParentEntry(gIOServicePlane));
		if (gpu && pci) {
			if (gpu->requestTerminate(pci, 0) && gpu->terminate())
				gpu->stop(pci);
			else
				SYSLOG("dev", "failed to terminate external gpu %ld", i);
			if (hda && hda->requestTerminate(pci, 0) && hda->terminate())
				hda->stop(pci);
			else if (hda)
				SYSLOG("dev", "failed to terminate external hdau %ld", i);

			videoExternal.erase(i);
		} else {
			SYSLOG("dev", "incompatible external gpu %ld discovered", i);
			i++;
		}
	}

	if (videoExternal.size() == 0)
		videoExternal.deinit();
}

void BaseDeviceInfo::updateFirmwareVendor() {
	auto entry = IORegistryEntry::fromPath("/efi", gIODTPlane);
	if (entry) {
		auto ven = OSDynamicCast(OSData, entry->getProperty("firmware-vendor"));
		if (ven) {
			auto bytes = ven->getBytesNoCopy();
			size_t len = ven->getLength();
			if (bytes && len > 0) {
				struct Matching {
					DeviceInfo::FirmwareVendor ven;
					const char16_t *str;
					size_t len;
				};

				//TODO: Add more vendors here (like branded Phoenix or Intel).
				Matching matching[] {
					// All Apple-made hardware.
					{DeviceInfo::FirmwareVendor::Apple, u"Apple", sizeof(u"Apple")},
					// Parallels Desktop virtual machines.
					{DeviceInfo::FirmwareVendor::Parallels, u"Parallels Software International Inc.", sizeof(u"Parallels Software International Inc.")},
					// VMware Fusion, Workstation, Player, ESXi virtual machines.
					{DeviceInfo::FirmwareVendor::VMware, u"VMware, Inc.", sizeof(u"VMware, Inc.")},
					// OVMF firmware for QEMU or XEN.
					{DeviceInfo::FirmwareVendor::EDKII, u"EDK II", sizeof(u"EDK II")},
					// Two variants of AMI APTIO firmware names. Shorter one is older.
					{DeviceInfo::FirmwareVendor::AMI, u"American Megatrends", sizeof(u"American Megatrends")},
					{DeviceInfo::FirmwareVendor::AMI, u"American Megatrends Inc.", sizeof(u"American Megatrends Inc.")},
					// Branded Insyde H2O firmwares.
					{DeviceInfo::FirmwareVendor::Insyde, u"INSYDE Corp.", sizeof(u"INSYDE Corp.")},
					// Branded Phoenix firmwares.
					{DeviceInfo::FirmwareVendor::Phoenix, u"Phoenix Technologies Ltd.", sizeof(u"Phoenix Technologies Ltd.")},
					// Legacy HP firmwares found in old HP ProBooks, which were based on Insyde or Phoenix.
					{DeviceInfo::FirmwareVendor::HP, u"HPQ", sizeof(u"HPQ")},
					// New FSP-based HP firmwares found in hardware like HP ProDesk 400 G4 Mini
					{DeviceInfo::FirmwareVendor::HP, u"HP", sizeof(u"HP")}
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

void BaseDeviceInfo::updateModelInfo() {
	auto entry = IORegistryEntry::fromPath("/efi/platform", gIODTPlane);
	if (entry) {
		if (entry->getProperty("BEMB")) {
			bootloaderVendor = BootloaderVendor::Clover;
			SYSLOG("dev", "WARN: found Clover bootloader");
		} else if (entry->getProperty("REV")) {
			bootloaderVendor = BootloaderVendor::Acidanthera;
			DBGLOG("dev", "assuming Acidanthera bootloader");
			// Note, OpenCore is mostly stealth. One can detect it via:
			// - Acidanthera manufacturer (only if we decided to update SMBIOS, available much later).
			// - opencore-version variable (only if we decided to expose it NVRAM)
			// - "REV" key before it is deleted by VirtualSMC (only if we decided to update DataHub)
		}

		auto data = OSDynamicCast(OSData, entry->getProperty("Model"));
		size_t dataSize = data ? data->getLength() : 0;
		if (dataSize > 0) {
			auto bytes = static_cast<const char16_t *>(data->getBytesNoCopy());
			size_t i = 0;
			while (bytes[i] != '\0' && i < sizeof(modelIdentifier) - 1 && i < dataSize) {
				modelIdentifier[i] = static_cast<char>(bytes[i]);
				i++;
			}
		}

		if (modelIdentifier[0] != '\0')
			DBGLOG("dev", "got %s model from /efi/platform", modelIdentifier);
		else
			DBGLOG("dev", "failed to get valid model from /efi/platform");

		data = OSDynamicCast(OSData, entry->getProperty("board-id"));
		if (data && data->getLength() > 0)
			lilu_os_strlcpy(boardIdentifier, static_cast<const char *>(data->getBytesNoCopy()), sizeof(boardIdentifier));

		if (boardIdentifier[0] != '\0')
			DBGLOG("dev", "got %s board-id from /efi/platform", boardIdentifier);
		else
			DBGLOG("dev", "failed to get valid board-id from /efi/platform");

		entry->release();
	} else {
		SYSLOG("dev", "failed to get DT /efi/platform");
	}

	// Try the legacy approach for old MacEFI and VMware.
	while (modelIdentifier[0] == '\0' || boardIdentifier[0] == '\0') {
		auto entry = IORegistryEntry::fromPath("/", gIODTPlane);
		if (entry) {
			bool modelReady = false;

			if (boardIdentifier[0] == '\0') {
				auto data = OSDynamicCast(OSData, entry->getProperty("board-id"));
				if (data && data->getLength() > 0)
					lilu_os_strlcpy(boardIdentifier, static_cast<const char *>(data->getBytesNoCopy()), sizeof(boardIdentifier));

				if (boardIdentifier[0] != '\0') {
					DBGLOG("dev", "got %s board-id from /", boardIdentifier);
					// Otherwise we will get ACPI model.
					modelReady = true;
				} else {
					DBGLOG("dev", "failed to get valid board-id from /");
				}
			} else {
				modelReady = true;
			}

			if (modelReady && modelIdentifier[0] == '\0') {
				auto data = OSDynamicCast(OSData, entry->getProperty("model"));
				if (data && data->getLength() > 0)
					lilu_os_strlcpy(modelIdentifier, static_cast<const char *>(data->getBytesNoCopy()), sizeof(modelIdentifier));

				if (modelIdentifier[0] != '\0')
					DBGLOG("dev", "got %s model from /", modelIdentifier);
				else
					DBGLOG("dev", "failed to get valid model from /");
			}



			entry->release();
		} else {
			SYSLOG("dev", "failed to get DT root");
		}

		if (modelIdentifier[0] == '\0' || boardIdentifier[0] == '\0') {
			SYSLOG("dev", "failed to obtain model information, retrying...");
			IOSleep(1);
		}
	}

	if (strstr(modelIdentifier, "Book", strlen("Book")))
		modelType = WIOKit::ComputerModel::ComputerLaptop;
	else
		modelType = WIOKit::ComputerModel::ComputerDesktop;
}

const BaseDeviceInfo &BaseDeviceInfo::get() {
	return globalBaseDeviceInfo;
}

void BaseDeviceInfo::init() {
	// Initialize the CPU part.
	CPUInfo::init();

	globalBaseDeviceInfo.updateFirmwareVendor();
	globalBaseDeviceInfo.updateModelInfo();
}

