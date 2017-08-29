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
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_param.h>
#include <machine/vm.h>
#include <machine/vm_param.h>

#include "mmu.h"

MALLOC_DECLARE(M_HYP);

void
hypmap_init(pmap_t map)
{
	mtx_init(&map->pm_mtx, "hypmap_pm_mtx", NULL, MTX_DEF);
	pmap_pinit(map);
}

void
hypmap_map(pmap_t map, vm_offset_t va, size_t len, vm_prot_t prot)
{
	vm_offset_t va_end, hypva;
	vm_page_t dummy_page;

	dummy_page = malloc(sizeof(*dummy_page), M_HYP, M_WAITOK | M_ZERO);
	dummy_page->oflags = VPO_UNMANAGED;
	dummy_page->md.pv_memattr = VM_MEMATTR_DEFAULT;

	/*
	 * Add the physical pages which correspond to the specified virtual
	 * addresses.The virtual addresses span contiguous virtual pages, but
	 * they might not reside in contiguous physical pages.
	 */
	va_end = va + len - 1;
	va = trunc_page(va);
	while (va < va_end) {
		dummy_page->phys_addr = vtophys(va);
		hypva = (va >= VM_MIN_KERNEL_ADDRESS ? ktohyp(va) : va);
		pmap_enter(map, hypva, dummy_page, prot, PMAP_ENTER_WIRED, 0);
		va += PAGE_SIZE;
	}

	free(dummy_page, M_HYP);
}

void
hypmap_map_identity(pmap_t map, vm_offset_t va, size_t len,
		vm_prot_t prot)
{
	vm_offset_t va_end;
	vm_page_t dummy_page;

	dummy_page = malloc(sizeof(*dummy_page), M_HYP, M_WAITOK | M_ZERO);
	dummy_page->oflags = VPO_UNMANAGED;
	dummy_page->md.pv_memattr = VM_MEMATTR_DEFAULT;

	/*
	 * The virtual addresses span contiguous virtual pages, but they might
	 * not reside in contiguous physical pages. For each virtual page we
	 * get the physical page address and use that for the mapping.
	 */
	va_end = va + len - 1;
	va = trunc_page(va);
	while (va < va_end) {
		dummy_page->phys_addr = vtophys(va);
		pmap_enter(map, dummy_page->phys_addr, dummy_page,
				prot, PMAP_ENTER_WIRED, 0);
		va += PAGE_SIZE;
	}

	free(dummy_page, M_HYP);
}

/*
 * Map "len" bytes starting at virtual address "va" to "len" bytes
 * starting at physical address "pa".
 */
void
hypmap_set(pmap_t map, vm_offset_t va, vm_offset_t pa, size_t len,
		vm_prot_t prot)
{
	vm_offset_t va_end, hypva;
	vm_page_t dummy_page;

	dummy_page = malloc(sizeof(*dummy_page), M_HYP, M_WAITOK | M_ZERO);
	dummy_page->oflags = VPO_UNMANAGED;
	dummy_page->md.pv_memattr = VM_MEMATTR_DEFAULT;

	va_end = va + len - 1;
	va = trunc_page(va);
	dummy_page->phys_addr = trunc_page(pa);
	while (va < va_end) {
		hypva = (va >= VM_MIN_KERNEL_ADDRESS ? ktohyp(va) : va);
		pmap_enter(map, hypva, dummy_page, prot, PMAP_ENTER_WIRED, 0);
		va += PAGE_SIZE;
		dummy_page->phys_addr += PAGE_SIZE;
	}

	free(dummy_page, M_HYP);
}

/*
 * Return the physical address associated with virtual address "va".
 */
vm_paddr_t
hypmap_get(pmap_t map, vm_offset_t va)
{
	return pmap_extract(map, va);
}

/*
 * Remove all the mappings from the hyp translation tables
 */
void
hypmap_cleanup(pmap_t map)
{
	pmap_remove(map, HYP_VM_MIN_ADDRESS, HYP_VM_MAX_ADDRESS);
	mtx_destroy(&map->pm_mtx);
	pmap_release(map);
}
