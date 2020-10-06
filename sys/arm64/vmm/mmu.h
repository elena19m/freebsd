/*
 * Copyright (C) 2017 Alexandru Elisei <alexandru.elisei@gmail.com>
 * All rights reserved.
 *
 * This software was developed by Alexandru Elisei under sponsorship
 * from the FreeBSD Foundation.
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

#include <machine/vmparam.h>
#include <machine/vmm.h>

#include "hyp.h"

#define ktohyp(kva)		(((vm_offset_t)(kva) & HYP_KVA_MASK) | \
					HYP_KVA_OFFSET)
#define ipatok(ipa, hypmap)	(PHYS_TO_DMAP(pmap_extract(hypmap, (ipa))))
#define gtoipa(gva) 		((gva) - KERNBASE + VM_GUEST_BASE_IPA)

#define page_aligned(x)		(((vm_offset_t)(x) & PAGE_MASK) == 0)

void 		hypmap_init(pmap_t map, enum pmap_stage pm_stage);
void 		hypmap_map(pmap_t map, vm_offset_t va, size_t len,
			vm_prot_t prot);
void 		hypmap_map_identity(pmap_t map, vm_offset_t va, size_t len,
			vm_prot_t prot);
void 		hypmap_set(void *arg, vm_offset_t va, vm_offset_t pa,
			size_t len, vm_prot_t prot);
vm_paddr_t 	hypmap_get(void *arg, vm_offset_t va);
void 		hypmap_cleanup(pmap_t map);

#endif
