//
//  kern_efi_trampoline_i386.s
//  Lilu
//
//  Copyright © 2021 Goldfish64. All rights reserved.
//

#if defined(__i386__)

#define	KERNEL32_CS	0x08		/* kernel 32-bit code for 32-bit kernel */
#define	KERNEL64_CS	0x80		/* kernel 64-bit code for 32-bit kernel */

/*
 * Copy "count" bytes from "src" to %esp, using
 * "tmpindex" for a scratch counter and %eax
 */
#define COPY_STACK(src, count, tmpindex) \
	mov	$0, tmpindex	/* initial scratch counter */ ; \
1: \
	mov	0(src,tmpindex,1), %eax	 /* copy one 32-bit word from source... */ ; \
	mov	%eax, 0(%esp,tmpindex,1) /* ... to stack */ ; \
	add	$4, tmpindex		 /* increment counter */ ; \
	cmp	count, tmpindex		 /* exit it stack has been copied */ ; \
	jne 1b

/*
 * Long jump to 64-bit space from 32-bit compatibility mode.
 */
#define	ENTER_64BIT_MODE()			\
	.code32					;\
	.byte   0xea	/* far jump longmode */	;\
	.long   1f				;\
	.word   KERNEL64_CS			;\
				.code64					;\
1:

/*
 * Long jump to 32-bit compatibility mode from 64-bit space.
 */
#define ENTER_COMPAT_MODE()			\
	ljmp	*(%rip)				;\
	.long	4f				;\
	.word	KERNEL32_CS			;\
	.code32					;\
4:

/**
 * This code is a slightly modified pal_efi_call_in_64bit_mode_asm function.
 *
 * Switch from compatibility mode to long mode, and
 * then execute the function pointer with the specified
 * register and stack contents (based at %rsp). Afterwards,
 * collect the return value, restore the original state,
 * and return.
 */
.globl _performEfiCallAsm64
_performEfiCallAsm64:

pushl %ebp;
movl %esp, %ebp

/* save non-volatile registers */
push	%ebx
push	%esi
push	%edi

sub	$12, %esp	/* align to 16-byte boundary */
mov	16(%ebp), %esi	/* load efi_reg into %esi */
mov	20(%ebp), %edx	/* load stack_contents into %edx */
mov	24(%ebp), %ecx	/* load s_c_s into %ecx */
sub	%ecx, %esp	/* make room for stack contents */

COPY_STACK(%edx, %ecx, %edi)

ENTER_64BIT_MODE()

/* load efi_reg into real registers */
mov	0(%rsi),  %rcx
mov	8(%rsi),  %rdx
mov	16(%rsi), %r8
mov	24(%rsi), %r9
mov	32(%rsi), %rax

mov	8(%rbp), %rdi		/* load func pointer */
call	*%rdi			/* call EFI runtime */

mov	16(%rbp), %esi		/* load efi_reg into %esi */
mov	%rax, 32(%rsi)		/* save RAX back */

ENTER_COMPAT_MODE()

add	24(%ebp), %esp	/* discard stack contents */
add	$12, %esp	/* restore stack pointer */

pop	%edi
pop	%esi
pop	%ebx

leave

ret


/**
 * This code is a slightly modified pal_efi_call_in_32bit_mode_asm function.
 */
.globl _performEfiCallAsm32
_performEfiCallAsm32:

pushl %ebp;
movl %esp, %ebp

/* save non-volatile registers */
push	%ebx
push	%esi
push	%edi

sub	$12, %esp	/* align to 16-byte boundary */
mov	12(%ebp), %esi	/* load efi_reg into %esi */
mov	16(%ebp), %edx	/* load stack_contents into %edx */
mov	20(%ebp), %ecx	/* load s_c_s into %ecx */
sub	%ecx, %esp	/* make room for stack contents */

COPY_STACK(%edx, %ecx, %edi)

/* load efi_reg into real registers */
mov	0(%esi),  %ecx
mov	8(%esi),  %edx
mov	32(%esi), %eax

mov	8(%ebp), %edi		/* load func pointer */
call	*%edi			/* call EFI runtime */

mov	12(%ebp), %esi		/* load efi_reg into %esi */
mov	%eax, 32(%esi)		/* save RAX back */
movl	$0, 36(%esi)		/* zero out high bits of RAX */

add	20(%ebp), %esp	/* discard stack contents */
add	$12, %esp	/* restore stack pointer */

pop	%edi
pop	%esi
pop	%ebx

leave

ret

#endif
