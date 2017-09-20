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

#include <Headers/kern_compat.hpp>
#include <libkern/OSByteOrder.h>
#include <IOKit/IOLib.h>
#include <string.h>

#define DEBUG_STATE_ENABLED		0

#if DEBUG_STATE_ENABLED
#define _LZVN_DEBUG_DUMP(x...)	IOLog(x)
#else
#define _LZVN_DEBUG_DUMP(x...)
#endif

#define LZVN_0		0
#define LZVN_1		1
#define LZVN_2		2
#define LZVN_3		3
#define LZVN_4		4
#define LZVN_5		5
#define LZVN_6		6
#define LZVN_7		7
#define LZVN_8		8
#define LZVN_9		9
#define LZVN_10		10
#define LZVN_11		11

#define CASE_TABLE	127

//==============================================================================

size_t lzvn_decode(void * decompressedData, size_t decompressedSize, void * compressedData, size_t compressedSize)
{
	const uint64_t decompBuffer = (const uint64_t)decompressedData;

	size_t	length	= 0;															// xor	%rax,%rax

	uint64_t compBuffer	= (uint64_t)compressedData;

	uint64_t compBufferPointer	= 0;												// use p(ointer)?
	uint64_t caseTableIndex	= 0;
	uint64_t byteCount		= 0;
	uint64_t currentLength	= 0;													// xor	%r12,%r12
	uint64_t negativeOffset	= 0;
	uint64_t address		= 0;													// ((uint64_t)compBuffer + compBufferPointer)

	uint8_t jmpTo			= CASE_TABLE;											// On the first run!

	// Example values:
	//
	// byteCount: 10,	negativeOffset: 28957,	length: 42205762, currentLength: 42205772, compBufferPointer: 42176805
	// byteCount: 152,	negativeOffset: 28957,	length: 42205772, currentLength: 42205924, compBufferPointer: 42176815
	// byteCount: 10,	negativeOffset: 7933,	length: 42205924, currentLength: 42205934, compBufferPointer: 42197991
	// byteCount: 45,	negativeOffset: 7933,	length: 42205934, currentLength: 42205979, compBufferPointer: 42198001
	// byteCount: 9,	negativeOffset: 64,		length: 42205979, currentLength: 42205988, compBufferPointer: 42205915
	// byteCount: 10,	negativeOffset: 8180,	length: 42205988, currentLength: 42205998, compBufferPointer: 42197808
	// byteCount: 59,	negativeOffset: 8180,	length: 42205998, currentLength: 42206057, compBufferPointer: 42197818
	// byteCount: 10,	negativeOffset: 359,	length: 42206057, currentLength: 42206067, compBufferPointer: 42205698
	// byteCount: 1,	negativeOffset: 359,	length: 42206067, currentLength: 42206068, compBufferPointer: 42205708
	// byteCount: 10,	negativeOffset: 29021,	length: 42206068, currentLength: 42206078, compBufferPointer: 42177047
	//
	// length + byteCount = currentLength
	// currentLength - (negativeOffset + byteCount) = compBufferPointer
	// length - negativeOffset = compBufferPointer

	static short caseTable[ 256 ] =
	{
		1,  1,  1,  1,    1,  1,  2,  3,    1,  1,  1,  1,    1,  1,  4,  3,
		1,  1,  1,  1,    1,  1,  4,  3,    1,  1,  1,  1,    1,  1,  5,  3,
		1,  1,  1,  1,    1,  1,  5,  3,    1,  1,  1,  1,    1,  1,  5,  3,
		1,  1,  1,  1,    1,  1,  5,  3,    1,  1,  1,  1,    1,  1,  5,  3,
		1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
		1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
		1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
		5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,
		1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
		1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
		6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,
		6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,    6,  6,  6,  6,
		1,  1,  1,  1,    1,  1,  0,  3,    1,  1,  1,  1,    1,  1,  0,  3,
		5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,    5,  5,  5,  5,
		7,  8,  8,  8,    8,  8,  8,  8,    8,  8,  8,  8,    8,  8,  8,  8,
		9, 10, 10, 10,   10, 10, 10, 10,   10, 10, 10, 10,   10, 10, 10, 10
	};

	decompressedSize -= 8;															// sub	$0x8,%rsi

	if (decompressedSize < 8)														// jb	Llzvn_exit
	{
		return 0;
	}

	compressedSize = (compBuffer + compressedSize - 8);								// lea	-0x8(%rdx,%rcx,1),%rcx

	if (compBuffer > compressedSize)												// cmp	%rcx,%rdx
	{
		return 0;																	// ja	Llzvn_exit
	}

	compBufferPointer = *(uint64_t *)compBuffer;									// mov	(%rdx),%r8
	caseTableIndex = (compBufferPointer & 255);										// movzbq	(%rdx),%r9

	do																				// jmpq	*(%rbx,%r9,8)
	{
		switch (jmpTo)																// our jump table
		{
			case CASE_TABLE: /******************************************************/

				switch (caseTable[(uint8_t)caseTableIndex])
				{
					case 0: _LZVN_DEBUG_DUMP("caseTable[0]\n");

							caseTableIndex >>= 6;									// shr	$0x6,%r9
							compBuffer = (compBuffer + caseTableIndex + 1);			// lea	0x1(%rdx,%r9,1),%rdx
						
							if (compBuffer > compressedSize)						// cmp	%rcx,%rdx
							{
								return 0;											// ja	Llzvn_exit
							}
						
							byteCount = 56;											// mov	$0x38,%r10
							byteCount &= compBufferPointer;							// and	%r8,%r10
							compBufferPointer >>= 8;								// shr	$0x8,%r8
							byteCount >>= 3;										// shr	$0x3,%r10
							byteCount += 3;											// add	$0x3,%r10
						
							jmpTo = LZVN_10;										// jmp	Llzvn_l10
							break;
						
					case 1:	_LZVN_DEBUG_DUMP("caseTable[1]\n");

							caseTableIndex >>= 6;									// shr	$0x6,%r9
							compBuffer = (compBuffer + caseTableIndex + 2);			// lea	0x2(%rdx,%r9,1),%rdx

							if (compBuffer > compressedSize)						// cmp	%rcx,%rdx
							{
								return 0;											// ja	Llzvn_exit
							}
						
							negativeOffset = compBufferPointer;						// mov	%r8,%r12
							negativeOffset = OSSwapInt64(negativeOffset);			// bswap	%r12
							byteCount = negativeOffset;								// mov	%r12,%r10
							negativeOffset <<= 5;									// shl	$0x5,%r12
							byteCount <<= 2;										// shl	$0x2,%r10
							negativeOffset >>= 53;									// shr	$0x35,%r12
							byteCount >>= 61;										// shr	$0x3d,%r10
							compBufferPointer >>= 16;								// shr	$0x10,%r8
							byteCount += 3;											// add	$0x3,%r10

							jmpTo = LZVN_10;										// jmp	Llzvn_l10
							break;

					case 2: _LZVN_DEBUG_DUMP("caseTable[2]\n");

							return length;
			
					case 3: _LZVN_DEBUG_DUMP("caseTable[3]\n");

							caseTableIndex >>= 6;									// shr	$0x6,%r9
							compBuffer = (compBuffer + caseTableIndex + 3);			// lea	0x3(%rdx,%r9,1),%rdx

							if (compBuffer > compressedSize)						// cmp	%rcx,%rdx
							{
								return 0;											// ja	Llzvn_exit
							}
						
							byteCount = 56;											// mov	$0x38,%r10
							negativeOffset = 65535;									// mov	$0xffff,%r12
							byteCount &= compBufferPointer;							// and	%r8,%r10
							compBufferPointer >>= 8;								// shr	$0x8,%r8
							byteCount >>= 3;										// shr	$0x3,%r10
							negativeOffset &= compBufferPointer;					// and	%r8,%r12
							compBufferPointer >>= 16;								// shr	$0x10,%r8
							byteCount += 3;											// add	$0x3,%r10
						
							jmpTo = LZVN_10;										// jmp	Llzvn_l10
							break;
						
					case 4:	_LZVN_DEBUG_DUMP("caseTable[4]\n");

							compBuffer++;											// add	$0x1,%rdx

							if (compBuffer > compressedSize)						// cmp	%rcx,%rdx
							{
								return 0;											// ja	Llzvn_exit
							}
						
							compBufferPointer = *(uint64_t *)compBuffer;			// mov	(%rdx),%r8
							caseTableIndex = (compBufferPointer & 255);				// movzbq (%rdx),%r9
						
							jmpTo = CASE_TABLE;										// continue;
							break;													// jmpq	*(%rbx,%r9,8)

					case 5: _LZVN_DEBUG_DUMP("caseTable[5]\n");

							return 0;												// Llzvn_table5;
					
					case 6: _LZVN_DEBUG_DUMP("caseTable[6]\n");

							caseTableIndex >>= 3;									// shr	$0x3,%r9
							caseTableIndex &= 3;									// and	$0x3,%r9
							compBuffer = (compBuffer + caseTableIndex + 3);			// lea	0x3(%rdx,%r9,1),%rdx

							if (compBuffer > compressedSize)						// cmp	%rcx,%rdx
							{
								return 0;											// ja	Llzvn_exit
							}
						
							byteCount = compBufferPointer;							// mov	%r8,%r10
							byteCount &= 775;										// and	$0x307,%r10
							compBufferPointer >>= 10;								// shr	$0xa,%r8
							negativeOffset = (byteCount & 255);						// movzbq %r10b,%r12
							byteCount >>= 8;										// shr	$0x8,%r10
							negativeOffset <<= 2;									// shl	$0x2,%r12
							byteCount |= negativeOffset;							// or	%r12,%r10
							negativeOffset = 16383;									// mov	$0x3fff,%r12
							byteCount += 3;											// add	$0x3,%r10
							negativeOffset &= compBufferPointer;					// and	%r8,%r12
							compBufferPointer >>= 14;								// shr	$0xe,%r8

							jmpTo = LZVN_10;										// jmp	Llzvn_l10
							break;
						
					case 7:	_LZVN_DEBUG_DUMP("caseTable[7]\n");

							compBufferPointer >>= 8;								// shr	$0x8,%r8
							compBufferPointer &= 255;								// and	$0xff,%r8
							compBufferPointer += 16;								// add	$0x10,%r8
							compBuffer = (compBuffer + compBufferPointer + 2);		// lea	0x2(%rdx,%r8,1),%rdx

							jmpTo = LZVN_0;											// jmp	Llzvn_l0
							break;
						
					case 8: _LZVN_DEBUG_DUMP("caseTable[8]\n");

							compBufferPointer &= 15;								// and	$0xf,%r8
							compBuffer = (compBuffer + compBufferPointer + 1);		// lea	0x1(%rdx,%r8,1),%rdx
						
							jmpTo = LZVN_0;											// jmp	Llzvn_l0
							break;
					
					case 9:	_LZVN_DEBUG_DUMP("caseTable[9]\n");

							compBuffer += 2;										// add	$0x2,%rdx
					
							if (compBuffer > compressedSize)						// cmp	%rcx,%rdx
							{
								return 0;											// ja	Llzvn_exit
							}

							// Up most significant byte (count) by 16 (0x10/16 - 0x10f/271).
							byteCount = compBufferPointer;							// mov	%r8,%r10
							byteCount >>= 8;										// shr	$0x8,%r10
							byteCount &= 255;										// and	$0xff,%r10
							byteCount += 16;										// add	$0x10,%r10

							jmpTo = LZVN_11;										// jmp	Llzvn_l11
							break;

					case 10:_LZVN_DEBUG_DUMP("caseTable[10]\n");

							compBuffer++;											// add	$0x1,%rdx
							
							if (compBuffer > compressedSize)						// cmp	%rcx,%rdx
							{
								return 0;											// ja	Llzvn_exit
							}
						
							byteCount = compBufferPointer;							// mov	%r8,%r10
							byteCount &= 15;										// and	$0xf,%r10
						
							jmpTo = LZVN_11;										// jmp	Llzvn_l11
							break;
					default:_LZVN_DEBUG_DUMP("default() caseTableIndex[%d]\n", (uint8_t)caseTableIndex);
				}																	// switch (caseTable[caseTableIndex])

				break;

			case LZVN_0: /**********************************************************/

				_LZVN_DEBUG_DUMP("jmpTable(0)\n");

				if (compBuffer > compressedSize)									// cmp	%rcx,%rdx
				{
					return 0;														// ja	Llzvn_exit
				}
				
				currentLength = (length + compBufferPointer);						// lea	(%rax,%r8,1),%r11
				compBufferPointer = -compBufferPointer;								// neg	%r8
				
				if (currentLength > decompressedSize)								// cmp	%rsi,%r11
				{
					jmpTo = LZVN_2;													// ja	Llzvn_l2
					break;
				}

				currentLength = (decompBuffer + currentLength);						// lea	(%rdi,%r11,1),%r11

			case LZVN_1: /**********************************************************/

				do																	// Llzvn_l1:
				{
					_LZVN_DEBUG_DUMP("jmpTable(1)\n");

//					caseTableIndex = *(uint64_t *)((uint64_t)compBuffer + compBufferPointer);

					address = (compBuffer + compBufferPointer);						// mov	(%rdx,%r8,1),%r9
					caseTableIndex = *(uint64_t *)address;

//					*(uint64_t *)((uint64_t)currentLength + compBufferPointer) = caseTableIndex;
// or:
//					lilu_os_memcpy((void *)currentLength + compBufferPointer, &caseTableIndex, 8);
// or:
					address = (currentLength + compBufferPointer);					// mov	%r9,(%r11,%r8,1)
					*(uint64_t *)address = caseTableIndex;
					compBufferPointer += 8;											// add	$0x8,%r8

				} while ((UINT64_MAX - (compBufferPointer - 8)) >= 8);				// jae	Llzvn_l1

				length = currentLength;												// mov	%r11,%rax
				length -= decompBuffer;												// sub	%rdi,%rax
				
				compBufferPointer = *(uint64_t *)compBuffer;						// mov	(%rdx),%r8
				caseTableIndex = (compBufferPointer & 255);							// movzbq (%rdx),%r9

				jmpTo = CASE_TABLE;
				break;																// jmpq	*(%rbx,%r9,8)

			case LZVN_2: /**********************************************************/

				_LZVN_DEBUG_DUMP("jmpTable(2)\n");

				currentLength = (decompressedSize + 8);								// lea	0x8(%rsi),%r11

			case LZVN_3: /***********************************************************/

				do																	// Llzvn_l3: (block copy of bytes)
				{
					_LZVN_DEBUG_DUMP("jmpTable(3)\n");

					address = (compBuffer + compBufferPointer);						// movzbq (%rdx,%r8,1),%r9
					caseTableIndex = (*((uint64_t *)address) & 255);
					lilu_os_memcpy((void *)decompBuffer + length, &caseTableIndex, 1);
					length++;														// add	$0x1,%rax
					
					if (currentLength == length)									// cmp	%rax,%r11
					{
						return length;												// je	Llzvn_exit2
					}
					
					compBufferPointer++;											// add	$0x1,%r8
					
				} while ((int64_t)compBufferPointer != 0);							// jne	Llzvn_l3
				
				compBufferPointer = *(uint64_t *)compBuffer;						// mov	(%rdx),%r8
				caseTableIndex = (compBufferPointer & 255);							// movzbq	(%rdx),%r9

				jmpTo = CASE_TABLE;
				break;																// jmpq	*(%rbx,%r9,8)

			case LZVN_4: /**********************************************************/

				_LZVN_DEBUG_DUMP("jmpTable(4)\n");

				currentLength = (decompressedSize + 8);								// lea	0x8(%rsi),%r11

			case LZVN_9: /**********************************************************/

				do																	// Llzvn_l9: (block copy of bytes)
				{
					_LZVN_DEBUG_DUMP("jmpTable(9)\n");

					address = (decompBuffer + compBufferPointer);					// movzbq (%rdi,%r8,1),%r9
					caseTableIndex = (*((uint8_t *)address) & 255);

					compBufferPointer++;											// add	$0x1,%r8
					lilu_os_memcpy((void *)decompBuffer + length, &caseTableIndex, 1);		// mov	%r9,(%rdi,%rax,1)
					length++;														// add	$0x1,%rax
					
					if (length == currentLength)									// cmp	%rax,%r11
					{
						return length;												// je	Llzvn_exit2
					}

					byteCount--;													// sub	$0x1,%r10
					
				} while (byteCount);												// jne	Llzvn_l9
				
				compBufferPointer = *(uint64_t *)compBuffer;						// mov	(%rdx),%r8
				caseTableIndex = (compBufferPointer & 255);							// movzbq	(%rdx),%r9

				jmpTo = CASE_TABLE;
				break;																// jmpq	*(%rbx,%r9,8)

			case LZVN_5: /**********************************************************/

				do																	// Llzvn_l5: (block copy of qwords)
				{
					_LZVN_DEBUG_DUMP("jmpTable(5)\n");

					address = (decompBuffer + compBufferPointer);					// mov	(%rdi,%r8,1),%r9
					caseTableIndex = *((uint64_t *)address);

					compBufferPointer += 8;											// add	$0x8,%r8
					lilu_os_memcpy((void *)decompBuffer + length, &caseTableIndex, 8);		// mov	%r9,(%rdi,%rax,1)
					length += 8;													// add	$0x8,%rax
					byteCount -= 8;													// sub	$0x8,%r10
					
				} while ((byteCount + 8) > 8);										// ja	Llzvn_l5

				length += byteCount;												// add	%r10,%rax
				compBufferPointer = *(uint64_t *)compBuffer;						// mov	(%rdx),%r8
				caseTableIndex = (compBufferPointer & 255);							// movzbq	(%rdx),%r9

				jmpTo = CASE_TABLE;
				break;																// jmpq	*(%rbx,%r9,8)

			case LZVN_10: /*********************************************************/

				_LZVN_DEBUG_DUMP("jmpTable(10)\n");

				currentLength = (length + caseTableIndex);							// lea	(%rax,%r9,1),%r11
				currentLength += byteCount;											// add	%r10,%r11

				if (currentLength < decompressedSize)								// cmp	%rsi,%r11 (block_end: jae	Llzvn_l8)
				{
					lilu_os_memcpy((void *)decompBuffer + length, &compBufferPointer, 8);	// mov	%r8,(%rdi,%rax,1)
					length += caseTableIndex;										// add	%r9,%rax
					compBufferPointer = length;										// mov	%rax,%r8
						
					if (compBufferPointer < negativeOffset)							// jb	Llzvn_exit
					{
						return 0;
					}

					compBufferPointer -= negativeOffset;							// sub	%r12,%r8

					if (negativeOffset < 8)											// cmp	$0x8,%r12
					{
						jmpTo = LZVN_4;												// jb	Llzvn_l4
						break;
					}

					jmpTo = LZVN_5;													// jmpq	*(%rbx,%r9,8)
					break;
				}

			case LZVN_8: /**********************************************************/

				_LZVN_DEBUG_DUMP("jmpTable(8)\n");

				if (caseTableIndex == 0)											// test	%r9,%r9
				{
					jmpTo = LZVN_7;													// jmpq	*(%rbx,%r9,8)
					break;
				}

				currentLength = (decompressedSize + 8);								// lea	0x8(%rsi),%r11

			case LZVN_6: /**********************************************************/

				do
				{
					_LZVN_DEBUG_DUMP("jmpTable(6)\n");

					lilu_os_memcpy((void *)decompBuffer + length, &compBufferPointer, 1);	// mov	%r8b,(%rdi,%rax,1)
					length++;														// add	$0x1,%rax
						
					if (length == currentLength)									// cmp	%rax,%r11
					{
						return length;												// je	Llzvn_exit2
					}
						
					compBufferPointer >>= 8;										// shr	$0x8,%r8
					caseTableIndex--;												// sub	$0x1,%r9
						
				} while (caseTableIndex != 1);										// jne	Llzvn_l6

			case LZVN_7: /**********************************************************/

				_LZVN_DEBUG_DUMP("jmpTable(7)\n");

				compBufferPointer = length;											// mov	%rax,%r8
				compBufferPointer -= negativeOffset;								// sub	%r12,%r8

				if (compBufferPointer < negativeOffset)								// jb	Llzvn_exit
				{
					return 0;
				}

				jmpTo = LZVN_4;
				break;																// jmpq	*(%rbx,%r9,8)
	
			case LZVN_11: /*********************************************************/

				_LZVN_DEBUG_DUMP("jmpTable(11)\n");

				compBufferPointer = length;											// mov	%rax,%r8
				compBufferPointer -= negativeOffset;								// sub	%r12,%r8
				currentLength = (length + byteCount);								// lea	(%rax,%r10,1),%r11
				
				if (currentLength < decompressedSize)								// cmp	%rsi,%r11
				{
					if (negativeOffset >= 8)										// cmp	$0x8,%r12
					{
						jmpTo = LZVN_5;												// jae	Llzvn_l5
						break;
					}
				}
				
				jmpTo = LZVN_4;														// jmp	Llzvn_l4
				break;
		}																			// switch (jmpq)

	} while (1);

	return 0;
}

