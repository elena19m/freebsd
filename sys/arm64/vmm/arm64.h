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
#ifndef _VMM_ARM64_H_
#define _VMM_ARM64_H_

#include <machine/reg.h>
#include <machine/vfp.h>

#include "mmu.h"
#include "vgic.h"
#include "vtimer.h"

struct hypctx {
	struct reg	regs;

	/* EL0 and EL1 control registers */
	uint64_t	actlr_el1;	/* Auxiliary Control Register */
	uint64_t	amair_el1;	/* Auxiliary Memory Attribute Indirection Register */
	uint64_t	elr_el1;	/* Exception Link Register */
	uint64_t	par_el1;	/* Physical Address Register */
	uint64_t	mair_el1;	/* Memory Attribute Indirection Register */
	uint64_t	tcr_el1;	/* Translation Control Register */
	uint64_t	tpidr_el0;	/* EL0 Software ID Register */
	uint64_t	tpidr_el1;	/* EL1 Software ID Register */
	uint64_t	tpidrro_el0;	/* EL0 Read-Only Software Thread ID Register */
	uint64_t	ttbr0_el1;	/* Translation Table Base Register 0 */
	uint64_t	ttbr1_el1;	/* Translation Table Base Register 1 */
	uint64_t	vbar_el1;	/* Vector Base Address Register */
	uint32_t	afsr0_el1;	/* Auxiliary Fault Status Register 0 */
	uint32_t	afsr1_el1;	/* Auxiliary Fault Status Register 1 */
	uint32_t	contextidr_el1;	/* Current Process Identifier */
	uint32_t	cpacr_el1;	/* Arhitectural Feature Access Control Register */
	uint32_t	esr_el1;	/* Exception Syndrome Register */
	uint32_t	far_el1;	/* Fault Address Register */
	uint32_t	sctlr_el1;	/* System Control Register */
	uint32_t	spsr_el1;	/* Saved Program Status Register */

	/* EL2 constrol registers */
	uint64_t	actlr_el2;	/* Auxiliary Control Register */
	uint64_t	hcr_el2;	/* Hypervisor Configuration Register */
	uint32_t	cptr_el2;	/* Architectural Feature Trap Register */
	uint32_t	hacr_el2;	/* Hypervisor Auxiliary Control Register */
	uint32_t	hstr_el2;	/* Hypervisor System Trap Register */

	uint32_t	vcpu;
	struct hyp	*hyp;
	struct {
		uint32_t	esr_el2;	/* Exception Syndrome Register */
		uint32_t	far_el2;	/* Fault Address Register */
		uint32_t	hpfar_el2;	/* Hypervisor IPA Fault Address Register */
	} exit_info;
	struct vtimer_cpu vtimer_cpu;
	struct vgic_cpu_int	vgic_cpu_int;
#ifdef VFP
	struct vfpstate host_vfp_state;
	struct vfpstate guest_vfp_state;
#endif
};

struct hyp {
	lpae_pd_entry_t		l1pd[2 * LPAE_L1_ENTRIES];
	lpae_pd_entry_t		vttbr;
	struct vtimer		vtimer;
	uint64_t		vmid_generation;
	struct vm		*vm;
	lpae_pd_entry_t		l1pd_phys;
	struct hypctx		ctx[VM_MAXCPU];
	bool			vgic_attached;
	struct vgic_distributor	vgic_distributor;
};
CTASSERT((offsetof(struct hyp, l1pd) & PAGE_MASK) == 0);

uint64_t vmm_call_hyp(void *hyp_func_addr, ...);
void vmm_cleanup(void *hyp_stub_vectors);
uint64_t vmm_enter_guest(struct hypctx *hypctx);

#define LOW(x)	(x & 0xFFFFFFFF)
#define HIGH(x)	LOW(x >> 32)

#define VMID_GENERATION_MASK ((1UL<<8) - 1)
#define BUILD_VTTBR(VMID, PTADDR) ((VMID << 48) | PTADDR);

#define MPIDR_SMP_MASK (0x3 << 30)
#define MPIDR_AFF1_LEVEL(x) ((x >> 2) << 8)
#define MPIDR_AFF0_LEVEL(x) ((x & 0x3) << 0)

#endif /* !_VMM_ARM64_H_ */
