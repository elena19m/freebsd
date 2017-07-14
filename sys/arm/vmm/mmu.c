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

#include <sys/cdefs.h>

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/smp.h>

#include <cpu-v6.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/param.h>
#include <machine/cpufunc.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>

#include <machine/vmm.h>
#include "mmu.h"
#include "arm.h"

MALLOC_DECLARE(M_HYP);
extern lpae_pd_entry_t *hyp_l1pd;
/*
 * create_lpae_mapping
 * - l1pd - the level 1 address of the PD (NULL for the HYP mode PD)
 * - virt_start - a 32 bit virtual address to be mapped
 * - phys_start - a 64 bit physical address to map to
 * - len - the desired length mapping, but it will be truncated to the virt_start 
 *         alignment
 * - prot - the FreeBSD mapping permissions
 * - returns the actual length of the mapping
 *
 * An l1pd or l2pd will have a size of 8K (2 * LPAE_Lx_ENTRIES * sizeof(lpae_pd_entry_t)).
 * The first 4K will include the bits for the MMU (physical addresses and bit permissions)
 * and the second 4K will be a mirror of the first one but will include the virtual
 * addresses of allocated page tables needed for walking and clean-up.
 *
 */
static int create_lpae_mapping(lpae_pd_entry_t *l1pd,
		    lpae_vm_vaddr_t virt_start,
		    lpae_vm_paddr_t phys_start,
		    size_t len,
		    vm_prot_t prot)
{
	lpae_pd_entry_t *l2pd, *l3pd, *l1pd_shadow, *l2pd_shadow, *pd;
	int l1_index, l2_index, l3_index;
	int mapped_size = 0;
	bool is_hyp_pd = false;

	if (l1pd == NULL) {
		l1pd = &hyp_l1pd[0];
		is_hyp_pd = true;
	}

	l1_index = (virt_start >> LPAE_L1_SHIFT) & LPAE_L1_INDEX_MASK;
	l2_index = (virt_start >> LPAE_L2_SHIFT) & LPAE_L2_INDEX_MASK;
	l3_index = (virt_start >> LPAE_L3_SHIFT) & LPAE_L3_INDEX_MASK;

	if ((virt_start & LPAE_L1_B_ADDR_MASK) == virt_start) {
		if (len >= LPAE_L1_SIZE) {
			mapped_size = LPAE_L1_SIZE;
		}
	}
	if(!mapped_size && (virt_start & LPAE_L2_B_ADDR_MASK) == virt_start) {
		if (len >= LPAE_L2_SIZE) {
			mapped_size = LPAE_L2_SIZE;
		}
	}
	if(!mapped_size) {
		mapped_size = LPAE_L3_SIZE;
	}

	if (mapped_size == LPAE_L1_SIZE) {
		pd = &l1pd[l1_index];
		/* See if this PD is a link and fallback to the next level */
		if ((*pd & LPAE_TYPE_LINK) == LPAE_TYPE_LINK)
			mapped_size = LPAE_L2_SIZE;
		else
			goto set_prot;
	}

	l1pd_shadow = &l1pd[LPAE_L1_ENTRIES];

	if (l1pd[l1_index] == 0) {
		l2pd = malloc(2 * PAGE_SIZE, M_HYP, M_WAITOK | M_ZERO);
		l2pd_shadow = &l2pd[LPAE_L2_ENTRIES];

		l1pd[l1_index] = (lpae_pd_entry_t) vtophys(l2pd);
		l1pd[l1_index] |= LPAE_TYPE_LINK;

		l1pd_shadow[l1_index] = (lpae_pd_entry_t) l2pd;

	} else {
		l2pd = (lpae_pd_entry_t *) (l1pd_shadow[l1_index]);
		l2pd_shadow = &l2pd[LPAE_L2_ENTRIES];
	}

	if (mapped_size == LPAE_L2_SIZE) {
		pd = &l2pd[l2_index];
		/* See if this PD is a link and fallback to the next level */
		if ((*pd & LPAE_TYPE_LINK) == LPAE_TYPE_LINK)
			mapped_size = LPAE_L3_SIZE;
		else
			goto set_prot;
	}

	if (l2pd[l2_index] == 0) {
		l3pd = malloc(PAGE_SIZE, M_HYP, M_WAITOK | M_ZERO);
		l2pd[l2_index] = vtophys(l3pd);
		l2pd[l2_index] |= LPAE_TYPE_LINK;

		l2pd_shadow[l2_index] = (lpae_pd_entry_t) l3pd;
	} else {
		l3pd = (lpae_pd_entry_t *) (l2pd_shadow[l2_index]);
	}
	
	pd = &l3pd[l3_index];

set_prot:
	if (prot != VM_PROT_NONE) {
		*pd = phys_start;
		*pd |= LPAE_AF;
		if (mapped_size == LPAE_L3_SIZE)
			*pd |= LPAE_L3_TYPE_BLOCK;
		else
			*pd |= LPAE_L12_TYPE_BLOCK;
		if (is_hyp_pd) { /* PL-2 stage-1 table */
			if (prot & (VM_PROT_READ | VM_PROT_WRITE))
				*pd |= LPAE_AP_HYP_RW | LPAE_ATTR_IDX_WB;
			else /* Map read-only*/
				*pd |= LPAE_AP_HYP_RDONLY | LPAE_ATTR_IDX_WB;
		} else { /* VM stage-2 page table */
			if (prot & VM_PROT_READ)
				*pd |= LPAE_HAP_READ | LPAE_MEM_ATTR_IO_WB;
			if (prot & VM_PROT_WRITE)
				*pd |= LPAE_HAP_WRITE | LPAE_MEM_ATTR_IO_WB;
		}
	} else {
		*pd = 0;
	}

	return mapped_size;
}

