//
//  kern_nvram.cpp
//  Lilu
//
//  Copyright © 2017 vit9696. All rights reserved.
//

#include <IOKit/IOService.h>
#include <IOKit/IONVRAM.h>

#include <Headers/kern_config.hpp>
#include <Headers/kern_compat.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/kern_file.hpp>
#include <Headers/kern_compression.hpp>
#include <Headers/kern_crypto.hpp>
#include <Headers/kern_nvram.hpp>

bool NVStorage::init() {
	dtEntry = IORegistryEntry::fromPath("/options", gIODTPlane);
	if (!dtEntry) {
		SYSLOG_COND(ADDPR(debugEnabled), "nvram", "failed to get IODeviceTree:/options");
		return false;
	}

	if (!OSDynamicCast(IODTNVRAM, dtEntry)) {
		SYSLOG_COND(ADDPR(debugEnabled), "nvram", "failed to get IODTNVRAM from IODeviceTree:/options");
		dtEntry->release();
		dtEntry = nullptr;
		return false;
	}

	return true;
}

void NVStorage::deinit() {
	if (dtEntry) {
		dtEntry->release();
		dtEntry = nullptr;
	}
}

uint8_t *NVStorage::read(const char *key, uint32_t &size, uint8_t opts, const uint8_t *enckey) {
	auto data = OSDynamicCast(OSData, dtEntry->getProperty(key));
	if (!data) {
		DBGLOG("nvram", "read %s is missing", key);
		return nullptr;
	}

	auto payloadSize = data->getLength();
	if (payloadSize == 0) {
		DBGLOG("nvram", "read %s has 0 length", key);
		return nullptr;
	}

	size = payloadSize;
	bool payloadAlloc = false;
	auto payloadBuf = static_cast<const uint8_t *>(data->getBytesNoCopy());

	if (!(opts & OptRaw)) {
		if (payloadSize < sizeof(Header)) {
			SYSLOG("nvram", "read %s contains not enough header bytes (%u)", key, size);
			return nullptr;
		}

		payloadBuf  += sizeof(Header);
		payloadSize -= sizeof(Header);

		auto replacePayload = [&payloadBuf, &payloadAlloc, opts](const uint8_t *newBuf, uint32_t orgSize) {
			if (payloadAlloc) {
				auto buf = const_cast<uint8_t *>(payloadBuf);
				if (opts & OptSensitive) Crypto::zeroMemory(orgSize, buf);
				Buffer::deleter(buf);
			}
			payloadBuf   = newBuf;
			payloadAlloc = true;
		};

		auto hdr = static_cast<const Header *>(data->getBytesNoCopy());
		if (hdr->magic != Header::Magic || hdr->version > Header::MaxVer || (hdr->opts & opts) != opts) {
			SYSLOG("nvram", "read %s contains invalid header (%X, %u, %X vs %X, %u, %X)",
						 key, hdr->magic, hdr->version, hdr->opts, Header::Magic, Header::MaxVer, opts);
			return nullptr;
		}

		if (hdr->opts & OptChecksum) {
			if (payloadSize < sizeof(Header::Checksum)) {
				SYSLOG("nvram", "read %s contains not enough checksum bytes (%u)", key, payloadSize);
				return nullptr;
			}

			payloadSize -= sizeof(Header::Checksum);

			auto ncrc = crc32(0, hdr, size - sizeof(Header::Checksum));
			auto ocrc = *reinterpret_cast<const uint32_t *>(payloadBuf + payloadSize);
			if (ocrc != ncrc) {
				SYSLOG("nvram", "read %s contains invalid checksum bytes (%08X instead of %08X)", key, ncrc, ocrc);
				return nullptr;
			}
		}

		if (hdr->opts & OptEncrypted) {
			auto orgSize = payloadSize;
			replacePayload(Crypto::decrypt(enckey, payloadBuf, payloadSize), orgSize);

			if (!payloadBuf) {
				SYSLOG("nvram", "read %s contains invalid encrypted data", key);
				return nullptr;
			}
		}

		if (hdr->opts & OptCompressed) {
			auto orgSize = payloadSize;
			replacePayload(decompress(payloadBuf, payloadSize, opts & OptSensitive), orgSize);

			if (!payloadBuf) {
				SYSLOG("nvram", "read %s contains invalid compressed data", key);
				return nullptr;
			}
		}
	}

	size = payloadSize;
	if (payloadAlloc) {
		return const_cast<uint8_t *>(payloadBuf);
	}

	auto buf = Buffer::create<uint8_t>(payloadSize);
	if (!buf) {
		SYSLOG("nvram", "read %s failed to allocate %u bytes", key, size);
		return nullptr;
	}

	lilu_os_memcpy(buf, payloadBuf, payloadSize);
	return buf;
}

