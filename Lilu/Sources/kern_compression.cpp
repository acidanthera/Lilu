//
//  kern_compression.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>

#ifdef COMPRESSION_SUPPORT

#include <Headers/kern_compression.hpp>
#include <Headers/kern_util.hpp>

#include <stdint.h>
#include <sys/types.h>

#include <FastCompression.hpp>

const size_t N = 4096;      /* size of ring buffer - must be power of 2 */
const size_t F = 18;        /* upper limit for match_length */
const size_t THRESHOLD = 2; /* encode string into position and length if match_length is greater than this */

// Taken from kext_tools/compression.c
static size_t decompress_lzss(uint8_t *dst, uint32_t dstlen, uint8_t *src, uint32_t srclen) {
	/* ring buffer of size N, with extra F-1 bytes to aid string comparison */
	uint8_t text_buf[N + F - 1];
	uint8_t *dststart = dst;
	const uint8_t *dstend = dst + dstlen;
	const uint8_t *srcend = src + srclen;
	int  i, j, k, r, c;
	unsigned int flags;
	
	dst = dststart;
	for (i = 0; i < N - F; i++)
		text_buf[i] = ' ';
	r = N - F;
	flags = 0;
	for ( ; ; ) {
		if (((flags >>= 1) & 0x100) == 0) {
			if (src < srcend) c = *src++; else break;
			flags = c | 0xFF00;  /* uses higher byte cleverly */
		}   /* to count eight */
		if (flags & 1) {
			if (src < srcend) c = *src++; else break;
			if (dst < dstend) *dst++ = c; else break;
			text_buf[r++] = c;
			r &= (N - 1);
		} else {
			if (src < srcend) i = *src++; else break;
			if (src < srcend) j = *src++; else break;
			i |= ((j & 0xF0) << 4);
			j  =  (j & 0x0F) + THRESHOLD;
			for (k = 0; k <= j; k++) {
				c = text_buf[(i + k) & (N - 1)];
				if (dst < dstend) *dst++ = c; else break;
				text_buf[r++] = c;
				r &= (N - 1);
			}
		}
	}
	
	return dst - dststart;
}

uint8_t *decompressData(uint32_t compression, uint32_t dstlen, uint8_t *src, uint32_t srclen) {
	auto decompressedBuf = Buffer::create<uint8_t>(dstlen);
	if (decompressedBuf) {
		size_t size {0};
		switch (compression) {
			case CompressionLZSS:
				size = decompress_lzss(decompressedBuf, dstlen, src, srclen);
				break;
			case CompressionLZVN:
				size = lzvn_decode(decompressedBuf, dstlen, src, srclen);
				break;
			default:
				SYSLOG("compression @ unsupported compression %X", compression);
		}
		
		if (size == dstlen) {
			return decompressedBuf;
		} else {
			SYSLOG("compression @ failed to correctly decompress the data");
		}
	} else {
		SYSLOG("compression @ failed to allocate memory for decompression buffer");
	}
	
	Buffer::deleter(decompressedBuf);
	return 0;
}

#endif /* COMPRESSION_SUPPORT */
