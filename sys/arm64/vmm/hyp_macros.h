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

#define PUSH_SYSTEM_REG_PAIR(reg0, reg1)		\
	mrs	x1, reg0;				\
	mrs	x2, reg1;				\
	stp	x2, x1, [sp, #-16]!;

#define POP_SYSTEM_REG_PAIR(reg0, reg1)			\
	ldp	x2, x1, [sp], #16;			\
	msr	reg1, x2;				\
	msr	reg0, x1;

#define PUSH_SYSTEM_REG(reg)				\
	mrs 	x1, reg;				\
	str	x1, [sp, #-16]!;

#define POP_SYSTEM_REG(reg)				\
	ldr	x1, [sp], #16;				\
	msr	reg, x1;

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
	str	x30, [sp, #-16]!;			\
							\
	/* Push the system registers */			\
	PUSH_SYSTEM_REG_PAIR(ACTLR_EL1, AMAIR_EL1);	\
	PUSH_SYSTEM_REG_PAIR(ELR_EL1, PAR_EL1);		\
	PUSH_SYSTEM_REG_PAIR(MAIR_EL1, TCR_EL1);	\
	PUSH_SYSTEM_REG_PAIR(TPIDR_EL0, TPIDR_EL1);	\
	PUSH_SYSTEM_REG_PAIR(TPIDRRO_EL0, TTBR0_EL1);	\
	PUSH_SYSTEM_REG_PAIR(TTBR1_EL1, VBAR_EL1);	\
	PUSH_SYSTEM_REG_PAIR(AFSR0_EL1, AFSR1_EL1);	\
	PUSH_SYSTEM_REG_PAIR(CONTEXTIDR_EL1, CPACR_EL1);\
	PUSH_SYSTEM_REG_PAIR(ESR_EL1, FAR_EL1);		\
	PUSH_SYSTEM_REG_PAIR(SCTLR_EL1, SPSR_EL1);	\
	PUSH_SYSTEM_REG_PAIR(ELR_EL2, HCR_EL2);		\
	PUSH_SYSTEM_REG_PAIR(VPIDR_EL2, VMPIDR_EL2);	\
	PUSH_SYSTEM_REG_PAIR(CPTR_EL2, VTTBR_EL2);	\
	PUSH_SYSTEM_REG(VTTBR_EL2);

/*
 * Restore all the host registers before entering the host.
 */
#define LOAD_HOST_REGS()				\
	/* Pop the system registers first */		\
	POP_SYSTEM_REG(VTTBR_EL2);			\
	POP_SYSTEM_REG_PAIR(CPTR_EL2, VTTBR_EL2);	\
	POP_SYSTEM_REG_PAIR(VPIDR_EL2, VMPIDR_EL2);	\
	POP_SYSTEM_REG_PAIR(ELR_EL2, HCR_EL2);		\
	POP_SYSTEM_REG_PAIR(SCTLR_EL1, SPSR_EL1);	\
	POP_SYSTEM_REG_PAIR(ESR_EL1, FAR_EL1);		\
	POP_SYSTEM_REG_PAIR(CONTEXTIDR_EL1, CPACR_EL1);	\
	POP_SYSTEM_REG_PAIR(AFSR0_EL1, AFSR1_EL1);	\
	POP_SYSTEM_REG_PAIR(TTBR1_EL1, VBAR_EL1);	\
	POP_SYSTEM_REG_PAIR(TPIDRRO_EL0, TTBR0_EL1);	\
	POP_SYSTEM_REG_PAIR(TPIDR_EL0, TPIDR_EL1);	\
	POP_SYSTEM_REG_PAIR(MAIR_EL1, TCR_EL1);		\
	POP_SYSTEM_REG_PAIR(ELR_EL1, PAR_EL1);		\
	POP_SYSTEM_REG_PAIR(ACTLR_EL1, AMAIR_EL1);	\
							\
	/* Pop the regular registers */			\
	ldr	x30, [sp], #16;				\
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

#define	SAVE_REG(reg)					\
	mov	x1, #HYPCTX_REGS_##reg;			\
	str	reg, [x0, x1];

#define	LOAD_REG(reg)					\
	mov	x1, #HYPCTX_REGS_##reg;			\
	ldr	reg, [x0, x1];

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

#define LOAD_REG_PAIR(reg0, reg1)			\
	mov	x1, #HYPCTX_REGS_##reg0;		\
	add	x1, x0, x1;				\
	ldp	reg0, reg1, [x1];

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
							\
	/* Pop and save x1 and x2 */			\
	ldp	x1, x2, [sp], #16;			\
	mov	x3, #HYPCTX_REGS_X1;			\
	add	x3, x0, x3;				\
	stp	x1, x2, [x3];				\
	/* Pop and save x0 */				\
	ldr	x1, [sp], #16;				\
	mov	x2, #HYPCTX_REGS_X0;			\
	str	x1, [x0, x2];

/*
 * The STR and LDR instructions take an offset between [-256, 255], but the
 * hypctx register offset can be larger than that. To get around this limitation
 * we use a temporary register to hold the offset.
 */
#define	SAVE_SYSTEM_REG32(reg)				\
	mrs	x1, reg;				\
	mov	x2, #HYPCTX_##reg;			\
	str	w1, [x0, x2];

#define LOAD_SYSTEM_REG32(reg)				\
	mov	x1, #HYPCTX_##reg;			\
	ldr	w2, [x0, x1];				\
	msr	reg, x2;

#define	SAVE_SYSTEM_REG64(reg)				\
	mrs	x1, reg;				\
	mov	x2, #HYPCTX_##reg;			\
	str	x1, [x0, x2];

#define LOAD_SYSTEM_REG64(reg)				\
	mov	x1, #HYPCTX_##reg;			\
	ldr	x2, [x0, x1];				\
	msr	reg, x2;

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
	SAVE_SYSTEM_REG64(ACTLR_EL1);			\
	SAVE_SYSTEM_REG64(AMAIR_EL1);			\
	SAVE_SYSTEM_REG64(ELR_EL1);			\
	SAVE_SYSTEM_REG64(PAR_EL1);			\
	SAVE_SYSTEM_REG64(MAIR_EL1);			\
	SAVE_SYSTEM_REG64(TCR_EL1);			\
	SAVE_SYSTEM_REG64(TPIDR_EL0);			\
	SAVE_SYSTEM_REG64(TPIDR_EL1);			\
	SAVE_SYSTEM_REG64(TPIDRRO_EL0);			\
	SAVE_SYSTEM_REG64(TTBR0_EL1);			\
	SAVE_SYSTEM_REG64(TTBR1_EL1);			\
	SAVE_SYSTEM_REG64(VBAR_EL1);			\
							\
	SAVE_SYSTEM_REG32(AFSR0_EL1);			\
	SAVE_SYSTEM_REG32(AFSR1_EL1);			\
	SAVE_SYSTEM_REG32(CONTEXTIDR_EL1);		\
	SAVE_SYSTEM_REG32(CPACR_EL1);			\
	SAVE_SYSTEM_REG32(ESR_EL1);			\
	SAVE_SYSTEM_REG32(FAR_EL1);			\
	SAVE_SYSTEM_REG32(SCTLR_EL1);			\
	SAVE_SYSTEM_REG32(SPSR_EL1);			\
							\
	SAVE_SYSTEM_REG64(ELR_EL2);			\
	SAVE_SYSTEM_REG64(HCR_EL2);			\
	SAVE_SYSTEM_REG64(VPIDR_EL2);			\
	SAVE_SYSTEM_REG64(VMPIDR_EL2);			\
	SAVE_SYSTEM_REG32(CPTR_EL2);

/*
 * We use x1 as a temporary register to store the hypctx member offset and x0
 * to hold the hypctx address. We load the x0 and x1 guest values, push them on
 * the stack and after all the other registers have been restored we pop and
 * restore them.
 */
#define	LOAD_GUEST_X_REGS()				\
	/* Load and push on the stack guest x0 and x1 */\
	mov	x1, #HYPCTX_REGS_X0;			\
	add	x1, x1, x0;				\
	/* Make x2 = x0 and x3 = x1 */			\
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
							\
	/* Save hypctx address to tpidr_el2 */		\
	msr	tpidr_el2, x0;				\
	/* Pop guest x0 and x1 from the stack */	\
	ldp	x0, x1, [sp], #16;			\

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
	LOAD_SYSTEM_REG64(ACTLR_EL1);			\
	LOAD_SYSTEM_REG64(AMAIR_EL1);			\
	LOAD_SYSTEM_REG64(ELR_EL1);			\
	LOAD_SYSTEM_REG64(PAR_EL1);			\
	LOAD_SYSTEM_REG64(MAIR_EL1);			\
	LOAD_SYSTEM_REG64(TCR_EL1);			\
	LOAD_SYSTEM_REG64(TPIDR_EL0);			\
	LOAD_SYSTEM_REG64(TPIDR_EL1);			\
	LOAD_SYSTEM_REG64(TPIDRRO_EL0);			\
	LOAD_SYSTEM_REG64(TTBR0_EL1);			\
	LOAD_SYSTEM_REG64(TTBR1_EL1);			\
	LOAD_SYSTEM_REG64(VBAR_EL1);			\
							\
	LOAD_SYSTEM_REG32(AFSR0_EL1);			\
	LOAD_SYSTEM_REG32(AFSR1_EL1);			\
	LOAD_SYSTEM_REG32(CONTEXTIDR_EL1);		\
	LOAD_SYSTEM_REG32(CPACR_EL1);			\
	LOAD_SYSTEM_REG32(ESR_EL1);			\
	LOAD_SYSTEM_REG32(FAR_EL1);			\
	LOAD_SYSTEM_REG32(SCTLR_EL1);			\
	LOAD_SYSTEM_REG32(SPSR_EL1);			\
							\
	LOAD_SYSTEM_REG64(ELR_EL2);			\
	LOAD_SYSTEM_REG64(HCR_EL2);			\
	LOAD_SYSTEM_REG64(VPIDR_EL2);			\
	LOAD_SYSTEM_REG64(VMPIDR_EL2);			\
	LOAD_SYSTEM_REG32(CPTR_EL2);			\
							\
	mov	x1, #HYPCTX_HYP;			\
	add	x1, x1, x0;				\
	mov	x2, #HYP_VTTBR;				\
	add	x1, x1, x2;				\
	ldr	x2, [x1];				\
	msr	vttbr_el2, x2;				\
							\
	LOAD_GUEST_X_REGS();

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
