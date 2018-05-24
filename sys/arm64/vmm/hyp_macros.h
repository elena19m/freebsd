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
	str	lr, [sp, #-16]!;			\
							\
	/* Push the system registers */			\
	PUSH_SYS_REG(SP_EL1);				\
	PUSH_SYS_REG_PAIR(ACTLR_EL1, AMAIR_EL1);	\
	PUSH_SYS_REG_PAIR(ELR_EL1, PAR_EL1);		\
	PUSH_SYS_REG_PAIR(MAIR_EL1, TCR_EL1);		\
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
	PUSH_SYS_REG_PAIR(CNTV_CTL_EL0, CNTV_CVAL_EL0);	\
	PUSH_SYS_REG(CNTHCTL_EL2);


#define POP_SYS_REG_PAIR(reg0, reg1)			\
	ldp	x2, x1, [sp], #16;			\
	msr	reg1, x2;				\
	msr	reg0, x1;


#define POP_SYS_REG(reg)				\
	ldr	x1, [sp], #16;				\
	msr	reg, x1;


/*
 * Restore all the host registers before entering the host.
 */
#define LOAD_HOST_REGS()				\
	/* Pop the system registers first */		\
	POP_SYS_REG(CNTHCTL_EL2);			\
	POP_SYS_REG_PAIR(CNTV_CTL_EL0, CNTV_CVAL_EL0);	\
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
	POP_SYS_REG_PAIR(MAIR_EL1, TCR_EL1);		\
	POP_SYS_REG_PAIR(ELR_EL1, PAR_EL1);		\
	POP_SYS_REG_PAIR(ACTLR_EL1, AMAIR_EL1);		\
	POP_SYS_REG(SP_EL1);				\
							\
	/* Pop the regular registers */			\
	ldr	lr, [sp], #16;				\
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


#define	SAVE_LR_REG(lr, to, remaining)			\
	cmp	remaining, #0;				\
	beq	9f;					\
	mrs	x7, lr;					\
	str	x7, [to];				\
	add	to, to, #8;				\
	sub	remaining, remaining, #1;


#define	SAVE_LR_REGS()					\
	/* Load the number of ICH_LR_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_LR_NUM;		\
	ldr	x3, [x0, x2];				\
	/* x1 holds the destination address */		\
	mov	x1, #HYPCTX_VGIC_ICH_LR_EL2;		\
	add	x1, x0, x1;				\
	SAVE_LR_REG(ich_lr0_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr1_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr2_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr3_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr4_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr5_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr6_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr7_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr8_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr9_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr10_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr11_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr12_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr13_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr14_el2, x1, x3);		\
	SAVE_LR_REG(ich_lr15_el2, x1, x3);		\
9:;							\
	;


/*
 * The STR and LDR instructions take an offset between [-256, 255], but the
 * hypctx register offset can be larger than that. To get around this limitation
 * we use a temporary register to hold the offset.
 */
#define	SAVE_SYS_REG(prefix, reg)			\
	mrs	x1, reg;				\
	mov	x2, prefix ##_ ##reg;			\
	str	x1, [x0, x2];


#define	SAVE_REG(reg)					\
	mov	x1, #HYPCTX_REGS_##reg;			\
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

#define	SAVE_REG_PAIR(reg0, reg1)			\
	mov	x1, #HYPCTX_REGS_##reg0;		\
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
	SAVE_REG_PAIR(X3, X4);				\
	SAVE_REG_PAIR(X5, X6);				\
	SAVE_REG_PAIR(X7, X8);				\
	SAVE_REG_PAIR(X9, X10);				\
	SAVE_REG_PAIR(X11, X12);			\
	SAVE_REG_PAIR(X13, X14);			\
	SAVE_REG_PAIR(X15, X16);			\
	SAVE_REG_PAIR(X17, X18);			\
	SAVE_REG_PAIR(X19, X20);			\
	SAVE_REG_PAIR(X21, X22);			\
	SAVE_REG_PAIR(X23, X24);			\
	SAVE_REG_PAIR(X25, X26);			\
	SAVE_REG_PAIR(X27, X28);			\
	SAVE_REG(X29);					\
	SAVE_REG(LR);					\
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
	/*						\
 	 * ICH_EISR_EL2, ICH_ELSR_EL2 and ICH_MISR_EL2 are read-only and are \
	 * saved because they are modified by the hardware as part of the \
	 * interrupt virtualization process and we need to inspect them in \
	 * the VGIC driver. \
 	 */						\
	SAVE_SYS_REG(HYPCTX_VGIC, ICH_EISR_EL2);	\
	SAVE_SYS_REG(HYPCTX_VGIC, ICH_ELSR_EL2);	\
	SAVE_SYS_REG(HYPCTX_VGIC, ICH_MISR_EL2);	\
	SAVE_SYS_REG(HYPCTX_VGIC, ICH_HCR_EL2);		\
	SAVE_SYS_REG(HYPCTX_VGIC, ICH_VMCR_EL2);	\
							\
	SAVE_LR_REGS();					\
							\
	/* Save the stack pointer. */			\
	mrs	x1, sp_el1;				\
	mov	x2, #HYPCTX_REGS_SP;			\
	str	x1, [x0, x2];				\
							\
	SAVE_SYS_REG(HYPCTX, ACTLR_EL1);		\
	SAVE_SYS_REG(HYPCTX, AMAIR_EL1);		\
	SAVE_SYS_REG(HYPCTX, ELR_EL1);			\
	SAVE_SYS_REG(HYPCTX, FAR_EL1);			\
	SAVE_SYS_REG(HYPCTX, MAIR_EL1);			\
	SAVE_SYS_REG(HYPCTX, PAR_EL1);			\
	SAVE_SYS_REG(HYPCTX, TCR_EL1);			\
	SAVE_SYS_REG(HYPCTX, TPIDR_EL1);		\
	SAVE_SYS_REG(HYPCTX, TTBR0_EL1);		\
	SAVE_SYS_REG(HYPCTX, TTBR1_EL1);		\
	SAVE_SYS_REG(HYPCTX, VBAR_EL1);			\
							\
	SAVE_SYS_REG(HYPCTX, AFSR0_EL1);		\
	SAVE_SYS_REG(HYPCTX, AFSR1_EL1);		\
	SAVE_SYS_REG(HYPCTX, CONTEXTIDR_EL1);		\
	SAVE_SYS_REG(HYPCTX, CPACR_EL1);		\
	SAVE_SYS_REG(HYPCTX, ESR_EL1);			\
	SAVE_SYS_REG(HYPCTX, SCTLR_EL1);		\
	SAVE_SYS_REG(HYPCTX, SPSR_EL1);			\
							\
	SAVE_SYS_REG(HYPCTX, ELR_EL2);			\
	SAVE_SYS_REG(HYPCTX, HCR_EL2);			\
	SAVE_SYS_REG(HYPCTX, VPIDR_EL2);		\
	SAVE_SYS_REG(HYPCTX, VMPIDR_EL2);		\
	SAVE_SYS_REG(HYPCTX, CPTR_EL2);			\
	SAVE_SYS_REG(HYPCTX, SPSR_EL2);


#define	LOAD_REG(reg)					\
	mov	x1, #HYPCTX_REGS_##reg;			\
	ldr	reg, [x0, x1];


/* See SAVE_REG_PAIR */
#define LOAD_REG_PAIR(reg0, reg1)			\
	mov	x1, #HYPCTX_REGS_##reg0;		\
	add	x1, x0, x1;				\
	ldp	reg0, reg1, [x1];


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
	LOAD_REG_PAIR(X2, X3);				\
	LOAD_REG_PAIR(X4, X5);				\
	LOAD_REG_PAIR(X6, X7);				\
	LOAD_REG_PAIR(X8, X9);				\
	LOAD_REG_PAIR(X10, X11);			\
	LOAD_REG_PAIR(X12, X13);			\
	LOAD_REG_PAIR(X14, X15);			\
	LOAD_REG_PAIR(X16, X17);			\
	LOAD_REG_PAIR(X18, X19);			\
	LOAD_REG_PAIR(X20, X21);			\
	LOAD_REG_PAIR(X22, X23);			\
	LOAD_REG_PAIR(X24, X25);			\
	LOAD_REG_PAIR(X26, X27);			\
	LOAD_REG_PAIR(X28, X29);			\
	LOAD_REG(LR);					\
							\
	/* Pop guest x0 and x1 from the stack */	\
	ldp	x0, x1, [sp], #16;			\


/* See SAVE_SYS_REG */
#define	LOAD_SYS_REG(prefix, reg)			\
	mov	x1, prefix ##_ ##reg;			\
	ldr	x2, [x0, x1];				\
	msr	reg, x2;


#define	LOAD_LR_REG(lr, from, remaining)		\
	cmp	remaining, #0;				\
	beq	9f;					\
	ldr	x2, [from];				\
	msr	lr, x2;					\
	add	from, from, #8;				\
	sub	remaining, remaining, #1;


#define	LOAD_LR_REGS();					\
	/* Load the number of ICH_LR_EL2 regs from memory */ \
	mov	x2, #HYPCTX_VGIC_LR_NUM;		\
	ldr	x3, [x0, x2];				\
	mov	x1, #HYPCTX_VGIC_ICH_LR_EL2;		\
	/* x1 holds the load address */			\
	add	x1, x0, x1;				\
	LOAD_LR_REG(ich_lr0_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr1_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr2_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr3_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr4_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr5_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr6_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr7_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr8_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr9_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr10_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr11_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr12_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr13_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr14_el2, x1, x3);		\
	LOAD_LR_REG(ich_lr15_el2, x1, x3);		\
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
	LOAD_SYS_REG(HYPCTX, ACTLR_EL1);		\
	LOAD_SYS_REG(HYPCTX, AMAIR_EL1);		\
	LOAD_SYS_REG(HYPCTX, ELR_EL1);			\
	LOAD_SYS_REG(HYPCTX, FAR_EL1);			\
	LOAD_SYS_REG(HYPCTX, MAIR_EL1);			\
	LOAD_SYS_REG(HYPCTX, PAR_EL1);			\
	LOAD_SYS_REG(HYPCTX, TCR_EL1);			\
	LOAD_SYS_REG(HYPCTX, TPIDR_EL1);		\
	LOAD_SYS_REG(HYPCTX, TTBR0_EL1);		\
	LOAD_SYS_REG(HYPCTX, TTBR1_EL1);		\
	LOAD_SYS_REG(HYPCTX, VBAR_EL1);			\
	LOAD_SYS_REG(HYPCTX, AFSR0_EL1);		\
	LOAD_SYS_REG(HYPCTX, AFSR1_EL1);		\
	LOAD_SYS_REG(HYPCTX, CONTEXTIDR_EL1);		\
	LOAD_SYS_REG(HYPCTX, CPACR_EL1);		\
	LOAD_SYS_REG(HYPCTX, ESR_EL1);			\
	LOAD_SYS_REG(HYPCTX, SCTLR_EL1);		\
	LOAD_SYS_REG(HYPCTX, SPSR_EL1);			\
							\
	LOAD_SYS_REG(HYPCTX, ELR_EL2);			\
	LOAD_SYS_REG(HYPCTX, HCR_EL2);			\
	LOAD_SYS_REG(HYPCTX, VPIDR_EL2);		\
	LOAD_SYS_REG(HYPCTX, VMPIDR_EL2);		\
	LOAD_SYS_REG(HYPCTX, CPTR_EL2);			\
	LOAD_SYS_REG(HYPCTX, SPSR_EL2);			\
							\
	LOAD_SYS_REG(HYPCTX_VGIC, ICH_HCR_EL2);		\
	LOAD_SYS_REG(HYPCTX_VGIC, ICH_VMCR_EL2);	\
							\
	LOAD_HYP_REG(HYP, VTTBR_EL2);			\
	LOAD_HYP_REG(HYP_VTIMER, CNTHCTL_EL2);		\
							\
	LOAD_LR_REGS();					\
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
	mrs	x1, esr_el2;				\
	mov	x2, #HYPCTX_EXIT_INFO_ESR_EL2;		\
	str	w1, [x0, x2];				\
							\
	mrs	x1, far_el2;				\
	mov	x2, #HYPCTX_EXIT_INFO_FAR_EL2;		\
	str	w1, [x0, x2];				\
							\
	mrs	x1, hpfar_el2;				\
	mov	x2, #HYPCTX_EXIT_INFO_HPFAR_EL2;	\
	str	w1, [x0, x2];				\

#endif /* !_VMM_HYP_MACROS_H_ */