OSData *NVStorage::read(const char *key, uint8_t opts, const uint8_t *enckey) {
	uint32_t size = 0;
	uint8_t *buf = read(key, size, opts, enckey);
	if (!buf)
		return nullptr;
	auto data = OSData::withBytes(buf, size);
	if (opts & OptSensitive) Crypto::zeroMemory(size, buf);
	Buffer::deleter(buf);
	return data;
}

bool NVStorage::write(const char *key, const uint8_t *src, uint32_t size, uint8_t opts, const uint8_t *enckey) {
	if (!src || size == 0) {
		SYSLOG("nvram", "write invalid size %u", size);
		return false;
	}

	bool payloadAlloc = false;
	auto payloadBuf   = src;
	auto payloadSize  = size;

	auto replacePayload = [&payloadBuf, &payloadAlloc, opts](const uint8_t *newBuf, uint32_t orgSize) {
		if (payloadAlloc) {
			auto buf = const_cast<uint8_t *>(payloadBuf);
			if (opts & OptSensitive) Crypto::zeroMemory(orgSize, buf);
			Buffer::deleter(buf);
		}
		payloadBuf   = newBuf;
		payloadAlloc = true;
	};

	if (!(opts & OptRaw)) {
		Header hdr {};
		hdr.opts = opts & ~OptSensitive;

		if (opts & OptCompressed) {
			auto orgSize = payloadSize;
			replacePayload(compress(payloadBuf, payloadSize, opts & OptSensitive), orgSize);

			if (!payloadBuf) {
				SYSLOG("nvram", "write %s can't compressed data", key);
				return false;
			}
		}

		if (opts & OptEncrypted) {
			auto orgSize = payloadSize;
			replacePayload(Crypto::encrypt(enckey, payloadBuf, payloadSize), orgSize);

			if (!payloadBuf) {
				SYSLOG("nvram", "write %s can't encrypt data", key);
				return false;
			}
		}

		size = sizeof(Header) + payloadSize + ((opts & OptChecksum) ? sizeof(Header::Checksum) : 0);
		auto buf = Buffer::create<uint8_t>(size);
		if (!buf) {
			SYSLOG("nvram", "write %s can't alloc %u bytes", key, size);
			replacePayload(buf, payloadSize);
			return false;
		}
		lilu_os_memcpy(buf, &hdr, sizeof(Header));
		lilu_os_memcpy(buf + sizeof(Header), payloadBuf, payloadSize);
		replacePayload(buf, payloadSize);
		payloadSize += sizeof(Header);

		if (opts & OptChecksum) {
			auto ncrc = crc32(0, buf, payloadSize);
			*reinterpret_cast<uint32_t *>(buf + payloadSize) = ncrc;
			payloadSize += sizeof(Header::Checksum);
		}
	}

	auto data = OSData::withBytes(payloadBuf, payloadSize);
	replacePayload(nullptr, payloadSize);
	if (data) {
		if (dtEntry->setProperty(key, data)) {
			data->release();
			return true;
		}

		if (opts & OptSensitive)
			Crypto::zeroMemory(data->getLength(), const_cast<void *>(data->getBytesNoCopy()));
		data->release();
	}

	return false;
}

