//
//  kern_efi.cpp
//  Lilu
//
//  Copyright © 2018 vit9696. All rights reserved.
//

/*
 * Copy "count" bytes from "src" to %rsp, using
 * "tmpindex" for a scratch counter and %rax
 */
#define COPY_STACK(src, count, tmpindex) \
	mov	$0, tmpindex	/* initial scratch counter */ ; \
1: \
	mov	0(src,tmpindex,1), %rax	 /* copy one 64-bit word from source... */ ; \
	mov	%rax, 0(%rsp,tmpindex,1) /* ... to stack */ ; \
	add	$8, tmpindex		 /* increment counter */ ; \
	cmp	count, tmpindex		 /* exit it stack has been copied */ ; \
	jne 1b

/**
 * This code is a slightly modified pal_efi_call_in_64bit_mode_asm function.
 *
 * Switch from compatibility mode to long mode, and
 * then execute the function pointer with the specified
 * register and stack contents (based at %rsp). Afterwards,
 * collect the return value, restore the original state,
 * and return.
 */
.globl _performEfiCallAsm
_performEfiCallAsm:
pushq %rbp
movq %rsp, %rbp

/* save non-volatile registers */
push	%rbx
push	%r12
push	%r13
push	%r14
push	%r15

/* save parameters that we will need later */
push	%rsi
push	%rcx

sub	$8, %rsp	/* align to 16-byte boundary */
/* efi_reg in %rsi */
/* stack_contents into %rdx */
/* s_c_s into %rcx */
sub	%rcx, %rsp	/* make room for stack contents */

COPY_STACK(%rdx, %rcx, %r8)

/* load efi_reg into real registers */
mov	0(%rsi),  %rcx
mov	8(%rsi),  %rdx
mov	16(%rsi), %r8
mov	24(%rsi), %r9
mov	32(%rsi), %rax

/* func pointer in %rdi */
call	*%rdi			/* call EFI runtime */

mov	-48(%rbp), %rsi		/* load efi_reg into %esi */
mov	%rax, 32(%rsi)		/* save RAX back */

mov	-56(%rbp), %rcx	/* load s_c_s into %rcx */
add	%rcx, %rsp	/* discard stack contents */
add	$8, %rsp	/* restore stack pointer */

pop	%rcx
pop	%rsi
pop	%r15
pop	%r14
pop	%r13
pop	%r12
pop	%rbx

leave

ret
