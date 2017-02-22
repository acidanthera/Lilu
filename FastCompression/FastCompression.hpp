/*
 * Copyright (c) 2014 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * Portions Copyright (c) 2003 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 2.0 (the "License").  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 *
 * The lzvn_decode function was first located and disassembled by Pike R.
 * Alpha, after that Andy Vandijck wrote a little C program that called the
 * assembler code, which 'MinusZwei' converted to flat C code. And below
 * you'll find my conversion of the assembler code, this time to a more
 * traditional C-style format, to make it easier to understand.
 *
 * Thanks to Andy Vandijck and 'MinusZwei' for their hard work!
 */

extern "C" {
	size_t lzvn_decode(void *dst, size_t dst_size, const void *src, size_t src_size);
}