/*
 * Copyright (C) 2015 Mihai Carabas <mihai.carabas@gmail.com>
 * All rights reserved.
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
#include <machine/asmacros.h>
#include <machine/armreg.h>
#include <machine/sysreg.h>
#include <machine/vfp.h>

#include <arm/arm/gic_common.h>

#include "hyp.h"
#include "hyp_assym.h"
#include "hyp_helpers.h"

#ifdef VFP
	.fpu vfp
#endif

.arch_extension virt

.text
	.globl	hyp_code_start
	.globl	hyp_code_end
	.globl	hyp_vector
	.globl	init_hyp_vector

	.p2align 12
hyp_code_start:
ENTRY(vmm_call_hyp)
	hvc	#0
	bx	lr
END(vmm_call_hyp)
/*
 * int hyp_enter_guest(struct *hyp_vmxctx);
 * - r0 pointer to the struct hyp_vmxctx
 */
ENTRY(hyp_enter_guest)
	mcr	CP15_HTPIDR(r0)		@ Store hyp_vmxctx into HTPIDR
	save_host_regs

	restore_vgic_regs
	restore_timer_regs

	/* Save HOST CP15 registers */
	load_cp15_regs_batch1				@ Load in r2-r12 CP15 regs
	push {r2-r12}
	load_cp15_regs_batch2				@ Load in r2-r12 CP15 regs
	push {r2-r12}
	load_cp15_regs_batch3				@ Load in r2-r6 CP15 regs
	push {r2-r6}
	
	/* Load GUEST CP15 registers */
	load_guest_cp15_regs_batch1
	store_cp15_regs_batch1
	load_guest_cp15_regs_batch2
	store_cp15_regs_batch2
	load_guest_cp15_regs_batch3
	store_cp15_regs_batch3
	
	/* Enable stage-2 MMU, trap interrupts */
	ldr	r1, [r0, #HYPCTX_HCR]
	mcr	CP15_HCR(r1)

	/* Set MIDR and MPIDR for the Guest */
	ldr	r1, [r0, #HYPCTX_MIDR]
	mcr	CP15_VPIDR(r1)
	ldr	r1, [r0, #HYPCTX_MPIDR]
	mcr	CP15_VMPIDR(r1)

	/* Set VTTBR for stage-2 translation */
	ldr	r1, [r0, #HYPCTX_HYP]
	add	r1, r1, #HYP_VTTBR
	ldrd	r2, r3, [r1]
	mcrr	CP15_VTTBR(r2, r3)

#ifdef VFP
	fmrx	r2, fpexc
	push	{r2}
	orr	r2, r2, #(VFPEXC_EN)
	fmxr	fpexc, r2
#endif

	/* Trap access to the CP10/CP11 [vfp/simd] */
	mrc     CP15_HCPTR(r1)
	ldr	r2, =(HCPTR_TTA | HCPTR_TCP(10) | HCPTR_TCP(11))
	orr     r1, r1, r2
	mcr     CP15_HCPTR(r1)

	restore_guest_regs

	eret
hyp_exit_guest:
	/* 
	 * r0 - hypctx pointer
	 * r1 - exception code
	 * guest r0-r2 saved on stack when trapping in HYP mode 
	 */
	
	/* Save exit status registers */
	mrc	CP15_HSR(r2)	@ Load HSR
	str	r2, [r0, #HYPCTX_EXIT_INFO_HSR]
	mrc	CP15_HIFAR(r2)	@ Load HIFAR
	str	r2, [r0, #HYPCTX_EXIT_INFO_HIFAR]
	mrc	CP15_HDFAR(r2)	@ Load HDFAR
	str	r2, [r0, #HYPCTX_EXIT_INFO_HDFAR]
	mrc	CP15_HPFAR(r2)	@ Load HPFAR
	str	r2, [r0, #HYPCTX_EXIT_INFO_HPFAR]

	save_guest_regs
	
	/* Disable trap access to the CP10/CP11 [vfp/simd] */
	mrc     CP15_HCPTR(r2)
	ldr	r3, =(HCPTR_TTA | HCPTR_TCP(10) | HCPTR_TCP(11))
	bic     r3, r2, r3
	mcr     CP15_HCPTR(r3)

#ifdef VFP
	tst	r2, #(HCPTR_TCP(10) | HCPTR_TCP(11))
	bne	after_restore

	vfp_switch_to_host

after_restore:
	pop	{r2}
	fmxr	fpexc, r2
#endif

	/* Set VTTBR to 0 (VMID to 0) */
	mov	r2, #0
	mov	r3, #0
	mcrr    CP15_VTTBR(r2, r3)

	/* Set MIDR and MPIDR at hardware values */
	mrc     CP15_MIDR(r2)
	mcr     CP15_VPIDR(r2)
	mrc     CP15_MPIDR(r2)
	mcr     CP15_VMPIDR(r2)

	/* Disable all traps - HCR */
	mov	r2, #0
	mcr	CP15_HCR(r2)


	/* Save guest CP15 registers */
	load_cp15_regs_batch1
	store_guest_cp15_regs_batch1
	load_cp15_regs_batch2
	store_guest_cp15_regs_batch2
	load_cp15_regs_batch3
	store_guest_cp15_regs_batch3

	/* Load HOST CP15 registers in reverse order from the stack */
	pop {r2-r6}
	store_cp15_regs_batch3			@ Load in r2-r6 CP15 regs
	pop {r2-r12}
	store_cp15_regs_batch2			@ Load in r2-r12 CP15 regs
	pop {r2-r12}
	store_cp15_regs_batch1			@ Load in r2-r12 CP15 regs

	save_timer_regs
	save_vgic_regs

	restore_host_regs

	mov	r0, r1				@ r0 must hold the return value
	bx	lr				@ go back to the host ("Returned from function" comment)
END(hyp_enter_guest)

/* 
 * void vmm_stub_install(void *hypervisor_stub_vect);
 * - r0 - the pointer to the stub vector
 */
ENTRY(vmm_stub_install)

	/* Install hypervisor stub vectors. */
	mcr	CP15_HVBAR(r0)		@ set HVBAR

	/* Disable all the traps in the hypervisor. */
	mov	r0, #0
	mcr	CP15_HCR(r0)		@ HCR
	mcr	CP15_HCPTR(r0)		@ HCPTR
	mcr	CP15_HSTR(r0)		@ HSTR
	mcr	CP15_HSCTLR(r0)		@ HSCTLR

	/* Don't disable access to perf-mon from PL0,1 */ 
	mrc	CP15_HDCR(r0)		@ HDCR
	and	r0, #0x1f		@ Preserve HPMN
	mcr	CP15_HDCR(r0)		@ HDCR

	eret
END(vmm_stub_install)

ENTRY(vmm_set_get_hvbar)
	/*
	 * If the first parameter is -1 than return the
	 * exception vector (HVBAR), otherwise set it to
	 * the value of it.
	 */
	cmp	r0, #-1
	mrceq	CP15_HVBAR(r0)		@ get HVBAR
	mcrne	CP15_HVBAR(r0)		@ set HVBAR
	bx	lr
END(vmm_set_get_hvbar)

	.align 5
init_hyp_vector:
	.word 0			/* Reset */
	.word 0			/* undev */
	.word 0			/* SVC */
	.word 0			/* PABT */
	.word 0			/* DABT */
	b	hyp_init_hvc	/* HYP-Mode */
	.word 0			/* FIQ */
	.word 0			/* IRQ */

hyp_init_hvc:
	mcr	CP15_HVBAR(r0)		@ set HVBAR to the new vector
	mov     sp, r1			@ set SP. r1 contains the stack pointer
	mcrr	CP15_HTTBR(r2, r3)	@ set the HTTBR (r2 is the low word, r3 is the high word)
	isb

	@ Set HTCR.T0SZ=0 so x=5 (ARM man: B4.1.76)
	@ Set HTCR.ORGN0/.IRGN0/.SH0 to 0 to disable cacheability and shareability
	@ HTCR_MASK contains all the above bits
	mrc	CP15_HTCR(r0)		@ HTCR
	ldr	r1,=HTCR_MASK
	bic     r0, r0, r1
	mcr	CP15_HTCR(r0)		@ HTCR

	@ VTCR for supporting only 32 bit IPA [see VMM_VTCR_T0SZ in hyp.h]
	ldr     r0, =(VTCR_RES | VTCR_SL_L1 | VMM_VTCR_T0SZ | VMM_VTCR_S)
	mcr     CP15_VTCR(r0)		@ VTCR

	@ Set the HMAIR0/1 (same as MAIR0/1) registers for AttrIndx[2:0]
	ldr	r0, =HMAIR0
	mcr     CP15_MAIR0(r0)
	ldr	r0, =HMAIR1
	mcr     CP15_MAIR1(r0)

	@ Flush the TLB entries from Hyp-Mode
	mcr	CP15_TLBIALLH(r0)	@ TLBIALLH
	dsb	ish

	mrc	CP15_HSCTLR(r0)		@ Read current HSCTLR
	ldr	r2, =HSCTLR_MASK
	bic     r0, r0, r2

	mrc	CP15_SCTLR(r1)		@ Read the current SCTLR
	ldr     r2, =(HSCTLR_EE | HSCTLR_FI | HSCTLR_I | HSCTLR_C)
	and     r1, r1, r2
	ldr     r2, =(HSCTLR_M | HSCTLR_A)
	orr     r1, r1, r2
	orr     r0, r0, r1
	isb
	mcr     CP15_HSCTLR(r0)		@ Set the new HSCTLR
	eret

	.align 5
hyp_vector:
	b	hyp_reset	/* Reset */
	b	hyp_undef	/* undef */
	b	hyp_svc		/* SVC */
	b	hyp_pabt	/* PABT */
	b	hyp_dabt	/* DABT */
	b	hyp_hvc		/* HYP-Mode */
	b	hyp_fiq		/* FIQ */
	b	hyp_irq		/* IRQ */
	.align

hyp_reset:
	b loop

	.align
hyp_undef:
	ldr r0, =und_die_str
	mov r1, #EXCEPTION_UNDEF
	bl handle_bad_exception
und_die_str:
	.ascii  "unexpected undefined exception in Hyp mode at: %#08x\n"

	.align
hyp_svc:
	ldr r0, =svc_die_str
	mov r1, #EXCEPTION_SVC
	bl handle_bad_exception
svc_die_str:
	.ascii  "unexpected HVC/SVC trap in Hyp mode at: %#08x\n"

	.align
hyp_pabt:
	ldr r0, =pabt_die_str
	mov r1, #EXCEPTION_PABT
	bl handle_bad_exception
pabt_die_str:
	.ascii  "unexpected prefetch abort in Hyp mode at: %#08x\n"

	.align
hyp_dabt:
	ldr r0, =dabt_die_str
	mov r1, #EXCEPTION_DABT
	bl handle_bad_exception
dabt_die_str:
	.ascii  "unexpected data abort in Hyp mode at: %#08x\n"

	.align
hyp_hvc:
	push	{r0, r1, r2}		@ Save registers in order to use them
	mrc	CP15_HSR(r1)		@ Check HSR for explicit HVC call
	lsr	r0, r1, #HSR_EC_SHIFT

#ifdef VFP
	cmp r0, #HSR_EC_HCPTR_CP0_CP13
	bne hyp_not_vfp
	mrc	CP15_HCPTR(r1)
	ldr	r2, =(HCPTR_TCP(10) | HCPTR_TCP(11))
	bic	r1, r1, r2
	mcr	CP15_HCPTR(r1)
	mrc	CP15_HTPIDR(r0)
	push {r3-r7}
	vfp_switch_to_guest
	pop {r3-r7}
	pop {r0, r1, r2}
	eret
	
hyp_not_vfp:
#endif

	cmp	r0, #HSR_EC_HVC
	bne	guest_trap

	mrrc	CP15_VTTBR(r0, r1)	@ Check VMID=0 to be sure that host called HVC
	lsr	r1, r1, #VTTBR_VMID_SHIFT
	and	r1, r1, #VTTBR_VMID_MASK
	cmp	r1, #0
	bne	guest_trap

host_called_hyp:
	pop	{r0, r1, r2}		@ Restore registers
	push	{lr}
	mrs	lr, SPSR
	push	{lr}

	/* Build param list for the function pointer in r0 */
	mov	lr, r0
	mov	r0, r1
	mov	r1, r2
	mov	r2, r3
	blx	lr
	/* Returned from function */
	pop	{lr}
	msr	SPSR_csxf, lr
	pop	{lr}
	eret

guest_trap:
	/* Load hypctx in r0 from HTPIDR */
	mrc	CP15_HTPIDR(r0)
	mov	r1, #EXCEPTION_HVC

	b	hyp_exit_guest

	.align
hyp_fiq:
	push	{r0, r1, r2}		@ Save registers in order to use them
	/* Load hypctx pointer to r0 */
	mrc	CP15_HTPIDR(r0)
	mov	r1, #EXCEPTION_FIQ
	b	hyp_exit_guest

	.align
hyp_irq:
	push	{r0, r1, r2}		@ Save registers in order to use them
	/* Load hypctx pointer to r0 */
	mrc	CP15_HTPIDR(r0)
	mov	r1, #EXCEPTION_IRQ
	b	hyp_exit_guest
	.align

ENTRY(handle_bad_exception)
	/* We have in r0 pointer to the panic string and in r1 the exception code */
	mrrc	CP15_VTTBR(r3, r2)	@ Read VTTBR
	lsr	r2, r2, #16
	ands	r2, r2, #0xff
	bne	guest_bad_exception

	mrs	r2, cpsr
	bic	r2, r2, #PSR_MODE
	orr	r2, r2, #PSR_SVC32_MODE
	msr	spsr_cxsf, r2
	mrs	r1, ELR_hyp		@ We don't need anymore the exception code, we store 2nd param for panic */
	ldr	r3, =panic
	msr	ELR_hyp, r3
	eret

guest_bad_exception:
	push	{r0, r1, r2}		@ Emulate a push to the stack to respect hyp_exit_guest restore convention
	/* Load hypctx pointer to r0 */
	mrc	CP15_HTPIDR(r0)
	b	hyp_exit_guest
END(handle_bad_exception)

loop:
	b loop

hyp_code_end:
