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

#ifndef _VMM_MMU_H_
#define	_VMM_MMU_H_

typedef	uint64_t	lpae_pd_entry_t;		/* LPAE page directory entry */
typedef	uint64_t	lpae_pt_entry_t;		/* LPAE page table entry */

typedef	uint64_t	lpae_vm_paddr_t;		/* LPAE VM paddr */
typedef	uint64_t	lpae_vm_vaddr_t;		/* LPAE VM vaddr */

int lpae_vmmmap_set(void *arg,
	    uint64_t virt_start,
	    uint64_t phys_start,
	    size_t len,
	    int prot);
uint64_t lpae_vmmmap_get(void *arg,
	    uint64_t ipa);
void lpae_vmcleanup(void *arg);

/* Debug only */
void dump_lpae_mapping(void *arg);

#define LPAE_NLEVELS		3

#define	LPAE_L1_TABLE_SIZE	0x1000				/* 4K */
#define	LPAE_L1_ENTRIES		(LPAE_L1_TABLE_SIZE / 8)	/* 512 */

#define	LPAE_L2_TABLE_SIZE	0x1000				/*  4K */
#define	LPAE_L2_ENTRIES		(LPAE_L2_TABLE_SIZE / 8)	/* 512 */

#define	LPAE_L3_TABLE_SIZE	0x1000				/*  4K */
#define	LPAE_L3_ENTRIES		(LPAE_L3_TABLE_SIZE / 8)	/* 512 */

#define	LPAE_L1_SHIFT		30
#define	LPAE_L1_SIZE		(1 << 30)
#define LPAE_L1_INDEX_MASK	0x3
#define	LPAE_L1_T_ADDR_MASK	((uint64_t)0xFFFFFFF000)	/* phys	address	of L2 Table */
#define	LPAE_L1_B_ADDR_MASK	((uint64_t)0xFFC0000000)	/* phys	address	of Phys Block */

#define	LPAE_L2_SHIFT		21
#define	LPAE_L2_SIZE		(1 << 21)
#define LPAE_L2_INDEX_MASK	0x1FF
#define	LPAE_L2_T_ADDR_MASK	((uint64_t)0xFFFFFFF000)/* phys	address	of L3 Table */
#define	LPAE_L2_B_ADDR_MASK	((uint64_t)0xFFFFE00000)/* phys	address	of Phys Block */

#define	LPAE_L3_SHIFT		12
#define	LPAE_L3_SIZE		(1 << 12)
#define LPAE_L3_INDEX_MASK	0x1FF
#define	LPAE_L3_B_ADDR_MASK	((uint64_t)0xFFFFFFF000)/* phys	address	of Phys Block */

#define	LPAE_TYPE_LINK			0x03
#define	LPAE_L12_TYPE_BLOCK		0x01
#define	LPAE_L3_TYPE_BLOCK		0x03
#define	LPAE_TYPE_MASK			0x03			/* mask of type bits */

#define	LPAE_AP_HYP_RW		(0x01 << 6)		/* RW permissions for PL-2 stage 1*/
#define	LPAE_AP_HYP_RDONLY	(0x03 << 6)		/* RD permissions for PL-2 stage 1 */

#define	LPAE_HAP_READ		(0x01 << 6)		/* read permissions for stage 2 */
#define	LPAE_HAP_WRITE		(0x02 << 6)		/* write permissions for stage 2*/

#define	LPAE_AF			(0x1 << 10)		/* Access Flag */

#define LPAE_ATTR_IDX_WB	(0b011 << 2)		/* stage 1 mem attr writeback */
#define LPAE_MEM_ATTR_OUT_WB	(0b11  << 4)		/* stage 2 mem attr outer writeback */
#define LPAE_MEM_ATTR_IN_WB	(0b11  << 2)		/* stage 2 mem attr inner writeback */
#define LPAE_MEM_ATTR_IO_WB	(LPAE_MEM_ATTR_OUT_WB | LPAE_MEM_ATTR_IN_WB)

/* Table B3-24 Long-descriptor format FSR encodings */
#define LPAE_TRANSLATION_FAULT(x)	((0b000111) & x)
#define LPAE_ACCESS_FLAG_FAULT(x)	((0b001011) & x)
#define LPAE_PERMISSION_FAULT(x)	((0b001111) & x)
#define LPAE_FAULT_LEVEL(x)		(0x3 & x)
#endif
