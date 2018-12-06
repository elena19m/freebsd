/*
 * Copyright (C) 2017 Alexandru Elisei <alexandru.elisei@gmail.com>
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

#ifndef _VMM_HYP_MACROS_H_
#define	_VMM_HYP_MACROS_H_


#define PUSH_SYS_REG_PAIR(reg0, reg1)			\
	mrs	x1, reg0;				\
	mrs	x2, reg1;				\
	stp	x2, x1, [sp, #-16]!;


#define PUSH_SYS_REG(reg)				\
	mrs 	x1, reg;				\
	str	x1, [sp, #-16]!;


/*
 * Push all the host registers before entering the guest.
 */
#define SAVE_HOST_REGS()				\
	/* Save the regular registers */		\
	stp	x0, x1, [sp, #-16]!;			\
	stp	x2, x3, [sp, #-16]!;			\
	stp	x4, x5, [sp, #-16]!;			\
	stp	x6, x7, [sp, #-16]!;			\
	stp	x8, x9, [sp, #-16]!;			\
	stp	x10, x11, [sp, #-16]!;			\
	stp	x12, x13, [sp, #-16]!;			\
	stp	x14, x15, [sp, #-16]!;			\
	stp	x16, x17, [sp, #-16]!;			\
	stp	x18, x19, [sp, #-16]!;			\
	stp	x20, x21, [sp, #-16]!;			\
	stp	x22, x23, [sp, #-16]!;			\
	stp	x24, x25, [sp, #-16]!;			\
	stp	x26, x27, [sp, #-16]!;			\
	stp	x28, x29, [sp, #-16]!;			\
	stp	lr, fp, [sp, #-16]!;			\
							\
	/* Push the system registers */			\
	PUSH_SYS_REG_PAIR(SP_EL0, SP_EL1);		\
	PUSH_SYS_REG_PAIR(ACTLR_EL1, AMAIR_EL1);	\
	PUSH_SYS_REG_PAIR(ELR_EL1, PAR_EL1);		\
	PUSH_SYS_REG_PAIR(MAIR_EL1, TCR_EL1);		\
	PUSH_SYS_REG_PAIR(TPIDR_EL0, TPIDRRO_EL0);	\
	PUSH_SYS_REG_PAIR(TPIDR_EL1, TTBR0_EL1);	\
	PUSH_SYS_REG_PAIR(TTBR1_EL1, VBAR_EL1);		\
	PUSH_SYS_REG_PAIR(AFSR0_EL1, AFSR1_EL1);	\
	PUSH_SYS_REG_PAIR(CONTEXTIDR_EL1, CPACR_EL1);	\
	PUSH_SYS_REG_PAIR(ESR_EL1, FAR_EL1);		\
	PUSH_SYS_REG_PAIR(SCTLR_EL1, SPSR_EL1);		\
	PUSH_SYS_REG_PAIR(ELR_EL2, HCR_EL2);		\
	PUSH_SYS_REG_PAIR(VPIDR_EL2, VMPIDR_EL2);	\
	PUSH_SYS_REG_PAIR(CPTR_EL2, SPSR_EL2);		\
	PUSH_SYS_REG_PAIR(ICH_HCR_EL2, ICH_VMCR_EL2);	\
	PUSH_SYS_REG_PAIR(CNTHCTL_EL2, CNTKCTL_EL1);


#define	SAVE_HOST_VFP_REGS()				\
	stp	q0, q1, [sp, #-16 * 2]!;		\
	stp	q2, q3, [sp, #-16 * 2]!;		\
	stp	q4, q5, [sp, #-16 * 2]!;		\
	stp	q6, q7, [sp, #-16 * 2]!;		\
	stp	q8, q9, [sp, #-16 * 2]!;		\
	stp	q10, q11, [sp, #-16 * 2]!;		\
	stp	q12, q13, [sp, #-16 * 2]!;		\
	stp	q14, q15, [sp, #-16 * 2]!;		\
	stp	q16, q17, [sp, #-16 * 2]!;		\
	stp	q18, q19, [sp, #-16 * 2]!;		\
	stp	q20, q21, [sp, #-16 * 2]!;		\
	stp	q22, q23, [sp, #-16 * 2]!;		\
	stp	q24, q25, [sp, #-16 * 2]!;		\
	stp	q26, q27, [sp, #-16 * 2]!;		\
	stp	q28, q29, [sp, #-16 * 2]!;		\
	stp	q30, q31, [sp, #-16 * 2]!;		\
	PUSH_SYS_REG_PAIR(FPCR, FPSR);


#define POP_SYS_REG_PAIR(reg0, reg1)			\
	ldp	x2, x1, [sp], #16;			\
	msr	reg1, x2;				\
	msr	reg0, x1;


#define LOAD_HOST_VFP_REGS()				\
	POP_SYS_REG_PAIR(FPCR, FPSR);			\
	ldp	q30, q31, [sp], #16 * 2;		\
	ldp	q28, q29, [sp], #16 * 2;		\
	ldp	q26, q27, [sp], #16 * 2;		\
	ldp	q24, q25, [sp], #16 * 2;		\
	ldp	q22, q23, [sp], #16 * 2;		\
	ldp	q20, q21, [sp], #16 * 2;		\
	ldp	q18, q19, [sp], #16 * 2;		\
	ldp	q16, q17, [sp], #16 * 2;		\
	ldp	q14, q15, [sp], #16 * 2;		\
	ldp	q12, q13, [sp], #16 * 2;		\
	ldp	q10, q11, [sp], #16 * 2;		\
	ldp	q8, q9, [sp], #16 * 2;			\
	ldp	q6, q7, [sp], #16 * 2;			\
	ldp	q4, q5, [sp], #16 * 2;			\
	ldp	q2, q3, [sp], #16 * 2;			\
	ldp	q0, q1, [sp], #16 * 2;			\


#define POP_SYS_REG(reg)				\
	ldr	x1, [sp], #16;				\
	msr	reg, x1;


/*
 * Restore all the host registers before entering the host.
 */
#define LOAD_HOST_REGS()				\
	/* Pop the system registers first */		\
	POP_SYS_REG_PAIR(CNTHCTL_EL2, CNTKCTL_EL1);	\
	POP_SYS_REG_PAIR(ICH_HCR_EL2, ICH_VMCR_EL2);	\
	POP_SYS_REG_PAIR(CPTR_EL2, SPSR_EL2);		\
	POP_SYS_REG_PAIR(VPIDR_EL2, VMPIDR_EL2);	\
	POP_SYS_REG_PAIR(ELR_EL2, HCR_EL2);		\
	POP_SYS_REG_PAIR(SCTLR_EL1, SPSR_EL1);		\
	POP_SYS_REG_PAIR(ESR_EL1, FAR_EL1);		\
	POP_SYS_REG_PAIR(CONTEXTIDR_EL1, CPACR_EL1);	\
	POP_SYS_REG_PAIR(AFSR0_EL1, AFSR1_EL1);		\
	POP_SYS_REG_PAIR(TTBR1_EL1, VBAR_EL1);		\
	POP_SYS_REG_PAIR(TPIDR_EL1, TTBR0_EL1);		\
	POP_SYS_REG_PAIR(TPIDR_EL0, TPIDRRO_EL0);	\
	POP_SYS_REG_PAIR(MAIR_EL1, TCR_EL1);		\
	POP_SYS_REG_PAIR(ELR_EL1, PAR_EL1);		\
	POP_SYS_REG_PAIR(ACTLR_EL1, AMAIR_EL1);		\
	POP_SYS_REG_PAIR(SP_EL0, SP_EL1);		\
							\
	/* Pop the regular registers */			\
	ldp	lr, fp, [sp], #16;			\
	ldp	x28, x29, [sp], #16;			\
	ldp	x26, x27, [sp], #16;			\
	ldp	x24, x25, [sp], #16;			\
	ldp	x22, x23, [sp], #16;			\
	ldp	x20, x21, [sp], #16;			\
	ldp	x18, x19, [sp], #16;			\
	ldp	x16, x17, [sp], #16;			\
	ldp	x14, x15, [sp], #16;			\
	ldp	x12, x13, [sp], #16;			\
	ldp	x10, x11, [sp], #16;			\
	ldp	x8, x9, [sp], #16;			\
	ldp	x6, x7, [sp], #16;			\
	ldp	x4, x5, [sp], #16;			\
	ldp	x2, x3, [sp], #16;			\
	ldp	x0, x1, [sp], #16;			\


#define	SAVE_ARRAY_REG64(reg, dest, remaining)		\
	cmp	remaining, #0;				\
	beq	9f;					\
	mrs	x7, reg;				\
	str	x7, [dest];				\
	add	dest, dest, #8;				\
	sub	remaining, remaining, #1;


#define	SAVE_LR_REGS()					\
	/* Load the number of ICH_LR_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_ICH_LR_NUM;		\
	ldr	x3, [x0, x2];				\
	/* x1 holds the destination address */		\
	mov	x1, #HYPCTX_VGIC_ICH_LR_EL2;		\
	add	x1, x0, x1;				\
	SAVE_ARRAY_REG64(ich_lr0_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr1_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr2_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr3_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr4_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr5_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr6_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr7_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr8_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr9_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr10_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr11_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr12_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr13_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr14_el2, x1, x3);		\
	SAVE_ARRAY_REG64(ich_lr15_el2, x1, x3);		\
9:;							\
	;


#define	SAVE_ARRAY_REG32(reg, dest, remaining)		\
	cmp	remaining, #0;				\
	beq	9f;					\
	mrs	x7, reg;				\
	str	w7, [dest];				\
	add	dest, dest, #4;				\
	sub	remaining, remaining, #1;


#define	SAVE_AP0R_REGS()				\
	/* Load the number of ICH_AP0R_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_ICH_AP0R_NUM;		\
	ldr	x3, [x0, x2];				\
	/* x1 holds the destination address */		\
	mov	x1, #HYPCTX_VGIC_ICH_AP0R_EL2;		\
	add	x1, x0, x1;				\
	SAVE_ARRAY_REG32(ich_ap0r0_el2, x1, x3);	\
	SAVE_ARRAY_REG32(ich_ap0r1_el2, x1, x3);	\
	SAVE_ARRAY_REG32(ich_ap0r2_el2, x1, x3);	\
	SAVE_ARRAY_REG32(ich_ap0r3_el2, x1, x3);	\
9:;							\
	;


#define	SAVE_AP1R_REGS()				\
	/* Load the number of ICH_AP1R_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_ICH_AP1R_NUM;		\
	ldr	x3, [x0, x2];				\
	/* x1 holds the destination address */		\
	mov	x1, #HYPCTX_VGIC_ICH_AP1R_EL2;		\
	add	x1, x0, x1;				\
	SAVE_ARRAY_REG32(ich_ap1r0_el2, x1, x3);	\
	SAVE_ARRAY_REG32(ich_ap1r1_el2, x1, x3);	\
	SAVE_ARRAY_REG32(ich_ap1r2_el2, x1, x3);	\
	SAVE_ARRAY_REG32(ich_ap1r3_el2, x1, x3);	\
9:;							\
	;


/*
 * The STR and LDR instructions take an offset between [-256, 255], but the
 * hypctx register offset can be larger than that. To get around this limitation
 * we use a temporary register to hold the offset.
 */
#define	SAVE_SYS_REG64(prefix, reg)			\
	mrs	x1, reg;				\
	mov	x2, prefix ##_ ##reg;			\
	str	x1, [x0, x2];


#define	SAVE_SYS_REG32(prefix, reg)			\
	mrs	x1, reg;				\
	mov	x2, prefix ##_ ##reg;			\
	str	w1, [x0, x2];


#define	SAVE_REG(prefix, reg)				\
	mov	x1, prefix ##_ ##reg;			\
	str	reg, [x0, x1];

/*
 * The STP and LDP instructions takes an immediate in the range of [-512, 504]
 * when using the post-indexed addressing mode, but the hypctx register offset
 * can be larger than that. To get around this limitation we compute the address
 * by adding the hypctx base address with the struct member offset.
 *
 * Using STP/LDP to save/load register pairs to the corresponding struct hypctx
 * variables works because the registers are declared as an array and they are
 * stored in contiguous memory addresses.
 */

#define	SAVE_REG_PAIR(prefix, reg0, reg1)		\
	mov	x1, prefix ##_ ##reg0;			\
	add	x1, x0, x1;				\
	stp	reg0, reg1, [x1];


/*
 * We use x0 to load the hypctx address from TPIDR_EL2 and x1 and x2 as
 * temporary registers to compute the hypctx member addresses. To save the guest
 * values at first we push them on the stack, use these temporary registers to
 * save the rest of the registers and at the end we pop the values from the
 * stack and save them.
 */
#define SAVE_GUEST_X_REGS()				\
	/* Push x0 */					\
	str	x0, [sp, #-16]!;			\
	/* Restore hypctx address */			\
	mrs	x0, tpidr_el2;				\
	/* Push x1 and x2 */				\
	stp	x1, x2, [sp, #-16]!;			\
							\
	/* Save the other registers */			\
	SAVE_REG_PAIR(HYPCTX_REGS, X3, X4);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X5, X6);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X7, X8);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X9, X10);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X11, X12);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X13, X14);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X15, X16);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X17, X18);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X19, X20);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X21, X22);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X23, X24);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X25, X26);		\
	SAVE_REG_PAIR(HYPCTX_REGS, X27, X28);		\
	SAVE_REG(HYPCTX_REGS, X29);			\
	SAVE_REG(HYPCTX_REGS, LR);			\
							\
	/* Pop and save x1 and x2 */			\
	ldp	x1, x2, [sp], #16;			\
	mov	x3, #HYPCTX_REGS_X1;			\
	add	x3, x0, x3;				\
	stp	x1, x2, [x3];				\
	/* Pop and save x0 */				\
	ldr	x1, [sp], #16;				\
	mov	x2, #HYPCTX_REGS_X0;			\
	add	x2, x2, x0;				\
	str	x1, [x2];


/*
 * Save all the guest registers. Start by saving the regular registers first
 * because those will be used as temporary registers for accessing the hypctx
 * member addresses.
 *
 * Expecting:
 * TPIDR_EL2 - struct hypctx address
 *
 * After call:
 * x0 - struct hypctx address
 */
#define	SAVE_GUEST_REGS()				\
	SAVE_GUEST_X_REGS();				\
							\
	SAVE_REG(HYPCTX, FP);				\
							\
	SAVE_SYS_REG32(HYPCTX_VTIMER_CPU, CNTKCTL_EL1);	\
	SAVE_SYS_REG64(HYPCTX_VTIMER_CPU, CNTV_CVAL_EL0); \
	SAVE_SYS_REG32(HYPCTX_VTIMER_CPU, CNTV_CTL_EL0);\
							\
	/*						\
 	 * ICH_EISR_EL2, ICH_ELSR_EL2 and ICH_MISR_EL2 are read-only and are \
	 * saved because they are modified by the hardware as part of the \
	 * interrupt virtualization process and we need to inspect them in \
	 * the VGIC driver. \
 	 */						\
	SAVE_SYS_REG32(HYPCTX_VGIC, ICH_EISR_EL2);	\
	SAVE_SYS_REG32(HYPCTX_VGIC, ICH_ELSR_EL2);	\
	SAVE_SYS_REG32(HYPCTX_VGIC, ICH_MISR_EL2);	\
	SAVE_SYS_REG32(HYPCTX_VGIC, ICH_HCR_EL2);	\
	SAVE_SYS_REG32(HYPCTX_VGIC, ICH_VMCR_EL2);	\
							\
	SAVE_LR_REGS();					\
	SAVE_AP0R_REGS();				\
	SAVE_AP1R_REGS();				\
							\
	/* Save the stack pointer. */			\
	mrs	x1, sp_el1;				\
	mov	x2, #HYPCTX_REGS_SP;			\
	str	x1, [x0, x2];				\
							\
	SAVE_SYS_REG64(HYPCTX, ACTLR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, AMAIR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, ELR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, FAR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, MAIR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, PAR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, SP_EL0);			\
	SAVE_SYS_REG64(HYPCTX, TCR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, TPIDR_EL0);		\
	SAVE_SYS_REG64(HYPCTX, TPIDRRO_EL0);		\
	SAVE_SYS_REG64(HYPCTX, TPIDR_EL1);		\
	SAVE_SYS_REG64(HYPCTX, TTBR0_EL1);		\
	SAVE_SYS_REG64(HYPCTX, TTBR1_EL1);		\
	SAVE_SYS_REG64(HYPCTX, VBAR_EL1);		\
							\
	SAVE_SYS_REG32(HYPCTX, AFSR0_EL1);		\
	SAVE_SYS_REG32(HYPCTX, AFSR1_EL1);		\
	SAVE_SYS_REG32(HYPCTX, CONTEXTIDR_EL1);		\
	SAVE_SYS_REG32(HYPCTX, CPACR_EL1);		\
	SAVE_SYS_REG32(HYPCTX, ESR_EL1);		\
	SAVE_SYS_REG32(HYPCTX, SCTLR_EL1);		\
	SAVE_SYS_REG32(HYPCTX, SPSR_EL1);		\
							\
	SAVE_SYS_REG64(HYPCTX, ELR_EL2);		\
	SAVE_SYS_REG64(HYPCTX, HCR_EL2);		\
	SAVE_SYS_REG64(HYPCTX, VPIDR_EL2);		\
	SAVE_SYS_REG64(HYPCTX, VMPIDR_EL2);		\
	SAVE_SYS_REG32(HYPCTX, CPTR_EL2);		\
	SAVE_SYS_REG32(HYPCTX, SPSR_EL2);


#define	SAVE_GUEST_VFP_REGS()				\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q0, Q1);		\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q2, Q3);		\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q4, Q5);		\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q6, Q7);		\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q8, Q9);		\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q10, Q11);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q12, Q13);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q14, Q15);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q16, Q17);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q18, Q19);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q20, Q21);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q22, Q23);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q24, Q25);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q26, Q27);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q28, Q29);	\
	SAVE_REG_PAIR(HYPCTX_VFPSTATE, Q30, Q31);	\
							\
	SAVE_SYS_REG32(HYPCTX_VFPSTATE, FPCR);		\
	SAVE_SYS_REG32(HYPCTX_VFPSTATE, FPSR);


/* See SAVE_SYS_REG */
#define	LOAD_SYS_REG64(prefix, reg)			\
	mov	x1, prefix ##_ ##reg;			\
	ldr	x2, [x0, x1];				\
	msr	reg, x2;


#define	LOAD_SYS_REG32(prefix, reg)			\
	mov	x1, prefix ##_ ##reg;			\
	ldr	w2, [x0, x1];				\
	msr	reg, x2;


/* See SAVE_REG_PAIR */
#define LOAD_REG_PAIR(prefix, reg0, reg1)		\
	mov	x1, prefix ##_ ##reg0;			\
	add	x1, x0, x1;				\
	ldp	reg0, reg1, [x1];


#define	LOAD_GUEST_VFP_REGS()				\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q0, Q1);		\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q2, Q3);		\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q4, Q5);		\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q6, Q7);		\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q8, Q9);		\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q10, Q11);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q12, Q13);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q14, Q15);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q16, Q17);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q18, Q19);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q20, Q21);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q22, Q23);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q24, Q25);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q26, Q27);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q28, Q29);	\
	LOAD_REG_PAIR(HYPCTX_VFPSTATE, Q30, Q31);	\
							\
	LOAD_SYS_REG32(HYPCTX_VFPSTATE, FPCR);		\
	LOAD_SYS_REG32(HYPCTX_VFPSTATE, FPSR);


#define	LOAD_REG(prefix, reg)				\
	mov	x1, prefix ##_ ##reg;			\
	ldr	reg, [x0, x1];


/*
 * We use x1 as a temporary register to store the hypctx member offset and x0
 * to hold the hypctx address. We load the guest x0 and x1 register values in
 * registers x2 and x3, push x2 and x3 on the stack and then we restore x0 and
 * x1.
 */
#define	LOAD_GUEST_X_REGS()				\
	mov	x1, #HYPCTX_REGS_X0;			\
	/* x1 now holds the address of hypctx reg x0 */	\
	add	x1, x1, x0;				\
	/* Make x2 = guest x0 and x3 = guest x1 */	\
	ldp	x2, x3, [x1];				\
	stp	x2, x3, [sp, #-16]!;			\
							\
	/* Load the other registers */			\
	LOAD_REG_PAIR(HYPCTX_REGS, X2, X3);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X4, X5);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X6, X7);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X8, X9);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X10, X11);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X12, X13);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X14, X15);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X16, X17);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X18, X19);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X20, X21);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X22, X23);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X24, X25);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X26, X27);		\
	LOAD_REG_PAIR(HYPCTX_REGS, X28, X29);		\
	LOAD_REG(HYPCTX_REGS, LR);			\
							\
	/* Pop guest x0 and x1 from the stack */	\
	ldp	x0, x1, [sp], #16;			\


#define	LOAD_ARRAY_REG64(reg, src, remaining)		\
	cmp	remaining, #0;				\
	beq	9f;					\
	ldr	x2, [src];				\
	msr	reg, x2;				\
	add	src, src, #8;				\
	sub	remaining, remaining, #1;


#define	LOAD_LR_REGS();					\
	/* Load the number of ICH_LR_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_ICH_LR_NUM;		\
	ldr	x3, [x0, x2];				\
	mov	x1, #HYPCTX_VGIC_ICH_LR_EL2;		\
	/* x1 holds the load address */			\
	add	x1, x0, x1;				\
	LOAD_ARRAY_REG64(ich_lr0_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr1_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr2_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr3_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr4_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr5_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr6_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr7_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr8_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr9_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr10_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr11_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr12_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr13_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr14_el2, x1, x3);		\
	LOAD_ARRAY_REG64(ich_lr15_el2, x1, x3);		\
9:;							\
	;


#define	LOAD_ARRAY_REG32(reg, src, remaining)		\
	cmp	remaining, #0;				\
	beq	9f;					\
	ldr	w2, [src];				\
	msr	reg, x2;				\
	add	src, src, #4;				\
	sub	remaining, remaining, #1;


#define	LOAD_AP0R_REGS();				\
	/* Load the number of ICH_AP0R_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_ICH_AP0R_NUM;		\
	ldr	x3, [x0, x2];				\
	/* x1 holds the load address */			\
	mov	x1, #HYPCTX_VGIC_ICH_AP0R_EL2;		\
	add	x1, x0, x1;				\
	LOAD_ARRAY_REG32(ich_ap0r0_el2, x1, x3);	\
	LOAD_ARRAY_REG32(ich_ap0r1_el2, x1, x3);	\
	LOAD_ARRAY_REG32(ich_ap0r2_el2, x1, x3);	\
	LOAD_ARRAY_REG32(ich_ap0r3_el2, x1, x3);	\
9:;							\
	;


#define	LOAD_AP1R_REGS();				\
	/* Load the number of ICH_AP1R_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_ICH_AP1R_NUM;		\
	ldr	x3, [x0, x2];				\
	/* x1 holds the load address */			\
	mov	x1, #HYPCTX_VGIC_ICH_AP1R_EL2;		\
	add	x1, x0, x1;				\
	LOAD_ARRAY_REG32(ich_ap1r0_el2, x1, x3);	\
	LOAD_ARRAY_REG32(ich_ap1r1_el2, x1, x3);	\
	LOAD_ARRAY_REG32(ich_ap1r2_el2, x1, x3);	\
	LOAD_ARRAY_REG32(ich_ap1r3_el2, x1, x3);	\
9:;							\
	;



#define KTOHYP_REG(reg)					\
	mov	x7, HYP_KVA_MASK;			\
	and	reg, reg, x7;				\
	mov	x7, HYP_KVA_OFFSET;			\
	orr	reg, reg, x7;


/* Load a register from struct hyp *hyp member of hypctx. */
#define	LOAD_HYP_REG(prefix, reg)			\
	/* Compute VA of hyp member in x1 */ 		\
	mov	x1, #HYPCTX_HYP;			\
	add	x1, x1, x0;				\
	/* Get hyp address in x2 */			\
	ldr	x2, [x1];				\
	/* Transform hyp kernel VA into an EL2 VA */	\
	KTOHYP_REG(x2);					\
	/* Get register offset inside struct hyp */	\
	mov	x1, prefix ##_ ##reg;			\
	/* Compute regster address */			\
	add	x2, x2, x1;				\
	/* Load the register */				\
	ldr	x1, [x2];				\
	msr	reg, x1;				\


/*
 * Restore all the guest registers to their original values.
 *
 * Expecting:
 * x0 - struct hypctx address
 *
 * After call:
 * tpidr_el2 - struct hypctx address
 */
#define	LOAD_GUEST_REGS()				\
	LOAD_SYS_REG64(HYPCTX, ACTLR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, AMAIR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, ELR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, FAR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, MAIR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, PAR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, SP_EL0);			\
	LOAD_SYS_REG64(HYPCTX, TCR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, TPIDR_EL0);		\
	LOAD_SYS_REG64(HYPCTX, TPIDRRO_EL0);		\
	LOAD_SYS_REG64(HYPCTX, TPIDR_EL1);		\
	LOAD_SYS_REG64(HYPCTX, TTBR0_EL1);		\
	LOAD_SYS_REG64(HYPCTX, TTBR1_EL1);		\
	LOAD_SYS_REG64(HYPCTX, VBAR_EL1);		\
	LOAD_SYS_REG32(HYPCTX, AFSR0_EL1);		\
	LOAD_SYS_REG32(HYPCTX, AFSR1_EL1);		\
	LOAD_SYS_REG32(HYPCTX, CONTEXTIDR_EL1);		\
	LOAD_SYS_REG32(HYPCTX, CPACR_EL1);		\
	LOAD_SYS_REG32(HYPCTX, ESR_EL1);		\
	LOAD_SYS_REG32(HYPCTX, SCTLR_EL1);		\
	LOAD_SYS_REG32(HYPCTX, SPSR_EL1);		\
							\
	LOAD_SYS_REG64(HYPCTX, ELR_EL2);		\
	LOAD_SYS_REG64(HYPCTX, HCR_EL2);		\
	LOAD_SYS_REG64(HYPCTX, VPIDR_EL2);		\
	LOAD_SYS_REG64(HYPCTX, VMPIDR_EL2);		\
	LOAD_SYS_REG32(HYPCTX, CPTR_EL2);		\
	LOAD_SYS_REG32(HYPCTX, SPSR_EL2);		\
							\
	LOAD_SYS_REG32(HYPCTX_VGIC, ICH_HCR_EL2);	\
	LOAD_SYS_REG32(HYPCTX_VGIC, ICH_VMCR_EL2);	\
							\
	LOAD_SYS_REG32(HYPCTX_VTIMER_CPU, CNTKCTL_EL1);	\
	/* LOAD_SYS_REG64(HYPCTX_VTIMER_CPU, CNTV_CVAL_EL0); */ \
	/* LOAD_SYS_REG32(HYPCTX_VTIMER_CPU, CNTV_CTL_EL0); */ \
							\
	LOAD_REG(HYPCTX, FP);				\
							\
	LOAD_HYP_REG(HYP, VTTBR_EL2);			\
	LOAD_HYP_REG(HYP_VTIMER, CNTHCTL_EL2);		\
							\
	LOAD_LR_REGS();					\
	LOAD_AP0R_REGS();				\
	LOAD_AP1R_REGS();				\
							\
	/* Load the guest EL1 stack pointer */		\
	mov	x1, #HYPCTX_REGS_SP;			\
	add	x1, x1, x0;				\
	ldr	x2, [x1];				\
	msr	sp_el1, x2;				\
							\
	LOAD_GUEST_X_REGS();				\


/*
 * Save exit information
 *
 * Expecting:
 * x0 - struct hypctx address
 */
#define	SAVE_EXIT_INFO()				\
	SAVE_SYS_REG32(HYPCTX_EXIT_INFO, ESR_EL2);	\
	SAVE_SYS_REG64(HYPCTX_EXIT_INFO, FAR_EL2);	\
	SAVE_SYS_REG64(HYPCTX_EXIT_INFO, HPFAR_EL2);	\

#endif /* !_VMM_HYP_MACROS_H_ */