void dump_lpae_mapping(void *arg)
{
	int i, j, k;
	struct hyp *vm_hyp;
	lpae_pd_entry_t *l1pd, *l1pd_shadow, *l2pd, *l2pd_shadow, *l3pd;

	vm_hyp = arg;

	if (arg)
		l1pd = &vm_hyp->l1pd[0];
	else 
		l1pd = &hyp_l1pd[0];

	l1pd_shadow = &l1pd[LPAE_L1_ENTRIES];

	printf("l1pd = %x\n", vtophys(l1pd));

	for (i = 0; i < LPAE_L1_ENTRIES; i++) {
		if(l1pd_shadow[i]) {
			printf("\t %d: l2pd = %llx\n", i, l1pd[i]);
			l2pd = (lpae_pd_entry_t *) l1pd_shadow[i];
			l2pd_shadow = &l2pd[LPAE_L2_ENTRIES];
			for (j = 0; j < LPAE_L2_ENTRIES; j++) {
				if (l2pd_shadow[j]) {
					printf("\t\t %d: l3pd = %llx\n", j, l2pd[j]);
					l3pd = (lpae_pd_entry_t *) l2pd_shadow[j];
					for (k = 0; k < LPAE_L3_ENTRIES; k++) {
						if (l3pd[k])
							printf("\t\t\t %d: l3_entry = %llx\n", k, l3pd[k]);
					}
				}
			}
		}
	}
}

int lpae_vmmmap_set(void *arg,
		    uint64_t virt_start,
		    uint64_t phys_start,
		    size_t len,
		    int prot)
{
	size_t n;
	struct hyp *vm_hyp;
	lpae_pd_entry_t *l1pd = NULL;
	vm_hyp = arg;
	if (arg)
		l1pd = &vm_hyp->l1pd[0];

	while (1) {
		n = create_lpae_mapping(l1pd, virt_start, phys_start, len, prot);

		if (len <= n)
			break;
		len -= n;
		virt_start += n;
		phys_start += n;
		printf("%s n: %d %d\n", __func__, n, len);
	}

	/*
	 * Flush all caches to be sure the tables entries are
	 * in physical memory
	 */
	tlb_flush_all();
	dcache_wbinv_poc_all();
	icache_inv_all();

	return (0);
}

uint64_t lpae_vmmmap_get(void *arg, uint64_t ipa)
{
	struct hyp *vm_hyp;
	int l1_index, l2_index, l3_index;
	lpae_pd_entry_t *l1pd, *l1pd_shadow, *l2pd, *l2pd_shadow, *l3pd;

	vm_hyp = arg;

	if (arg)
		l1pd = &vm_hyp->l1pd[0];
	else 
		l1pd = &hyp_l1pd[0];

	l1pd_shadow = &l1pd[LPAE_L1_ENTRIES];

	/* Check if there is a connnection to a 2nd level PT */
	l1_index = (ipa >> LPAE_L1_SHIFT) & LPAE_L1_INDEX_MASK;
	if ((l1pd[l1_index] & LPAE_TYPE_LINK) == LPAE_TYPE_LINK) {

		/* Grab the virtual address of the 2nd leel PT */
		l2pd = (lpae_pd_entry_t *) (l1pd_shadow[l1_index]);
		l2pd_shadow = &l2pd[LPAE_L2_ENTRIES];

		/* Check if there is a connect to a 3nd level PT */
		l2_index = (ipa >> LPAE_L2_SHIFT) & LPAE_L2_INDEX_MASK;
		if ((l2pd[l2_index] & LPAE_TYPE_LINK) == LPAE_TYPE_LINK) {
			
			l3pd = (lpae_pd_entry_t *) (l2pd_shadow[l2_index]);
		
			l3_index = (ipa >> LPAE_L3_SHIFT) & LPAE_L3_INDEX_MASK;
			return (l3pd[l3_index] & LPAE_L3_B_ADDR_MASK);
		} else {
			return (l2pd[l2_index] & LPAE_L2_B_ADDR_MASK);
		}
	} else {
		return (l1pd[l1_index] & LPAE_L1_B_ADDR_MASK);
	}

	return ((uint64_t)-1);
}

void lpae_vmcleanup(void *arg)
{
	int i, j;
	struct hyp *vm_hyp;
	lpae_pd_entry_t *l1pd, *l1pd_shadow, *l2pd, *l2pd_shadow;

	vm_hyp = arg;

	if (arg)
		l1pd = &vm_hyp->l1pd[0];
	else 
		l1pd = &hyp_l1pd[0];

	l1pd_shadow = &l1pd[LPAE_L1_ENTRIES];

	for (i = 0; i < LPAE_L1_ENTRIES; i++) {
		if(l1pd_shadow[i]) {
			l2pd = (lpae_pd_entry_t *) l1pd_shadow[i];
			l2pd_shadow = &l2pd[LPAE_L2_ENTRIES];
			for (j = 0; j < LPAE_L2_ENTRIES; j++) {
				if (l2pd_shadow[j]) {
					free((void *) l2pd_shadow[j], M_HYP);
				}
			}
			free((void *) l1pd_shadow[i], M_HYP);
		}
	}
}
