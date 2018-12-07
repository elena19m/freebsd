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
#include <machine/hypervisor.h>

#include "mmu.h"
#include "io/vgic_v3.h"
#include "io/vtimer.h"

struct hypctx {
	struct reg	regs;

	/* EL1 control registers */
	uint64_t	actlr_el1;	/* Auxiliary Control Register */
	uint64_t	amair_el1;	/* Auxiliary Memory Attribute Indirection Register */
	uint64_t	elr_el1;	/* Exception Link Register */
	uint64_t	far_el1;	/* Fault Address Register */
	uint64_t	fp;		/* Frame Pointer */
	uint64_t	mair_el1;	/* Memory Attribute Indirection Register */
	uint64_t	par_el1;	/* Physical Address Register */
	uint64_t	sp_el0;		/* Stack Pointer */
	uint64_t	tcr_el1;	/* Translation Control Register */
	uint64_t	tpidr_el0;	/* EL0 Software ID Register */
	uint64_t	tpidrro_el0;	/* Read-only Thread ID Register */
	uint64_t	tpidr_el1;	/* EL1 Software ID Register */
	uint64_t	ttbr0_el1;	/* Translation Table Base Register 0 */
	uint64_t	ttbr1_el1;	/* Translation Table Base Register 1 */
	uint64_t	vbar_el1;	/* Vector Base Address Register */
	uint32_t	afsr0_el1;	/* Auxiliary Fault Status Register 0 */
	uint32_t	afsr1_el1;	/* Auxiliary Fault Status Register 1 */
	uint32_t	contextidr_el1;	/* Current Process Identifier */
	uint32_t	cpacr_el1;	/* Arhitectural Feature Access Control Register */
	uint32_t	esr_el1;	/* Exception Syndrome Register */
	uint32_t	sctlr_el1;	/* System Control Register */
	uint32_t	spsr_el1;	/* Saved Program Status Register */

	/* EL2 control registers */
	uint64_t	elr_el2;	/* Exception Link Register */
	uint64_t	hcr_el2;	/* Hypervisor Configuration Register */
	uint64_t	vpidr_el2;	/* Virtualization Processor ID Register */
	uint64_t	vmpidr_el2;	/* Virtualization Multiprocessor ID Register */
	uint32_t	cptr_el2;	/* Architectural Feature Trap Register */
	uint32_t	spsr_el2;	/* Saved Program Status Register */

	uint32_t	vcpu;
	struct hyp	*hyp;
	struct {
		uint32_t	esr_el2;	/* Exception Syndrome Register */
		uint64_t	far_el2;	/* Fault Address Register */
		uint64_t	hpfar_el2;	/* Hypervisor IPA Fault Address Register */
	} exit_info;

	struct vtimer_cpu 	vtimer_cpu;
	struct vgic_v3_cpu_if	vgic_cpu_if;
	struct vgic_v3_redist	vgic_redist;
#ifdef VFP
	struct vfpstate	vfpstate;
#endif
};

struct hyp {
	pmap_t		stage2_map;
	struct hypctx	ctx[VM_MAXCPU];
	struct vgic_mmio_region	*vgic_mmio_regions;
	size_t		vgic_mmio_regions_num;
	struct vgic_v3_dist vgic_dist;
	struct vm	*vm;
	struct vtimer	vtimer;
	uint64_t	vmid_generation;
	uint64_t	vttbr_el2;
	bool		vgic_attached;
};

uint64_t vmm_call_hyp(void *hyp_func_addr, ...);
void vmm_cleanup(void *hyp_stub_vectors);
uint64_t vmm_enter_guest(struct hypctx *hypctx);
uint64_t vmm_read_ich_vtr_el2(void);
uint64_t vmm_read_cnthctl_el2(void);
uint64_t vmm_read_tcr_el2(void);

#define	eprintf(fmt, ...)	printf("%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
//#define	eprintf(fmt, ...)	do {} while(0)

#define	VMID_GENERATION_MASK 		((1UL<<8) - 1)
#define	build_vttbr(vmid, ptaddr) 	\
		((((vmid) & VMID_GENERATION_MASK) << VTTBR_VMID_SHIFT) | \
						(uint64_t)(ptaddr))

#define	MPIDR_SMP_MASK 		(0x3 << 30)
#define	MPIDR_AFF1_LEVEL(x) 	(((x) >> 2) << 8)
#define	MPIDR_AFF0_LEVEL(x) 	(((x) & 0x3) << 0)

/*
 * Return true if the exception was caused by a translation fault in the stage 2
 * translation regime. The DFSC encoding for a translation fault has the format
 * 0b0001LL, where LL (bits [1:0]) represents the level where the fault occured
 * (page D7-2280 of the ARMv8 Architecture Manual).
 */
#define	ISS_DATA_DFSC_TF(esr_iss)	\
		(!((esr_iss) & 0b111000) && ((esr_iss) & 0b000100))
#define	FAR_EL2_PAGE_OFFSET(x)		((x) & PAGE_MASK)

#define	DEBUG_ME	1

#endif /* !_VMM_ARM64_H_ */
