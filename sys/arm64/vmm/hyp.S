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
#include <machine/armreg.h>
#include <machine/asm.h>
#include <machine/hypervisor.h>
#include <arm/arm/gic_common.h>

#include "hyp_assym.h"
#include "hyp_macros.h"
#include "hyp.h"

	.text

	.globl	hyp_code_start
	.globl	hyp_code_end

	.align 12

hyp_code_start:

	.align 3

	.globl _C_LABEL(hyp_debug1)
_C_LABEL(hyp_debug1):
	.zero 8
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

	.align 3

tcr:
	/* 48 bits physical addresses */
	.quad (TCR_EL2_RES1 | TCR_EL2_PS_48BITS)
sctlr_el2_set:
	/* Bits to set */
	.quad (SCTLR_EL2_M | SCTLR_EL2_C | SCTLR_EL2_SA | SCTLR_EL2_I)
sctlr_el2_clear:
	/* Bits to clear */
	.quad (SCTLR_EL2_WXN | SCTLR_EL2_A)

/*
 * Initialize the hypervisor mode with a new exception vector table, translation
 * table and stack.
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

	/* Use the host VTTBR_EL2 to tell the host and the guests apart */
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

	/* Return success */
	mov	x0, #0

	/* MMU is up and running */
	eret
END(handle_hyp_init)

.macro wth num
	.align	7
	mov	x0, \num
	eret
.endm

	.align 11

	.globl	hyp_vectors
hyp_vectors:
	wth 10			/* Synchronous EL2t */
	wth 11			/* IRQ EL2t */
	wth 12			/* FIQ EL2t */
	wth 13			/* Error EL2t */

	wth 14			/* Synchronous EL2h */
	wth 15			/* IRQ EL2h */
	wth 16			/* FIQ EL2h */
	wth 17			/* Error EL2h */

	vector el1_sync		/* Synchronous 64-bit EL1 */
	vector el1_irq		/* IRQ 64-bit EL1 */
	vector el1_fiq		/* FIQ 64-bit EL1 */
	vector el1_error	/* Error 64-bit EL1 */

	wth 18			/* Synchronous 32-bit EL1 */
	wth 19			/* IRQ 32-bit EL1 */
	wth 20			/* FIQ 32-bit EL1 */
	wth 21			/* Error 32-bit EL1 */

.macro ktohyp reg
	.align	7
	ldr	x7, kva_mask
	and	\reg, \reg, x7
	ldr	x7, kva_off
	orr	\reg, \reg, x7
.endm

kva_mask:
	.quad 0x0000ffffffffffff

kva_off:
	.quad 0x0000800000000000

.macro do_world_switch_to_host
	.align 	7
	SAVE_GUEST_REGS()
	LOAD_HOST_REGS()
	SAVE_EXIT_INFO()
.endm

ENTRY(handle_el1_sync)
	/* Save registers before modifying so we can restore them */
	str	x1, [sp, #-16]!

	/* Check for host hypervisor call */
	mrs	x1, vttbr_el2
	cmp	x1, #VTTBR_HOST
	mov	x0, #42
	eret
	beq	1f

	/* Restore register */
	ldr	x1, [sp], #16

	/* Guest exception taken to EL2 */
	do_world_switch_to_host
	mov	x0, #EXCP_TYPE_EL1_SYNC
	b	exit

1:
	/* Restore register */
	ldr	x1, [sp], #16

	/* TODO: delete me */
	mov	x0, #42 
	b 	exit

	cmp 	x0, #HYP_GET_VECTOR_TABLE
	beq	2f
	b	call_function
2:
	/* Return the vector table base address */
	mrs	x0, vbar_el2
exit:
	eret
END(handle_el1_sync)

/*
 * Call a function in EL2 context
 *
 * Expecting:
 * x0 - function virtual address
 * x1-x7 - function parameters
 */
ENTRY(call_function)
	/* Save the function address before shuffling parameters */
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
	br 	x9
END(call_function)

ENTRY(handle_el1_irq)
	do_world_switch_to_host
	mov	x0, EXCP_TYPE_EL1_IRQ
	eret
END(handle_el1_irq)

ENTRY(handle_el1_fiq)
	do_world_switch_to_host
	mov	x0, EXCP_TYPE_EL1_FIQ
	eret
END(handle_el1_fiq)

ENTRY(handle_el1_error)
	do_world_switch_to_host
	mov	x0, EXCP_TYPE_EL1_ERROR
	eret
END(handle_el1_error)

/*
 * Usage:
 * void vmm_enter_guest(struct hypctx *hypctx)
 *
 * Expecting:
 * x0 - hypctx address
 */
ENTRY(vmm_enter_guest)
	/* Save hypctx address */
	msr	tpidr_el2, x0

	SAVE_HOST_REGS()
	LOAD_GUEST_REGS()
	
	/* TODO: delete me */
	do_world_switch_to_host
	mov	x0, EXCP_TYPE_EL1_FIQ

	/* Enter guest */
	eret
END(vmm_enter_guest)

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
	bic	x2, x2, #SCTLR_EL2_M
	msr	sctlr_el2, x2

	eret
END(vmm_cleanup)

hyp_code_end:
