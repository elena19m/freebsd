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

/*
 * The STR instruction takes an offset between [-256, 255], but the hypctx
 * register offset can be larger than that.
 *
 * Use a temporary register to hold the offset.
 */
#define	SAVE_SYSTEM_REG32(reg)				\
	mrs	x9, reg;				\
	mov	x10, #HYPCTX_##reg;			\
	str	w9, [x0, x10];

#define LOAD_SYSTEM_REG32(reg)				\
	mov	x9, #HYPCTX_##reg;			\
	ldr	w10, [x0, x9];				\
	msr	reg, x10;

#define	SAVE_SYSTEM_REG64(reg)				\
	mrs	x9, reg;				\
	mov	x10, #HYPCTX_##reg;			\
	str	x9, [x0, x10];

#define LOAD_SYSTEM_REG64(reg)				\
	mov	x9, #HYPCTX_##reg;			\
	ldr	x10, [x0, x9];				\
	msr	reg, x10;

#define PUSH_SYSTEM_REG_PAIR(reg0, reg1)		\
	mrs	x9, reg0;				\
	mrs	x10, reg1;				\
	stp	x10, x9, [sp, #-16]!;

#define POP_SYSTEM_REG_PAIR(reg0, reg1)			\
	ldp	x10, x9, [sp], #16;			\
	msr	reg1, x10;				\
	msr	reg0, x9;

#define	SAVE_REG(reg)					\
	mov	x9, #HYPCTX_REGS_##reg;			\
	str	reg, [x0, x9];

#define	LOAD_REG(reg)					\
	mov	x9, #HYPCTX_REGS_##reg;			\
	ldr	reg, [x0, x9];

/*
 * Save a pair of consecutive registers.
 *
 * The STP instruction takes an immediate in the range of [-512, 504] when
 * using the post-indexed addressing mode, but the hypctx register offset can be
 * larger than that.
 *
 * Compute the address by adding the hypctx base address with the register
 * member offset.
 *
 * Using STP/LDP to save/load register pairs to the corresponding struct hypctx
 * variables works because the registers are declared as an array and they are
 * stored in contiguous memory addresses.
 */
#define	SAVE_REG_PAIR(reg0, reg1)			\
	mov	x9, #HYPCTX_REGS_##reg0;		\
	add	x9, x0, x9;				\
	stp	reg0, reg1, [x9];

/*
 * Load a pair of consecutive registers.
 */
#define LOAD_REG_PAIR(reg0, reg1)			\
	mov	x9, #HYPCTX_REGS_##reg0;		\
	add	x9, x0, x9;				\
	ldp	reg0, reg1, [x9];

/*
 * Push all the host registers before returning to the guest.
 *
 * Expecting:
 * x0 - struct hypctx address
 */
#define PUSH_HOST_REGS()				\
	/* Save struct hypctx address in TPIDR_EL2 */	\
	msr	tpidr_el2, x0;				\
							\
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
							\
	/* Push the system registers */			\
	PUSH_SYSTEM_REG_PAIR(ACTLR_EL2, HCR_EL2);	\
	PUSH_SYSTEM_REG_PAIR(CPTR_EL2, HACR_EL2);	\
	mrs	x9, hstr_el2;				\
	str	x9, [sp, #-16]!;

/*
 * Restore all the host registers after returning from the guest.
 *
 * After call:
 * x0 - struct hypctx address
 */
#define POP_HOST_REGS()					\
	/* Pop the system registers first */		\
	ldr	x9, [sp], #16;				\
	msr	hstr_el2, x9;				\
	POP_SYSTEM_REG_PAIR(CPTR_EL2, HACR_EL2);	\
	POP_SYSTEM_REG_PAIR(ACTLR_EL2, HCR_EL2);	\
	isb;						\
							\
	/* Pop the regular registers */			\
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
							\
	/* Restore hypctx address from TPIDR_EL2 */	\
	mrs	x0, tpidr_el2;

/*
 * Save all the guest registers.
 *
 * Expecting:
 * x0 - struct hypctx address
 */
#define	SAVE_GUEST_REGS()				\
	/* Save regular registers first */		\
	/* We use x0 for the hypctx address, save it on the stack */	\
	str	x0, [sp, #-16]!;			\
	/* We use x9 and x10 as temporary registers, push them both */	\
	stp	x9, x10, [sp, #-16]!;			\
	SAVE_REG_PAIR(X1, X2);				\
	SAVE_REG_PAIR(X3, X4);				\
	SAVE_REG_PAIR(X5, X6);				\
	SAVE_REG_PAIR(X7, X8);				\
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

#endif /* !_VMM_HYP_MACROS_H_ */
