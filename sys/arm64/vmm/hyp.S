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

	.text

	.globl	hyp_code_start
	.globl	hyp_code_end

	.align 12

hyp_code_start:

	.align 3
	.globl _C_LABEL(hyp_debug1)
_C_LABEL(hyp_debug1):
	.zero 8

	.align 3
	.globl _C_LABEL(hyp_debug2)
_C_LABEL(hyp_debug2):
	.zero 8

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

/*
 * Initialize the hypervisor mode with a new exception vector table, translation
 * tables and stack.
 *
 * Expecting:
 * x0 - the hypervisor exception vectors
 * x1 - translation tables physical address
 * x2 - stack top virtual address
 */
ENTRY(handle_hyp_init)
	/* Install the new exception vectors */
	msr	vbar_el2, x0

	/* Set the stack top address */
	mov	sp, x2

	/* Set the host VMID */
	mov	x2, #VTTBR_HOST
	msr	vttbr_el2, x2

	/* Load the base address for the translation tables */
	msr	ttbr0_el2, x1
	isb

	/* Invalidate the TLB */
	tlbi	alle2

	/* Use the same memory attributes as EL1 */
	mrs	x3, mair_el1
	msr	mair_el2, x3
	isb

	/*
	 * We've generated the translation tables using pmap, set up TCR
 	 * using the kernel EL1 values.
	 */
	mrs	x3, tcr_el1
	ldr	x4, tcr
	/* Copy T0SZ from EL1 */
	bfi	x4, x3, #0, #6
	/* Copy IRGN0, ORGN0, SH0 and TG0 from EL1 */
	lsr	x3, x3, #TCR_IRGN0_SHIFT
	bfi	x4, x3, #8, #8
	msr	tcr_el2, x4

	/* Set up SCTLR */
	mrs	x3, sctlr_el2
	ldr	x4, sctlr_el2_set
	ldr	x5, sctlr_el2_clear
	bic	x3, x3, x5
	orr	x3, x3, x4
	msr	sctlr_el2, x3

	/* MMU is up and running */
	eret
END(handle_hyp_init)

	.align 3
tcr:
	/* 48 bits physical addresses */
	.quad (TCR_EL2_RES1 | TCR_EL2_PS_48BITS)

sctlr_el2_set:
	/* Bits to set */
	.quad (SCTLR_EL2_M | SCTLR_EL2_C | SCTLR_EL2_SA | SCTLR_I)

sctlr_el2_clear:
	/* Bits to clear */
	.quad (SCTLR_EL2_WXN | SCTLR_EL2_A)

hcr_el2_set:
	/* Bits to set */
	.quad (HCR_ID | HCR_CD)

hcr_el2_clear:
	/* Bits to clear */
	.quad (HCR_TRVM | HCR_HCD | HCR_TDZ | HCR_TGE | HCR_TVM | HCR_TTLB | \
		HCR_TPU | HCR_TPCP | HCR_TSW | HCR_TACR | HCR_TIDCP | \
		HCR_TSC | HCR_TID3 | HCR_TID2 | HCR_TID1 | HCR_TID0 | \
		HCR_TWE | HCR_TWI | HCR_AMO | HCR_IMO | HCR_FMO | HCR_PTW | \
		HCR_VM)

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
	/* Check for host hypervisor call */
	mrs	x9, vttbr_el2
	cmp	x9, #VTTBR_HOST
	bne	9f

	/* Return the vector table base address if is argument is -1 */
	cmp 	x0, #-1
	b.ne	1f
	mrs	x0, vbar_el2
	b	9f
1:
	/*
	 * Call function in EL2 context.
	 */
	/* Push the vmm_call_hyp() return address on the stack */
	str	lr, [sp, #-16]!

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

	/* Pop the vmm_call_hyp() return address from the stack */
	ldr	lr, [sp], #16;
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
	/* Pop vmm_call_hyp() return address */
	ldr	lr, [sp], #16

	/* Restore the stub vectors */
	msr	vbar_el2, x0

	/* Disable the MMU */
	dsb	sy
	mrs	x2, sctlr_el2
	bic	x2, x2, #SCTLR_EL2_M
	msr	sctlr_el2, x2

	/* Return here from the exception */
	eret
END(vmm_cleanup)

/*
 * Usage:
 * void vmm_enter_guest(struct hypctx *hypctx)
 *
 * Expecting:
 * x0 - hypctx address
 */
ENTRY(vmm_enter_guest)
	/* Pop vmm_call_hyp() return address */
	ldr	lr, [sp], #16

	/*
	 * SAVE_GUEST_REGS() will be called when the guest traps to hyp mode and
	 * the hypervisor expects to find the hypctx address in tpidr_el2.
	 */
	msr	tpidr_el2, x0

	/* These are the arm_init() original hypctx values */
	mov	x0, #42
	mov	x1, #42
	mov	x2, #42
	mov	x3, #42
	mov	x4, #42

	SAVE_GUEST_REGS()

	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	mov	x4, xzr

	LOAD_GUEST_REGS()

	/* Return from exception */
	eret
END(vmm_enter_guest)

hyp_code_end:
