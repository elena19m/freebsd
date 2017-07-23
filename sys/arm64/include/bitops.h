/*
 * Copyright (C) TODO
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

#ifndef _ARM_BITOPS_H_
#define _ARM_BITOPS_H_

#include <sys/bitstring.h>

#define for_each_set_bit(bit, addr, size) 									\
	for (bit_ffs((bitstr_t *)(addr), (size), (int *)&(bit));				\
	     (bit) != -1;														\
	     bit_ffs_at((bitstr_t *)(addr), (bit) + 1, (size), (int *)&(bit)))

/* same as for_each_set_bit() but use bit as value to start with */
#define for_each_set_bit_from(bit, addr, size)								\
	for (bit_ffs_at((bitstr_t *)(addr), (bit), (size), (int *)&(bit));		\
	     (bit) != -1;														\
	     bit_ffs_at((bitstr_t *)(addr), (bit) + 1, (size), (int *)&(bit)))

#define for_each_clear_bit(bit, addr, size) 								\
	for (bit_ffc((bitstr_t *)(addr), (size), (int *)&(bit));				\
	     (bit) != -1;														\
	     bit_ffc_at((bitstr_t *)(addr), (bit) + 1, (size), (int *)&(bit)))

/* same as for_each_clear_bit() but use bit as value to start with */
#define for_each_clear_bit_from(bit, addr, size)							\
	for (bit_ffc_at((bitstr_t *)(addr), (bit), (size), (int *)&(bit));		\
	     (bit) != -1;														\
	     bit_ffc_at((bitstr_t *)(addr), (bit) + 1, (size), (int *)&(bit)))

#endif /* _ARM_BITOPS_H_ */
