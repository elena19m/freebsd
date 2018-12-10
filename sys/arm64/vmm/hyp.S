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


#include <sys/syscall.h>
#include <machine/armreg.h>
#include <machine/asm.h>
#include <machine/hypervisor.h>
#include <arm/arm/gic_common.h>

#include "hyp_macros.h"
#include "hyp.h"
#include "hyp_assym.h"

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


/*
 * Initialize the hypervisor mode with a new exception vector table, translation
 * table and stack.
 *
 * Expecting:
 * x0 - the hypervisor exception vectors
 * x1 - translation tables physical address
 * x2 - stack top virtual address
 * x3 - TCR_EL2 value
 * x4 - SCTLR_EL2 value
 * x5 - VTCR_EL2 value
 */
ENTRY(handle_hyp_init)
	/* Install the new exception vectors */
	msr	vbar_el2, x0
	/* Set the stack top address */
	mov	sp, x2
	/* Use the host VTTBR_EL2 to tell the host and the guests apart */
	mov	x9, #VTTBR_HOST
	msr	vttbr_el2, x9
	/* Load the base address for the translation tables */
	msr	ttbr0_el2, x1
	/* Invalidate the TLB */
	tlbi	alle2
	/* Use the same memory attributes as EL1 */
	mrs	x9, mair_el1
	msr	mair_el2, x9
	/* Configure address translation */
	msr	tcr_el2, x3
	isb
	/* Set the system control register for EL2 */
	msr	sctlr_el2, x4
	/* Set the Stage 2 translation control register */
	msr	vtcr_el2, x5
	/* Return success */
	mov	x0, #0
	/* MMU is up and running */
	eret
END(handle_hyp_init)


	.align 11
	.globl	hyp_vectors
hyp_vectors:
	vempty			/* Synchronous EL2t */
	vempty			/* IRQ EL2t */
	vempty			/* FIQ EL2t */
	vempty			/* Error EL2t */

	vector el2_el2h_sync	/* Synchronous EL2h */
	vector el2_el2h_irq	/* IRQ EL2h */
	vector el2_el2h_fiq	/* FIQ EL2h */
	vector el2_el2h_error	/* Error EL2h */

	vector el2_el1_sync64	/* Synchronous 64-bit EL1 */
	vector el2_el1_irq64	/* IRQ 64-bit EL1 */
	vector el2_el1_fiq64	/* FIQ 64-bit EL1 */
	vector el2_el1_error64	/* Error 64-bit EL1 */

	vempty			/* Synchronous 32-bit EL1 */
	vempty			/* IRQ 32-bit EL1 */
	vempty			/* FIQ 32-bit EL1 */
	vempty			/* Error 32-bit EL1 */


.macro do_world_switch_to_host
	.align 	7
	SAVE_GUEST_REGS()
#ifdef VFP
	/*
	 * Saving the guest VFP registers needs to come after saving the rest of
	 * the registers because the process dirties the regular registers.
	 */
	SAVE_GUEST_VFP_REGS()
	LOAD_HOST_VFP_REGS()
#endif
	LOAD_HOST_REGS()
	SAVE_EXIT_INFO()

	/* Restore host VTTBR */
	mov	x9, #VTTBR_HOST
	msr	vttbr_el2, x9
.endm


.macro handle_el2_excp type
	.align 	7
	/* Save registers before modifying so we can restore them */
	str	x9, [sp, #-16]!

	/* Test if the exception happened when the host was running */
	mrs	x9, vttbr_el2
	cmp	x9, #VTTBR_HOST
	beq	1f

	/* We got the exception while the guest was running */
	ldr	x9, [sp], #16
	do_world_switch_to_host
	b	2f
1:
	/* We got the exception while the host was running */
	ldr	x9, [sp], #16
2:
	mov	x0, \type
	eret
.endm


ENTRY(handle_el2_el2h_sync)
	handle_el2_excp #EXCP_TYPE_EL2_SYNC
END(handle_el2_el2h_sync)

ENTRY(handle_el2_el2h_irq)
	handle_el2_excp #EXCP_TYPE_EL2_IRQ
END(handle_el2_el2h_sync)

ENTRY(handle_el2_el2h_fiq)
	handle_el2_excp #EXCP_TYPE_EL2_FIQ
END(handle_el2_el2h_sync)

ENTRY(handle_el2_el2h_error)
	handle_el2_excp #EXCP_TYPE_EL2_ERROR
END(handle_el2_el2h_sync)


ENTRY(handle_el2_el1_sync64)
	/* Save registers before modifying so we can restore them */
	str	x9, [sp, #-16]!

	/* Check for host hypervisor call */
	mrs	x9, vttbr_el2
	cmp	x9, #VTTBR_HOST
	beq	1f

	/* Restore register */
	ldr	x9, [sp], #16

	/* Guest exception taken to EL2 */
	do_world_switch_to_host
	mov	x0, #EXCP_TYPE_EL1_SYNC
	b	exit

1:
	/* Restore register */
	ldr	x9, [sp], #16

	cmp 	x0, #HYP_GET_VECTOR_TABLE
	beq 	2f
	b	call_function
2:
	/* Return the vector table base address */
	mrs	x0, vbar_el2
exit:
	eret
END(handle_el2_el1_sync64)


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


/*
 * We only trap IRQ, FIQ and SError exceptions when a guest is running. Do a
 * world switch to host to handle these exceptions.
 */


ENTRY(handle_el2_el1_irq64)
	do_world_switch_to_host
	mov	x0, #EXCP_TYPE_EL1_IRQ
	eret
END(handle_el2_el1_irq)

ENTRY(handle_el2_el1_fiq64)
	do_world_switch_to_host
	mov	x0, #EXCP_TYPE_EL1_FIQ
	eret
END(handle_el2_el1_fiq64)

ENTRY(handle_el2_el1_error64)
	do_world_switch_to_host
	mov	x0, #EXCP_TYPE_EL1_ERROR
	eret
END(handle_el2_el1_error64)


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
#ifdef VFP
	SAVE_HOST_VFP_REGS()
	/*
	 * Loading the guest VFP registers needs to come before loading the
 	 * rest of the registers because this process dirties the regular
	 * registers.
 	 */
	LOAD_GUEST_VFP_REGS()
#endif
	LOAD_GUEST_REGS()

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

.macro read_reg name
	mrs	x0, \name
.endm

/*
 * Return the value of the ICH_VTR_EL2 register.
 */
ENTRY(vmm_read_ich_vtr_el2)
	read_reg ich_vtr_el2
	eret
END(vmm_read_ich_vtr_el2)

/*
 * Return the value of the CNTHCTL_EL2 register.
 */
ENTRY(vmm_read_cnthctl_el2)
	read_reg cnthctl_el2
	eret
END(vmm_read_cnthctl_el2)

/*
 * Return the value of the TCR_EL2 register.
 */
ENTRY(vmm_read_tcr_el2)
	read_reg tcr_el2
	eret
END(vmm_read_tcr_el2)



hyp_code_end:
