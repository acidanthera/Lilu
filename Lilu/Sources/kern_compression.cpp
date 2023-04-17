//
//  kern_compression.cpp
//  Lilu
//
//  Copyright © 2016-2017 vit9696. All rights reserved.
//

#include <Headers/kern_config.hpp>

#ifdef LILU_COMPRESSION_SUPPORT

#include <Headers/kern_compression.hpp>
#include <Headers/kern_util.hpp>

#include <stdint.h>
#include <sys/types.h>

#include <lzvn.h>

//
// Zlib compression
// Taken from /apple/xnu/libkern/c++/OSKext.cpp
//
#include <libkern/zlib.h>
#include <libkern/crypto/sha1.h>

extern "C" {
	static void* z_alloc(void*, u_int items, u_int size);
	static void z_free(void*, void *ptr);
	
	typedef struct z_mem {
		UInt32 alloc_size;
		UInt8 data[0];
	} z_mem;

	/*
	 * Space allocation and freeing routines for use by zlib routines.
	 */
	void* z_alloc(void* notused __unused, u_int num_items, u_int size)
	{
		void* result = NULL;
		z_mem* zmem = NULL;
		UInt32 total = num_items * size;
		UInt32 allocSize =  total + sizeof(zmem);

		zmem = (z_mem*)IOMalloc(allocSize);

		if (zmem)
		{
			zmem->alloc_size = allocSize;
			result = (void*)&(zmem->data);
		}

		return result;
	}

	void z_free(void* notused __unused, void* ptr)
	{
		UInt32* skipper = (UInt32 *)ptr - 1;
		z_mem* zmem = (z_mem*)skipper;
		IOFree((void*)zmem, zmem->alloc_size);
	}
};

static size_t decompress_zlib(uint8_t *dst, uint32_t dstlen, const uint8_t *src, uint32_t srclen) {
	z_stream zstream;
	int zlib_result;
	size_t result = 0;

	bzero(&zstream, sizeof(zstream));

	zstream.next_in   = (unsigned char*)src;
	zstream.avail_in  = srclen;

	zstream.next_out  = (unsigned char*)dst;
	zstream.avail_out = dstlen;

	zstream.zalloc    = z_alloc;
	zstream.zfree     = z_free;

	zlib_result = inflateInit(&zstream);

	if (zlib_result != Z_OK)
	{
		return 0;
	}

	zlib_result = inflate(&zstream, Z_FINISH);

	if (zlib_result == Z_STREAM_END || zlib_result == Z_OK) {
		result = zstream.total_out;
	}

	inflateEnd(&zstream);
	return result;
}

// Taken from kext_tools/compression.c

const size_t RBSIZE = 4096;      /* size of ring buffer - must be power of 2 */
const size_t UPLIM = 18;         /* upper limit for match_length */
const size_t THRESHOLD = 2;      /* encode string into position and length if match_length is greater than this */
const size_t NIL = RBSIZE;       /* index for root of binary search trees */

struct encode_state {
	/*
	 * left & right children & parent. These constitute binary search trees.
	 */
	int lchild[RBSIZE + 1], rchild[RBSIZE + 257], parent[RBSIZE + 1];

	/* ring buffer of size N, with extra F-1 bytes to aid string comparison */
	uint8_t text_buf[RBSIZE + UPLIM - 1];

	/*
	 * match_length of longest match.
	 * These are set by the insert_node() procedure.
	 */
	int match_position, match_length;
};

