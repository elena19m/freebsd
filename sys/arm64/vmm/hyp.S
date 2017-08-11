/*
 * Copyright (C) 2017 Alexandru Elisei <alexandru.elisei@gmail.com>
 * All rights reserved.
 *
 * This software was developed by Alexandru Elisei under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "assym.s"
#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/armreg.h>
#include <machine/hypervisor.h>
#include <arm/arm/gic_common.h>

#include "hyp_assym.h"
#include "hyp_macros.h"

/*
 * TODO
 * hypervisor.h might replace hyp.h
 *  #include "hyp.h"
 */

/*
 * TODO
 * #include "hyp_assym.h"
 * #include "hyp_helpers.h" - this was renamed to hyp_macros.h
 */

	.text

	.globl	hyp_code_start
	.globl	hyp_code_end

	.align 12

hyp_code_start:

ENTRY(vmm_call_hyp)
	hvc	#0
	ret
END(vmm_call_hyp)

.macro vempty
	.align 7
	1: b 	1b
.endm

.macro vector name
	.align 7
	b 	handle_\name
.endm

	.align 11

	.globl	hyp_init_vectors
hyp_init_vectors:
	vempty		/* Synchronous EL2t */
	vempty		/* IRQ EL2t */
	vempty		/* FIQ EL2t */
	vempty		/* Error EL2t */

	vempty		/* Synchronous EL2h */
	vempty		/* IRQ EL2h */
	vempty		/* FIQ EL2h */
	vempty		/* Error EL2h */

	vector hyp_init	/* Synchronous 64-bit EL1 */
	vempty		/* IRQ 64-bit EL1 */
	vempty		/* FIQ 64-bit EL1 */
	vempty		/* Error 64-bit EL1 */

	vempty		/* Synchronous 32-bit EL1 */
	vempty		/* IRQ 32-bit EL1 */
	vempty		/* FIQ 32-bit EL1 */
	vempty		/* Error 32-bit EL1 */

ENTRY(handle_hyp_init)
	/* TODO */
	cmp 	x0, #-1
	b.ne	1f
	mrs	x0, vbar_el2
	b	9f
1:
	/* Install the new exception vectors */
	msr	vbar_el2, x0
	/* Set the new stack top */
	mov	sp, x1
9:
	eret
END(handle_hyp_init)

	.align 11

	.globl	hyp_vectors
hyp_vectors:
	vempty		/* Synchronous EL2t */
	vempty		/* IRQ EL2t */
	vempty		/* FIQ EL2t */
	vempty		/* Error EL2t */

	vempty		/* Synchronous EL2h */
	vempty		/* IRQ EL2h */
	vempty		/* FIQ EL2h */
	vempty		/* Error EL2h */

	vector hyp_hvc	/* Synchronous 64-bit EL1 */
	vempty		/* IRQ 64-bit EL1 */
	vempty		/* FIQ 64-bit EL1 */
	vempty		/* Error 64-bit EL1 */

	vempty		/* Synchronous 32-bit EL1 */
	vempty		/* IRQ 32-bit EL1 */
	vempty		/* FIQ 32-bit EL1 */
	vempty		/* Error 32-bit EL1 */

ENTRY(handle_hyp_hvc)
	cmp 	x0, #-1
	b.ne	1f
	mrs	x0, vbar_el2
	b	9f
1:
	cmp	x0, #-2
	b.eq	2f

	/* Save vmm_call_hyp() return address */
	PUSH_REG(lr)

	/* Save function address */
	mov	x9, x0

	/* Shuffle function parameters */
	mov 	x0, x1
	mov	x1, x2
	mov 	x2, x3
	mov	x3, x4
	mov	x4, x5
	mov	x5, x6
	mov 	x6, x7

	/* Call function */
	blr 	x9

	/* Restore vmm_call_hyp() return address */
	POP_REG(lr)

	b	9f
2:
	mov	x2, #42
	mov	x3, #42
	PUSH_REG_PAIR_X(2, 3)
	mov	x2, xzr
	mov	x3, xzr
	POP_REG_PAIR_X(2, 3)
	SAVE_REG_PAIR_X(2, 3)

	PUSH_SYSTEM_REG_PAIR(CPTR_EL2, HCR_EL2)
	POP_SYSTEM_REG_PAIR(CPTR_EL2, HCR_EL2)
	isb
9:
	eret
END(handle_hyp_hvc)

/*
 * Usage:
 * void vmm_cleanup(void *hyp_stub_vectors)
 *
 * Expecting:
 * x0 - physical address of hyp_stub_vectors
 */
ENTRY(vmm_cleanup)
	/* Restore the stub vectors */
	msr	vbar_el2, x0

	/* Disable the MMU */
	dsb	sy
	mrs	x2, sctlr_el2
	bic	x2, x2, SCTLR_M
	msr	sctlr_el2, x2
	isb

	ret
END(vmm_cleanup)

/*
 * Usage:
 * void vmm_enter_guest(struct hypctx *hypctx)
 *
 * Expecting:
 * x0 - physical address of hypctx
 */
ENTRY(vmm_enter_guest)
	/* Pop vmm_call_hyp() return address */
	ldr	lr, [sp], #16;

	LOAD_REG_PAIR(X1, X2)
	LOAD_REG_PAIR(X3, X4)

	PUSH_HOST_REGS()

	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr

	POP_HOST_REGS()

	SAVE_REG_PAIR(X1, X2)
	SAVE_REG_PAIR(X3, X4)

	/* Return from exception */
	eret
END(vmm_enter_guest)

hyp_code_end:
