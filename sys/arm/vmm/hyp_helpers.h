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

#ifndef _VMM_HYP_HELPERS_H_
#define	_VMM_HYP_HELPERS_H_

/* Banked registers */
#define SAVE_GUEST_BANKED_REG(reg)		\
	mrs	r2, reg;			\
	str	r2, [r0, #HYPCTX_##reg]
#define SAVE_GUEST_BANKED_MODE(mode)		\
	SAVE_GUEST_BANKED_REG(SP_##mode);	\
	SAVE_GUEST_BANKED_REG(LR_##mode);	\
	SAVE_GUEST_BANKED_REG(SPSR_##mode)

#define RESTORE_GUEST_BANKED_REG(reg)		\
	ldr	r2, [r0, #HYPCTX_##reg];	\
	msr	reg, r2
#define RESTORE_GUEST_BANKED_MODE(mode)		\
	RESTORE_GUEST_BANKED_REG(SP_##mode);	\
	RESTORE_GUEST_BANKED_REG(LR_##mode);	\
	RESTORE_GUEST_BANKED_REG(SPSR_##mode)

#define	save_guest_regs						\
	/* r0 - address of the hypctx */			\
	add	r2, r0, #HYPCTX_REGS_R(3);			\
	stm	r2, {r3-r12};					\
	pop	{r3-r5};	/* Get r0-r2 from the stack */	\
	add	r2, r0, #HYPCTX_REGS_R(0);			\
	stm	r2, {r3-r5};					\
								\
	str	lr, [r0, #HYPCTX_REGS_LR];			\
	mrs	r2, SP_usr;					\
	str	r2, [r0, #HYPCTX_REGS_SP];			\
								\
	mrs	r2, ELR_hyp;					\
	str	r2, [r0, #HYPCTX_REGS_PC];			\
	mrs	r2, spsr;					\
	str	r2, [r0, #HYPCTX_REGS_CPSR];			\
								\
	SAVE_GUEST_BANKED_MODE(svc);				\
	SAVE_GUEST_BANKED_MODE(abt);				\
	SAVE_GUEST_BANKED_MODE(und);				\
	SAVE_GUEST_BANKED_MODE(irq);				\
	SAVE_GUEST_BANKED_MODE(fiq);				\
	SAVE_GUEST_BANKED_REG(r8_fiq);				\
	SAVE_GUEST_BANKED_REG(r9_fiq);				\
	SAVE_GUEST_BANKED_REG(r10_fiq);				\
	SAVE_GUEST_BANKED_REG(r11_fiq);				\
	SAVE_GUEST_BANKED_REG(r12_fiq)

#define	restore_guest_regs					\
	/* r0 - address of the hypctx */			\
	RESTORE_GUEST_BANKED_MODE(svc);				\
	RESTORE_GUEST_BANKED_MODE(abt);				\
	RESTORE_GUEST_BANKED_MODE(und);				\
	RESTORE_GUEST_BANKED_MODE(irq);				\
	RESTORE_GUEST_BANKED_MODE(fiq);				\
	RESTORE_GUEST_BANKED_REG(r8_fiq);			\
	RESTORE_GUEST_BANKED_REG(r9_fiq);			\
	RESTORE_GUEST_BANKED_REG(r10_fiq);			\
	RESTORE_GUEST_BANKED_REG(r11_fiq);			\
	RESTORE_GUEST_BANKED_REG(r12_fiq);			\
								\
	ldr	r2, [r0, #HYPCTX_REGS_PC];			\
	msr	ELR_hyp, r2;					\
	ldr	r2, [r0, #HYPCTX_REGS_CPSR];			\
	msr	SPSR_cxsf, r2;					\
								\
	ldr	lr, [r0, #HYPCTX_REGS_LR];			\
	ldr	r2, [r0, #HYPCTX_REGS_SP];			\
	msr	SP_usr, r2;					\
								\
	add	r2, r0, #HYPCTX_REGS_R(0);			\
	ldm	r2, {r0-r12}


#define SAVE_HOST_BANKED_REG(reg)		\
	mrs	r2, reg;			\
	push	{r2}
#define SAVE_HOST_BANKED_MODE(mode)		\
	SAVE_HOST_BANKED_REG(SP_##mode);	\
	SAVE_HOST_BANKED_REG(LR_##mode);	\
	SAVE_HOST_BANKED_REG(SPSR_##mode)

#define RESTORE_HOST_BANKED_REG(reg)		\
	pop	{r2};				\
	msr	reg, r2
#define RESTORE_HOST_BANKED_MODE(mode)		\
	RESTORE_HOST_BANKED_REG(SPSR_##mode);	\
	RESTORE_HOST_BANKED_REG(LR_##mode);	\
	RESTORE_HOST_BANKED_REG(SP_##mode)

#define	save_host_regs						\
	/* SPSR was saved when entered HYP mode */		\
	mrs	r2, ELR_hyp;					\
	push	{r2};						\
								\
	push	{r4-r12};					\
	mrs	r2, SP_usr;					\
	push	{r2};						\
	push	{lr};						\
								\
	SAVE_HOST_BANKED_MODE(svc);				\
	SAVE_HOST_BANKED_MODE(abt);				\
	SAVE_HOST_BANKED_MODE(und);				\
	SAVE_HOST_BANKED_MODE(irq);				\
	SAVE_HOST_BANKED_MODE(fiq);				\
	SAVE_HOST_BANKED_REG(r8_fiq);				\
	SAVE_HOST_BANKED_REG(r9_fiq);				\
	SAVE_HOST_BANKED_REG(r10_fiq);				\
	SAVE_HOST_BANKED_REG(r11_fiq);				\
	SAVE_HOST_BANKED_REG(r12_fiq)

#define	restore_host_regs					\
	RESTORE_HOST_BANKED_REG(r12_fiq);			\
	RESTORE_HOST_BANKED_REG(r11_fiq);			\
	RESTORE_HOST_BANKED_REG(r10_fiq);			\
	RESTORE_HOST_BANKED_REG(r9_fiq);			\
	RESTORE_HOST_BANKED_REG(r8_fiq);			\
	RESTORE_HOST_BANKED_MODE(fiq);				\
	RESTORE_HOST_BANKED_MODE(irq);				\
	RESTORE_HOST_BANKED_MODE(und);				\
	RESTORE_HOST_BANKED_MODE(abt);				\
	RESTORE_HOST_BANKED_MODE(svc);				\
								\
	pop	{lr};						\
	pop	{r2};						\
	msr	SP_usr, r2;					\
	pop	{r4-r12};					\
								\
	pop	{r2};						\
	msr	ELR_hyp, r2

#define	load_cp15_regs_batch1					\
	mrc	CP15_SCTLR(r2);			/* SCTLR */	\
	mrc	CP15_CPACR(r3);			/* CPACR */	\
	mrc	CP15_TTBCR(r4);			/* TTBCR */	\
	mrc	CP15_DACR(r5);			/* DACR */	\
	mrrc	CP15_TTBR0_2(r6, r7);		/* TTBR 0 */	\
	mrrc	CP15_TTBR1_2(r8, r9);		/* TTBR 1 */	\
	mrc	CP15_PRRR(r10);			/* PRRR */	\
	mrc	CP15_NMRR(r11);			/* NMRR */	\
	mrc	CP15_CSSELR(r12)		/* CSSELR */

#define	load_cp15_regs_batch2					\
	mrc	CP15_CONTEXTIDR(r2);		/* CID */	\
	mrc	CP15_TPIDRURW(r3);		/* TID_URW */	\
	mrc	CP15_TPIDRURO(r4);		/* TID_URO */	\
	mrc	CP15_TPIDRPRW(r5);		/* TID_PRIV */	\
	mrc	CP15_DFSR(r6);			/* DFSR */	\
	mrc	CP15_IFSR(r7);			/* IFSR */	\
	mrc	CP15_ADFSR(r8);			/* ADFSR */	\
	mrc	CP15_AIFSR(r9);			/* AIFSR */	\
	mrc	CP15_DFAR(r10);			/* DFAR */	\
	mrc	CP15_IFAR(r11);			/* IFAR */	\
	mrc	CP15_VBAR(r12)			/* VBAR */

#define	load_cp15_regs_batch3					\
	mrc	CP15_CNTKCTL(r2);		/* CNTKCTL */	\
	mrrc	CP15_PAR_2(r4, r5);		/* PAR */	\
	mrc	CP15_AMAIR0(r3);		/* AMAIR0 */	\
	mrc	CP15_AMAIR1(r6)			/* AMAIR1 */

#define	store_cp15_regs_batch1					\
	mcr	CP15_SCTLR(r2);			/* SCTLR */	\
	mcr	CP15_CPACR(r3);			/* CPACR */	\
	mcr	CP15_TTBCR(r4);			/* TTBCR */	\
	mcr	CP15_DACR(r5);			/* DACR */	\
	mcrr	CP15_TTBR0_2(r6, r7);		/* TTBR 0 */	\
	mcrr	CP15_TTBR1_2(r8, r9);		/* TTBR 1 */	\
	mcr	CP15_PRRR(r10);			/* PRRR */	\
	mcr	CP15_NMRR(r11);			/* NMRR */	\
	mcr	CP15_CSSELR(r12)		/* CSSELR */

#define	store_cp15_regs_batch2					\
	mcr	CP15_CONTEXTIDR(r2);		/* CID */	\
	mcr	CP15_TPIDRURW(r3);		/* TID_URW */	\
	mcr	CP15_TPIDRURO(r4);		/* TID_URO */	\
	mcr	CP15_TPIDRPRW(r5);		/* TID_PRIV */	\
	mcr	CP15_DFSR(r6);			/* DFSR */	\
	mcr	CP15_IFSR(r7);			/* IFSR */	\
	mcr	CP15_ADFSR(r8);			/* ADFSR */	\
	mcr	CP15_AIFSR(r9);			/* AIFSR */	\
	mcr	CP15_DFAR(r10);			/* DFAR */	\
	mcr	CP15_IFAR(r11);			/* IFAR */	\
	mcr	CP15_VBAR(r12)			/* VBAR */

#define	store_cp15_regs_batch3					\
	mcr	CP15_CNTKCTL(r2);		/* CNTKCTL */	\
	mcrr	CP15_PAR_2(r4, r5);		/* PAR */	\
	mcr	CP15_AMAIR0(r3);		/* AMAIR0 */	\
	mcr	CP15_AMAIR1(r6)			/* AMAIR1 */

#define	store_guest_cp15_regs_batch1				\
	str	r2, [r0, #HYPCTX_CP15_SCTLR];			\
	str	r3, [r0, #HYPCTX_CP15_CPACR];			\
	str	r4, [r0, #HYPCTX_CP15_TTBCR];			\
	str	r5, [r0, #HYPCTX_CP15_DACR];			\
	add	r2, r0, #HYPCTX_CP15_TTBR0;			\
	strd	r6, r7, [r2];					\
	add	r2, r0, #HYPCTX_CP15_TTBR1;			\
	strd	r8, r9, [r2];					\
	str	r10, [r0, #HYPCTX_CP15_PRRR];			\
	str	r11, [r0, #HYPCTX_CP15_NMRR];			\
	str	r12, [r0, #HYPCTX_CP15_CSSELR]

#define	store_guest_cp15_regs_batch2				\
	str	r2, [r0, #HYPCTX_CP15_CID];			\
	str	r3, [r0, #HYPCTX_CP15_TID_URW];			\
	str	r4, [r0, #HYPCTX_CP15_TID_URO];			\
	str	r5, [r0, #HYPCTX_CP15_TID_PRIV];		\
	str	r6, [r0, #HYPCTX_CP15_DFSR];			\
	str	r7, [r0, #HYPCTX_CP15_IFSR];			\
	str	r8, [r0, #HYPCTX_CP15_ADFSR];			\
	str	r9, [r0, #HYPCTX_CP15_AIFSR];			\
	str	r10, [r0, #HYPCTX_CP15_DFAR];			\
	str	r11, [r0, #HYPCTX_CP15_IFAR];			\
	str	r12, [r0, #HYPCTX_CP15_VBAR]

#define	store_guest_cp15_regs_batch3				\
	str	r2, [r0, #HYPCTX_CP15_CNTKCTL];			\
	add	r2, r0, #HYPCTX_CP15_PAR;			\
	strd	r4, r5, [r2];					\
	str	r3, [r0, #HYPCTX_CP15_AMAIR0];			\
	str	r6, [r0, #HYPCTX_CP15_AMAIR1]

#define	load_guest_cp15_regs_batch1				\
	ldr	r2, [r0, #HYPCTX_CP15_SCTLR];			\
	ldr	r3, [r0, #HYPCTX_CP15_CPACR];			\
	ldr	r4, [r0, #HYPCTX_CP15_TTBCR];			\
	ldr	r5, [r0, #HYPCTX_CP15_DACR];			\
	add	r10, r0, #HYPCTX_CP15_TTBR0;			\
	ldrd	r6, r7, [r10];					\
	add	r10, r0, #HYPCTX_CP15_TTBR1;			\
	ldrd	r8, r9, [r10];					\
	ldr	r10, [r0, #HYPCTX_CP15_PRRR];			\
	ldr	r11, [r0, #HYPCTX_CP15_NMRR];			\
	ldr	r12, [r0, #HYPCTX_CP15_CSSELR]

#define	load_guest_cp15_regs_batch2				\
	ldr	r2, [r0, #HYPCTX_CP15_CID];			\
	ldr	r3, [r0, #HYPCTX_CP15_TID_URW];			\
	ldr	r4, [r0, #HYPCTX_CP15_TID_URO];			\
	ldr	r5, [r0, #HYPCTX_CP15_TID_PRIV];		\
	ldr	r6, [r0, #HYPCTX_CP15_DFSR];			\
	ldr	r7, [r0, #HYPCTX_CP15_IFSR];			\
	ldr	r8, [r0, #HYPCTX_CP15_ADFSR];			\
	ldr	r9, [r0, #HYPCTX_CP15_AIFSR];			\
	ldr	r10, [r0, #HYPCTX_CP15_DFAR];			\
	ldr	r11, [r0, #HYPCTX_CP15_IFAR];			\
	ldr	r12, [r0, #HYPCTX_CP15_VBAR]

#define	load_guest_cp15_regs_batch3				\
	ldr	r2, [r0, #HYPCTX_CP15_CNTKCTL];			\
	add	r3, r0, #HYPCTX_CP15_PAR;			\
	ldrd	r4, r5, [r3];					\
	ldr	r3, [r0, #HYPCTX_CP15_AMAIR0];			\
	ldr	r6, [r0, #HYPCTX_CP15_AMAIR1]


#define save_vgic_regs						\
	ldr	r2, [r0, #HYPCTX_VGIC_INT_CTRL];		\
	cmp	r2, #0;						\
	beq	1f;						\
								\
	ldr	r3, [r2, #GICH_HCR];				\
	str	r3, [r0, #HYPCTX_VGIC_HCR];			\
								\
	mov	r3, #0;						\
	str	r3, [r2, #GICH_HCR];				\
								\
	ldr	r3, [r2, #GICH_VMCR];				\
	str	r3, [r0, #HYPCTX_VGIC_VMCR];			\
								\
	ldr	r3, [r2, #GICH_MISR];				\
	str	r3, [r0, #HYPCTX_VGIC_MISR];			\
								\
	ldr	r3, [r2, #GICH_EISR0];				\
	ldr	r4, [r2, #GICH_EISR1];				\
	str	r3, [r2, #HYPCTX_VGIC_EISR];			\
	str	r4, [r2, #(HYPCTX_VGIC_EISR + 4)];		\
								\
	ldr	r3, [r2, #GICH_ELSR0];				\
	ldr	r4, [r2, #GICH_ELSR1];				\
	str	r3, [r0, #HYPCTX_VGIC_ELSR];			\
	str	r4, [r0, #(HYPCTX_VGIC_ELSR + 4)];		\
								\
	ldr	r3, [r2, #GICH_APR];				\
	str 	r3, [r0, #HYPCTX_VGIC_APR];			\
								\
	ldr	r3, [r0, #HYPCTX_VGIC_LR_NUM];			\
	add	r4, r2, #GICH_LR0;				\
	add	r5, r0, #HYPCTX_VGIC_LR;			\
2:	ldr	r6, [r4], #4;					\
	str	r6, [r5], #4;					\
	subs	r3, r3, #1;					\
	bne	2b;						\
1:

#define restore_vgic_regs					\
	ldr	r2, [r0, #HYPCTX_VGIC_INT_CTRL];		\
	cmp	r2, #0;						\
	beq	3f;						\
								\
	ldr	r3, [r0, #HYPCTX_VGIC_HCR];			\
	str	r3, [r2, #GICH_HCR];				\
								\
	ldr	r3, [r0, #HYPCTX_VGIC_VMCR];			\
	str	r3, [r2, #GICH_VMCR];				\
								\
	ldr 	r3, [r0, #HYPCTX_VGIC_APR];			\
	str	r3, [r2, #GICH_APR];				\
								\
	ldr	r3, [r0, #HYPCTX_VGIC_LR_NUM];			\
	add	r4, r2, #GICH_LR0;				\
	add	r5, r0, #HYPCTX_VGIC_LR;			\
4:	ldr	r6, [r5], #4;					\
	str	r6, [r4], #4;					\
	subs	r3, r3, #1;					\
	bne	4b;						\
3:

#define CNTHCTL_PL1PCTEN	(1 << 0)
#define CNTHCTL_PL1PCEN		(1 << 1)

#define save_timer_regs 					\
	ldr	r2, [r0, #HYPCTX_HYP];				\
	add	r2, r2, #HYP_VTTBR;				\
	add	r2, r2, #(HYP_VTIMER_ENABLED - HYP_VTTBR);	\
	ldr	r2, [r2];					\
	cmp	r2, #0;						\
	beq	1f;						\
								\
	mrc	CP15_CNTV_CTL(r2);		/* CNTV_CTL */	\
	str	r2, [r0, #HYPCTX_VTIMER_CPU_CNTV_CTL];		\
	bic	r2, #1;						\
	mcr	CP15_CNTV_CTL(r2);				\
	isb;							\
								\
	mrrc	CP15_CNTV_CVAL(r4, r5);		/* CNTV_CVAL */	\
	add	r2, r0, #HYPCTX_VTIMER_CPU_CNTV_CVAL;		\
	strd	r4, r5, [r2];					\
1:								\
	mrc	CP15_CNTHCTL(r2);				\
	orr	r2, r2, #(CNTHCTL_PL1PCEN | CNTHCTL_PL1PCTEN);	\
	mcr	CP15_CNTHCTL(r2)
	
#define restore_timer_regs					\
	mrc	CP15_CNTHCTL(r2);				\
	orr	r2, r2, #CNTHCTL_PL1PCTEN;			\
	bic	r2, r2, #CNTHCTL_PL1PCEN;			\
	mcr	CP15_CNTHCTL(r2);				\
								\
	ldr	r2, [r0, #HYPCTX_HYP];				\
	add	r3, r2, #HYP_VTTBR;				\
	add	r3, r3, #(HYP_VTIMER_ENABLED - HYP_VTTBR);	\
	ldr	r3, [r3];					\
	cmp	r3, #0;						\
	beq	2f;						\
								\
	add	r4, r2, #HYP_VTTBR;				\
	add	r4, r4, #(HYP_VTIMER_CNTVOFF - HYP_VTTBR);	\
	ldrd	r4, r5, [r4];					\
	mcrr	CP15_CNTVOFF(r4, r5);		/* CNTVOFF */	\
								\
	add	r2, r0, #HYPCTX_VTIMER_CPU_CNTV_CVAL;		\
	ldrd	r4, r5, [r2];					\
	mcrr	CP15_CNTV_CVAL(r4, r5);		/* CNTV_CVAL */	\
	isb;							\
								\
	ldr	r2, [r0, #HYPCTX_VTIMER_CPU_CNTV_CTL];		\
	and	r2, r2, #3;					\
	mcr	CP15_CNTV_CTL(r2);		/* CNTV_CTL */	\
2:

#ifdef VFP

#define vfp_store							\
	vmrs	r4, fpexc;						\
	orr		r7, r4, #VFPEXC_EN;				\
	vmsr	fpexc, r7;						\
	vmrs	r3, fpscr;						\
											\
	tst	r4, #VFPEXC_EX;						\
	beq	1f;									\
											\
	vmrs	r5, fpinst;						\
	tst	r4, #VFPEXC_FP2V;					\
	vmrsne	r6, fpinst2;					\
											\
	bic	r7, r4, #VFPEXC_EX;					\
	vmsr	fpexc, r7;						\
1:											\
	vstmia	r2!, {d0-d15};					\
	/* TODO Check if d16-d32 exist and need to be saved */	\
	add	r2, r2, #128;						\
	stm	r2, {r3-r6}

#define vfp_restore							\
	vldmia	r2!, {d0-d15};					\
	/* TODO Same as above */				\
	add r2, r2, #128;						\
	ldm	r2, {r3-r6};						\
											\
	vmsr	fpscr, r3;						\
											\
	tst	r4, #VFPEXC_EX;						\
	beq 2f;									\
											\
	vmsr	fpinst, r5;						\
	tst	r4, #VFPEXC_FP2V;					\
	vmsrne	fpinst2, r6;					\
2:											\
	vmsr	fpexc, r4

#define vfp_switch_to_guest					\
	add	r2, r0, #HYPCTX_HOST_VFP_STATE;			\
	vfp_store;				\
	add	r2, r0, #HYPCTX_GUEST_VFP_STATE;	\
	vfp_restore

#define vfp_switch_to_host					\
	add	r2, r0, #HYPCTX_GUEST_VFP_STATE;			\
	vfp_store;				\
	add	r2, r0, #HYPCTX_HOST_VFP_STATE;	\
	vfp_restore

#endif //VFP

#endif
