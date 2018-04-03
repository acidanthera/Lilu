//
//  kern_crypto.cpp
//  Lilu
//
//  Copyright © 2017 vit9696. All rights reserved.
//

#include "sha256.h"

#include <Headers/kern_crypto.hpp>
#include <Library/libkern/crypto/aes.h>
#include <sys/random.h>

static_assert(Crypto::BlockSize == AES_BLOCK_SIZE, "Invalid block size!");
static_assert(Crypto::BlockSize <= SHA256_BLOCK_SIZE ||
			  Crypto::MinDigestSize > SHA256_BLOCK_SIZE, "Hash function does not provide enough data");

uint8_t *Crypto::genPlatformKey(const uint8_t *seed, uint32_t size) {
	SYSLOG("crypto", "genPlatformKey is currently unimplemented");

	return nullptr;
}

uint8_t *Crypto::genUniqueKey(uint32_t size) {
	if (size < BlockSize) {
		SYSLOG("crypto", "invalid key size %u", size);
		return nullptr;
	}
	
	auto buf = Buffer::create<uint8_t>(size);
	if (buf) {
		read_random(buf, size);
		return buf;
	} else {
		SYSLOG("crypto", "unique failed to allocate key buffer");
	}
	
	return nullptr;
}

uint8_t *Crypto::encrypt(const uint8_t *key, const uint8_t *src, uint32_t &size) {
	if (!src || size == 0) {
		SYSLOG("crypto", "encrypt contains invalid size (%u) or null args", size);
		return nullptr;
	}
	
	uint8_t *pkey = nullptr;
	if (!key && !(pkey = genPlatformKey())) {
		SYSLOG("crypto", "encrypt unable to obtain platform key");
		return nullptr;
	}
	
	uint32_t realSize = sizeof(Encrypted::Data::size) + size;
	uint32_t padSize  = realSize % BlockSize;
	if (padSize > 0)
		padSize = BlockSize - padSize;
	uint32_t encSize  = realSize + padSize;
	uint32_t fullSize = encSize + BlockSize;
	
	uint8_t *encBuf = nullptr;
	
	auto dataBuf = Buffer::create<uint8_t>(encSize);
	if (dataBuf) {
		auto data = reinterpret_cast<Encrypted::Data *>(dataBuf);
		data->size = size;
		lilu_os_memcpy(data->buf, src, size);
		memset(data->buf + size, 0, padSize);
		
		encBuf = Buffer::create<uint8_t>(fullSize);
		if (encBuf) {
			auto enc = reinterpret_cast<Encrypted *>(encBuf);
			read_random(enc->iv, BlockSize);
			
			aes_encrypt_ctx ctx;
			auto ret = aes_encrypt_key(key ? key : pkey, BlockSize, &ctx);
			if (ret == aes_good) {
				ret = aes_encrypt_cbc(dataBuf, enc->iv, encSize / BlockSize, enc->buf, &ctx);
				if (ret == aes_good)
					size = fullSize;
				else
					SYSLOG("crypto", "encrypt failed to aes_encrypt (%d)", ret);
			} else {
				SYSLOG("crypto", "encrypt failed to init aes ctx (%d)", ret);
			}
			
			zeroMemory(sizeof(ctx), &ctx);
			if (ret != aes_good) {
				zeroMemory(fullSize, encBuf);
				Buffer::deleter(encBuf);
				encBuf = nullptr;
			}
		} else {
			SYSLOG("crypto", "encrypt failed to allocate dst buffer of %u bytes", fullSize);
		}
		
		zeroMemory(encSize, dataBuf);
		Buffer::deleter(dataBuf);
	} else {
		SYSLOG("crypto", "encrypt failed to allocate src buffer of %u bytes", encSize);
	}
	
	if (pkey) {
		zeroMemory(BlockSize, pkey);
		Buffer::deleter(pkey);
	}
	
	return encBuf;
}

uint8_t *Crypto::decrypt(const uint8_t *key, const uint8_t *src, uint32_t &size) {
	if (!src || size < sizeof(Encrypted) || size % BlockSize != 0) {
		SYSLOG("crypto", "decrypt contains invalid size (%u) or null args", size);
		return nullptr;
	}
	
	uint8_t *pkey = nullptr;
	if (!key && !(pkey = genPlatformKey())) {
		SYSLOG("crypto", "decrypt unable to obtain platform key");
		return nullptr;
	}
	
	size -= offsetof(Encrypted, enc);
	
	auto decBuf = Buffer::create<uint8_t>(size);
	if (decBuf) {
		aes_decrypt_ctx ctx;
		auto ret = aes_decrypt_key(key ? key : pkey, BlockSize, &ctx);
		if (ret == aes_good) {
			auto enc = reinterpret_cast<const Encrypted *>(src);
			ret = aes_decrypt_cbc(enc->buf, enc->iv, size / BlockSize, decBuf, &ctx);
			if (ret == aes_good) {
				auto data = reinterpret_cast<Encrypted::Data *>(decBuf);
				if (size - sizeof(Encrypted::Data::size) >= data->size) {
					size = data->size;
					lilu_os_memmove(decBuf, data->buf, size);
					zeroMemory(sizeof(Encrypted::Data::size), decBuf + size);
					Buffer::resize(decBuf, size);
				} else {
					SYSLOG("crypto", "decrypt found invalid size (%u with %u source)", data->size, size);
					ret = aes_error;
				}
			} else {
				SYSLOG("crypto", "decrypt failed to decrypt (%d)", ret);
			}
		} else {
			SYSLOG("crypto", "decrypt failed to init aes ctx (%d)", ret);
		}
		
		zeroMemory(sizeof(ctx), &ctx);
		if (ret != aes_good) {
			zeroMemory(size, decBuf);
			Buffer::deleter(decBuf);
			decBuf = nullptr;
		}
	}
	
	if (pkey) {
		zeroMemory(BlockSize, pkey);
		Buffer::deleter(pkey);
	}
	
	return decBuf;
}

uint8_t *Crypto::hash(const uint8_t *src, uint32_t size) {
	if (!src || size == 0) {
		SYSLOG("crypto", "hash invalid hash data (%u bytes)", size);
		return nullptr;
	}
	
	auto buf = Buffer::create<uint8_t>(SHA256_BLOCK_SIZE);
	if (buf) {
		SHA256_CTX ctx;
		sha256_init(&ctx);
		sha256_update(&ctx, src, size);
		sha256_final(&ctx, buf);
		zeroMemory(sizeof(ctx), &ctx);
		
		return buf;
	} else {
		SYSLOG("crypto", "hash failed to allocate hash buffer");
	}
	
	return nullptr;
}
