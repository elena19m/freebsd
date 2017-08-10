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
	str	w9, [x1, x10];

#define LOAD_SYSTEM_REG32(reg)				\
	mov	x10, #HYPCTX_##reg;			\
	ldr	w9, [x1, x10];				\
	msr	reg, x9;

#define	SAVE_SYSTEM_REG64(reg)				\
	mrs	x9, reg;				\
	mov	x10, #HYPCTX_##reg;			\
	str	x9, [x1, x10];

#define LOAD_SYSTEM_REG64(reg)				\
	mov	x10, #HYPCTX_##reg;			\
	ldr	x9, [x1, x10];				\
	msr	reg, x9;

#define PUSH_SYSTEM_REG_PAIR(reg0, reg1)		\
	mrs	x9, reg0;				\
	mrs	x10, reg1;				\
	/* Make room on the stack for the registers */	\
	sub	sp, sp, #(8 * 2);			\
	str	x10, [sp];				\
	str	x9, [sp, #8];

#define POP_SYSTEM_REG_PAIR(reg0, reg1)			\
	ldr	x9, [sp], #8;				\
	ldr	x10, [sp];				\
	add	sp, sp, #(8 * 2);			\
	msr	reg1, x10;				\
	msr	reg0, x9;

#define	SAVE_REG_X(reg)					\
	mov	x9, #(HYPCTX_REGS + reg * 8);		\
	str	x##reg, [x1, x9];

#define	LOAD_REG_X(reg)					\
	mov	x9, #(HYPCTX_REGS + reg * 8);		\
	ldr	x##reg, [x1, x9];

#define	PUSH_REG(reg)					\
	str	reg, [sp, #-16]!;

#define POP_REG(reg)					\
	ldr	reg, [sp], #16;


/*
 * The STP instruction takes an immediate in the range of [-512, 504] when
 * using the post-indexed addressing mode, but the hypctx register offset can be
 * larger than that.
 *
 * Compute the address by adding the hypctx base address with the register
 * member offset.
 *
 * Using STP/LDP to save/load register pairs to the corresponding struct hypctx
 * variables works because the registers are declared as a vector and they are
 * stored in contiguous memory addresses.
 */
#define	SAVE_REG_PAIR_X(reg0, reg1)			\
	mov	x9, #(HYPCTX_REGS + reg0 * 8);		\
	add	x9, x1, x9;				\
	stp	x##reg0, x##reg1, [x9];

#define LOAD_REG_PAIR_X(reg0, reg1)			\
	mov	x9, #(HYPCTX_REGS + reg0 * 8);		\
	add	x9, x1, x9;				\
	ldp	x##reg0, x##reg1, [x9];

#define PUSH_REG_PAIR_X(reg0, reg1)			\
	stp	x##reg0, x##reg1, [sp, #-16]!;

#define POP_REG_PAIR_X(reg0, reg1)			\
	ldp	x##reg0, x##reg1, [sp], #16;

#define PUSH_ALL_X_REGS()				\
	PUSH_REG_PAIR_X(0, 1);				\
	PUSH_REG_PAIR_X(2, 3);				\
	PUSH_REG_PAIR_X(4, 5);				\
	PUSH_REG_PAIR_X(6, 7);				\
	PUSH_REG_PAIR_X(8, 9);				\
	PUSH_REG_PAIR_X(10, 11);			\
	PUSH_REG_PAIR_X(12, 13);			\
	PUSH_REG_PAIR_X(14, 15);			\
	PUSH_REG_PAIR_X(16, 17);			\
	PUSH_REG_PAIR_X(18, 19);			\
	PUSH_REG_PAIR_X(20, 21);			\
	PUSH_REG_PAIR_X(22, 23);			\
	PUSH_REG_PAIR_X(24, 25);			\
	PUSH_REG_PAIR_X(26, 27);			\
	PUSH_REG_PAIR_X(28, 29);			\

/*
 * Save all the host registers before returning to the guest.
 *
 * Expecting:
 * x0 - struct hypctx address.
 */
#define SAVE_HOST_REGS()				\
	/* Save struct hypctx address in TPIDR_EL2 */	\
	msr	tpidr_el2, x0;				\
							\
	/* Save the regular registers */		\
	PUSH_ALL_X_REGS();				\
							\
	/* Save the system registers */			\

#endif /* !_VMM_HYP_MACROS_H_ */