bool NVStorage::write(const char *key, const OSData *data, uint8_t opts, const uint8_t *enckey) {
	if (!data) {
		SYSLOG("nvram", "write invalid data object");
		return false;
	}

	return write(key, static_cast<const uint8_t *>(data->getBytesNoCopy()), data->getLength(), opts, enckey);
}

bool NVStorage::remove(const char *key, bool sensitive) {
	if (sensitive) {
		auto data = OSDynamicCast(OSData, dtEntry->getProperty(key));
		if (data && data->getLength() > 0) {
			Crypto::zeroMemory(data->getLength(), const_cast<void *>(data->getBytesNoCopy()));
			dtEntry->setProperty(key, data);
			sync();
		}
	}

	dtEntry->removeProperty(key);
	return true;
}

bool NVStorage::sync() {
	auto entry = OSDynamicCast(IODTNVRAM, dtEntry);
	if (entry && entry->safeToSync()) {
		entry->sync();
		return true;
	}

	return false;
}

bool NVStorage::save(const char *filename, uint32_t max, bool sensitive) {
	static const char *PlistHeader {
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\">\n"
	};

	static const char *PlistFooter {
		"\n</plist>\n"
	};

	auto s = OSSerialize::withCapacity(max);
	if (s) {
		s->addString(PlistHeader);
		dtEntry->serializeProperties(s);
		s->addString(PlistFooter);
		int error = FileIO::writeBufferToFile(filename, s->text(), s->getLength());
		if (sensitive)
			Crypto::zeroMemory(s->getLength(), s->text());
		s->release();

		return error == 0;
	} else {
		SYSLOG("nvram", "failed to allocate serialization buffer of %u bytes", max);
	}

	return false;
}

uint8_t *NVStorage::compress(const uint8_t *src, uint32_t &size, bool sensitive) {
#ifdef LILU_COMPRESSION_SUPPORT
	uint32_t dstSize = size + 1024;
	auto buf = Buffer::create<uint8_t>(dstSize);
	if (buf) {
		*reinterpret_cast<uint32_t *>(buf) = size;
		DBGLOG("nvram", "compress saves dstSize = %u, srcSize = %u", dstSize, size);
		if (Compression::compress(Compression::ModeLZSS, dstSize, src, size, buf + sizeof(uint32_t))) {
			// Buffer was already resized by compress
			size = dstSize + sizeof(uint32_t);
			DBGLOG("nvram", "compress result size = %u", size);
			Buffer::resize(buf, size);
			return buf;
		}

		if (sensitive)
			Crypto::zeroMemory(dstSize, buf);
		Buffer::deleter(buf);
	} else {
		SYSLOG("nvram", "failed to allocate %u bytes", dstSize);
	}
#endif
	return nullptr;
}

uint8_t *NVStorage::decompress(const uint8_t *src, uint32_t &size, bool sensitive) {
#ifdef LILU_COMPRESSION_SUPPORT
	if (size <= sizeof(uint32_t)) {
		SYSLOG("nvram", "decompress too few bytes %u", size);
		return nullptr;
	}

	auto dstSize = *reinterpret_cast<const uint32_t *>(src);
	auto buf = Buffer::create<uint8_t>(dstSize);
	if (buf) {
		size -= sizeof(uint32_t);
		DBGLOG("nvram", "decompress restores dstSize = %u, srcSize = %u", dstSize, size);
		if (Compression::decompress(Compression::ModeLZSS, dstSize, src + sizeof(uint32_t), size, buf)) {
			size = dstSize;
			DBGLOG("nvram", "decompress result size = %u", size);
			return buf;
		}

		if (sensitive)
			Crypto::zeroMemory(dstSize, buf);
		Buffer::deleter(buf);
	} else {
		SYSLOG("nvram", "decompress failed to allocate %u bytes", dstSize);
	}
#endif
	return nullptr;
}

bool NVStorage::exists(const char *key)
{
	return OSDynamicCast(OSData, dtEntry->getProperty(key)) != nullptr;
}