static size_t decompress_lzss(uint8_t *dst, uint32_t dstlen, const uint8_t *src, uint32_t srclen) {
	/* ring buffer of size N, with extra F-1 bytes to aid string comparison */
	uint8_t text_buf[RBSIZE + UPLIM - 1];
	uint8_t *dststart = dst;
	const uint8_t *dstend = dst + dstlen;
	const uint8_t *srcend = src + srclen;
	int  i, j, k, r, c;
	unsigned int flags;

	dst = dststart;
	for (i = 0; i < static_cast<int>(RBSIZE - UPLIM); i++)
		text_buf[i] = ' ';
	r = RBSIZE - UPLIM;
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
			r &= (RBSIZE - 1);
		} else {
			if (src < srcend) i = *src++; else break;
			if (src < srcend) j = *src++; else break;
			i |= ((j & 0xF0) << 4);
			j  =  (j & 0x0F) + THRESHOLD;
			for (k = 0; k <= j; k++) {
				c = text_buf[(i + k) & (RBSIZE - 1)];
				if (dst < dstend) *dst++ = c; else break;
				text_buf[r++] = c;
				r &= (RBSIZE - 1);
			}
		}
	}

	return dst - dststart;
}

/*
 * initialize state, mostly the trees
 *
 * For i = 0 to N - 1, rchild[i] and lchild[i] will be the right and left
 * children of node i.  These nodes need not be initialized.  Also, parent[i]
 * is the parent of node i.  These are initialized to NIL (= N), which stands
 * for 'not used.'  For i = 0 to 255, rchild[N + i + 1] is the root of the
 * tree for strings that begin with character i.  These are initialized to NIL.
 * Note there are 256 trees. */
static void init_state(encode_state *sp) {
	size_t i;

	memset(sp, 0, sizeof(*sp));

	for (i = 0; i < RBSIZE - UPLIM; i++)
		sp->text_buf[i] = ' ';
	for (i = RBSIZE + 1; i <= RBSIZE + 256; i++)
		sp->rchild[i] = NIL;
	for (i = 0; i < RBSIZE; i++)
		sp->parent[i] = NIL;
}

/*
 * Inserts string of length F, text_buf[r..r+F-1], into one of the trees
 * (text_buf[r]'th tree) and returns the longest-match position and length
 * via the global variables match_position and match_length.
 * If match_length = F, then removes the old node in favor of the new one,
 * because the old one will be deleted sooner. Note r plays double role,
 * as tree node and position in buffer.
 */
static void insert_node(encode_state *sp, int r) {
	int  i, p, cmp;
	uint8_t  *key;

	cmp = 1;
	key = &sp->text_buf[r];
	p = RBSIZE + 1 + key[0];
	sp->rchild[r] = sp->lchild[r] = NIL;
	sp->match_length = 0;
	for ( ; ; ) {
		if (cmp >= 0) {
			if (sp->rchild[p] != NIL)
				p = sp->rchild[p];
			else {
				sp->rchild[p] = r;
				sp->parent[r] = p;
				return;
			}
		} else {
			if (sp->lchild[p] != NIL)
				p = sp->lchild[p];
			else {
				sp->lchild[p] = r;
				sp->parent[r] = p;
				return;
			}
		}
		for (i = 1; i < static_cast<int>(UPLIM); i++) {
			if ((cmp = key[i] - sp->text_buf[p + i]) != 0)
				break;
		}
		if (i > sp->match_length) {
			sp->match_position = p;
			if ((sp->match_length = i) >= static_cast<int>(UPLIM))
				break;
		}
	}
	sp->parent[r] = sp->parent[p];
	sp->lchild[r] = sp->lchild[p];
	sp->rchild[r] = sp->rchild[p];
	sp->parent[sp->lchild[p]] = r;
	sp->parent[sp->rchild[p]] = r;
	if (sp->rchild[sp->parent[p]] == p)
		sp->rchild[sp->parent[p]] = r;
	else
		sp->lchild[sp->parent[p]] = r;
	sp->parent[p] = NIL;  /* remove p */
}

