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
	if (dtEntry)
		return true;
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
		auto requiredOpts = opts & ~OptSensitive;
		if (hdr->magic != Header::Magic || hdr->version > Header::MaxVer || (hdr->opts & requiredOpts) != requiredOpts) {
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
			Header::Checksum ocrc;
			lilu_os_memcpy(&ocrc, payloadBuf + payloadSize, sizeof(ocrc));
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

		auto overhead = sizeof(Header) + ((opts & OptChecksum) ? sizeof(Header::Checksum) : 0);
		if (payloadSize > Buffer::BufferMax - overhead) {
			replacePayload(nullptr, payloadSize);
			return false;
		}
		size = static_cast<uint32_t>(overhead + payloadSize);
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
			lilu_os_memcpy(buf + payloadSize, &ncrc, sizeof(Header::Checksum));
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
		bool saved = s->addString(PlistHeader) && dtEntry->serializeProperties(s) && s->addString(PlistFooter);
		if (saved) {
			// OSSerialize includes the trailing NUL in its length.
			auto length = s->getLength() - 1;
			saved = length <= max && FileIO::writeBufferToFile(filename, s->text(), length) == 0;
		}
		if (sensitive)
			Crypto::zeroMemory(s->getLength(), s->text());
		s->release();

		return saved;
	} else {
		SYSLOG("nvram", "failed to allocate serialization buffer of %u bytes", max);
	}

	return false;
}

uint8_t *NVStorage::compress(const uint8_t *src, uint32_t &size, bool sensitive) {
#ifdef LILU_COMPRESSION_SUPPORT
	if (!src || !size || size > Buffer::BufferMax)
		return nullptr;
	// At most one flag byte per eight literal bytes, plus the stored input size.
	uint32_t dstSize = size + (size + 7) / 8;
	constexpr uint32_t maxPayload = Buffer::BufferMax - sizeof(uint32_t);
	if (dstSize > maxPayload)
		dstSize = maxPayload;
	auto capacity = sizeof(uint32_t) + dstSize;
	auto buf = Buffer::create<uint8_t>(capacity);
	if (buf) {
		*reinterpret_cast<uint32_t *>(buf) = size;
		DBGLOG("nvram", "compress saves dstSize = %u, srcSize = %u", dstSize, size);
		if (Compression::compress(Compression::ModeLZSS, dstSize, src, size, buf + sizeof(uint32_t))) {
			size = dstSize + sizeof(uint32_t);
			DBGLOG("nvram", "compress result size = %u", size);
			Buffer::resize(buf, size);
			return buf;
		}

		if (sensitive)
			Crypto::zeroMemory(capacity, buf);
		Buffer::deleter(buf);
	} else {
		SYSLOG("nvram", "failed to allocate %u bytes", dstSize);
	}
#endif
	return nullptr;
}

uint8_t *NVStorage::decompress(const uint8_t *src, uint32_t &size, bool sensitive) {
#ifdef LILU_COMPRESSION_SUPPORT
	if (!src || size <= sizeof(uint32_t)) {
		SYSLOG("nvram", "decompress too few bytes %u", size);
		return nullptr;
	}

	uint32_t dstSize;
	lilu_os_memcpy(&dstSize, src, sizeof(dstSize));
	auto buf = Buffer::create<uint8_t>(dstSize);
	if (buf) {
		DBGLOG("nvram", "decompress restores dstSize = %u, srcSize = %u", dstSize, size);
		if (Compression::decompress(Compression::ModeLZSS, dstSize, src + sizeof(uint32_t), size - sizeof(uint32_t), buf)) {
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