/* deletes node p from tree */
static void delete_node(encode_state *sp, int p) {
	int  q;

	if (sp->parent[p] == NIL)
		return;  /* not in tree */
	if (sp->rchild[p] == NIL)
		q = sp->lchild[p];
	else if (sp->lchild[p] == NIL)
		q = sp->rchild[p];
	else {
		q = sp->lchild[p];
		if (sp->rchild[q] != NIL) {
			do {
				q = sp->rchild[q];
			} while (sp->rchild[q] != NIL);
			sp->rchild[sp->parent[q]] = sp->lchild[q];
			sp->parent[sp->lchild[q]] = sp->parent[q];
			sp->lchild[q] = sp->lchild[p];
			sp->parent[sp->lchild[p]] = q;
		}
		sp->rchild[q] = sp->rchild[p];
		sp->parent[sp->rchild[p]] = q;
	}
	sp->parent[q] = sp->parent[p];
	if (sp->rchild[sp->parent[p]] == p)
		sp->rchild[sp->parent[p]] = q;
	else
		sp->lchild[sp->parent[p]] = q;
	sp->parent[p] = NIL;
}

static uint8_t *compress_lzss(uint8_t *dst, uint32_t dstlen, const uint8_t *src, uint32_t srclen) {
	uint8_t *result = nullptr;
	/* Encoding state, mostly tree but some current match stuff */
	encode_state *sp;

	int  i, c, len, r, s, last_match_length, code_buf_ptr;
	uint8_t code_buf[17], mask;
	auto srcend = src + srclen;
	auto dstend = dst + dstlen;

	/* initialize trees */
	sp = Buffer::create<encode_state>(1);
	if (!sp) goto finish;

	init_state(sp);

	/*
	 * code_buf[1..16] saves eight units of code, and code_buf[0] works
	 * as eight flags, "1" representing that the unit is an unencoded
	 * letter (1 byte), "0" a position-and-length pair (2 bytes).
	 * Thus, eight units require at most 16 bytes of code.
	 */
	code_buf[0] = 0;
	code_buf_ptr = mask = 1;

	/* Clear the buffer with any character that will appear often. */
	s = 0;  r = RBSIZE - UPLIM;

	/* Read F bytes into the last F bytes of the buffer */
	for (len = 0; len < static_cast<int>(UPLIM) && src < srcend; len++)
		sp->text_buf[r + len] = *src++;
	if (!len)
		goto finish;

	/*
	 * Insert the F strings, each of which begins with one or more
	 * 'space' characters.  Note the order in which these strings are
	 * inserted.  This way, degenerate trees will be less likely to occur.
	 */
	for (i = 1; i <= static_cast<int>(UPLIM); i++)
		insert_node(sp, r - i);

	/*
	 * Finally, insert the whole string just read.
	 * The global variables match_length and match_position are set.
	 */
	insert_node(sp, r);
	do {
		/* match_length may be spuriously long near the end of text. */
		if (sp->match_length > len)
			sp->match_length = len;
		if (sp->match_length <= static_cast<int>(THRESHOLD)) {
			sp->match_length = 1;  /* Not long enough match.  Send one byte. */
			code_buf[0] |= mask;  /* 'send one byte' flag */
			code_buf[code_buf_ptr++] = sp->text_buf[r];  /* Send uncoded. */
		} else {
			/* Send position and length pair. Note match_length > THRESHOLD. */
			code_buf[code_buf_ptr++] = static_cast<uint8_t>(sp->match_position);
			code_buf[code_buf_ptr++] = static_cast<uint8_t>
			( ((sp->match_position >> 4) & 0xF0)
			 |  (sp->match_length - (THRESHOLD + 1)) );
		}
		if ((mask <<= 1) == 0) {  /* Shift mask left one bit. */
			/* Send at most 8 units of code together */
			for (i = 0; i < code_buf_ptr; i++)
				if (dst < dstend)
					*dst++ = code_buf[i];
				else
					goto finish;
			code_buf[0] = 0;
			code_buf_ptr = mask = 1;
		}
		last_match_length = sp->match_length;
		for (i = 0; i < last_match_length && src < srcend; i++) {
			delete_node(sp, s);    /* Delete old strings and */
			c = *src++;
			sp->text_buf[s] = c;    /* read new bytes */

			/*
			 * If the position is near the end of buffer, extend the buffer
			 * to make string comparison easier.
			 */
			if (s < static_cast<int>(UPLIM - 1))
				sp->text_buf[s + RBSIZE] = c;

			/* Since this is a ring buffer, increment the position modulo N. */
			s = (s + 1) & (RBSIZE - 1);
			r = (r + 1) & (RBSIZE - 1);

			/* Register the string in text_buf[r..r+F-1] */
			insert_node(sp, r);
		}
		while (i++ < last_match_length) {
			delete_node(sp, s);

			/* After the end of text, no need to read, */
			s = (s + 1) & (RBSIZE - 1);
			r = (r + 1) & (RBSIZE - 1);
			/* but buffer may not be empty. */
			if (--len)
				insert_node(sp, r);
		}
	} while (len > 0);   /* until length of string to be processed is zero */

	if (code_buf_ptr > 1) {    /* Send remaining code. */
		for (i = 0; i < code_buf_ptr; i++)
			if (dst < dstend)
				*dst++ = code_buf[i];
			else
				goto finish;
	}

	result = dst;

finish:
	if (sp) Buffer::deleter(sp);

	return result;
}

static uint8_t *decompressInternal(uint32_t compression, uint32_t *dstlen, const uint8_t *src, uint32_t srclen, uint8_t *buffer, bool checkResult) {
	auto decompressedBuf = buffer ? buffer : Buffer::create<uint8_t>(*dstlen);
	if (decompressedBuf) {
		size_t size {0};
		switch (compression) {
			case Compression::ModeLZSS:
				size = decompress_lzss(decompressedBuf, *dstlen, src, srclen);
				break;
			case Compression::ModeLZVN:
				size = lzvn_decode_buffer(decompressedBuf, *dstlen, src, srclen);
				break;
			case Compression::ModeZLIB:
				size = decompress_zlib(decompressedBuf, *dstlen, src, srclen);
				break;
			default:
				SYSLOG("comp", "unsupported decompression format %X", compression);
		}

		if (!checkResult || size == *dstlen) {
			*dstlen = (uint32_t) size;
			return decompressedBuf;
		} else {
			SYSLOG("comp", "failed to correctly decompress the data");
		}

		if (!buffer) Buffer::deleter(decompressedBuf);
	} else {
		SYSLOG("comp", "failed to allocate memory for decompression buffer of %u", *dstlen);
	}

	return 0;
}

uint8_t *Compression::decompress(uint32_t compression, uint32_t dstlen, const uint8_t *src, uint32_t srclen, uint8_t *buffer) {
	return decompressInternal(compression, &dstlen, src, srclen, buffer, true);
}

uint8_t *Compression::decompress(uint32_t compression, uint32_t *dstlen, const uint8_t *src, uint32_t srclen, uint8_t *buffer) {
	return decompressInternal(compression, dstlen, src, srclen, buffer, false);
}

uint8_t *Compression::compress(uint32_t compression, uint32_t &dstlen, const uint8_t *src, uint32_t srclen, uint8_t *buffer) {
	auto compressedBuf = buffer ? buffer : Buffer::create<uint8_t>(dstlen);
	if (compressedBuf) {
		uint8_t *endptr = nullptr;
		if (compression == ModeLZSS)
			endptr = compress_lzss(compressedBuf, dstlen, src, srclen);
		else
			SYSLOG("comp", "unsupported compression format %X", compression);

		if (endptr) {
			dstlen = static_cast<uint32_t>(endptr-compressedBuf);
			if (!buffer) Buffer::resize(compressedBuf, dstlen);
			return compressedBuf;
		} else {
			SYSLOG("compression", "failed to correctly compress the data");
		}

		if (!buffer) Buffer::deleter(compressedBuf);
	} else {
		SYSLOG("comp", "failed to allocate memory for compression buffer of %u", dstlen);
	}

	return nullptr;
}

#endif /* LILU_COMPRESSION_SUPPORT */
