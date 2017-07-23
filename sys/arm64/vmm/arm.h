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

#include "mmu.h"
#include "vgic.h"
#include "vtimer.h"
#include <machine/reg.h>
#include <machine/vfp.h>

struct hypctx {
	uint32_t	vcpu;
	struct hyp*	hyp;

	uint32_t	hcr;

	uint32_t	midr;
	uint32_t	mpidr;

	struct reg	regs;

	uint32_t	sp_und;
	uint32_t	lr_und;
	uint32_t	spsr_und;

	uint32_t	sp_svc;
	uint32_t	lr_svc;
	uint32_t	spsr_svc;

	uint32_t	sp_abt;
	uint32_t	lr_abt;
	uint32_t	spsr_abt;

	uint32_t	sp_irq;
	uint32_t	lr_irq;
	uint32_t	spsr_irq;

	uint32_t	sp_fiq;
	uint32_t	lr_fiq;
	uint32_t	spsr_fiq;
	uint32_t	r8_fiq;
	uint32_t	r9_fiq;
	uint32_t	r10_fiq;
	uint32_t	r11_fiq;
	uint32_t	r12_fiq;

	uint32_t	cp15_sctlr;
	uint32_t	cp15_cpacr;
	uint32_t	cp15_ttbcr;
	uint32_t	cp15_dacr;
	uint64_t	cp15_ttbr0;
	uint64_t	cp15_ttbr1;
	uint32_t	cp15_prrr;
	uint32_t	cp15_nmrr;
	uint32_t	cp15_csselr;
	uint32_t	cp15_cid;
	uint32_t	cp15_tid_urw;
	uint32_t	cp15_tid_uro;
	uint32_t	cp15_tid_priv;
	uint32_t	cp15_dfsr;
	uint32_t	cp15_ifsr;
	uint32_t	cp15_adfsr;
	uint32_t	cp15_aifsr;
	uint32_t	cp15_dfar;
	uint32_t	cp15_ifar;
	uint32_t	cp15_vbar;
	uint32_t	cp15_cntkctl;
	uint64_t	cp15_par;
	uint32_t	cp15_amair0;
	uint32_t	cp15_amair1;
	struct {
		uint32_t	hsr;	/* Hyp Syndrome Register */
		uint32_t	hdfar;	/* VA at a Data Abort exception */
		uint32_t	hifar;	/* VA at a Prefetch Abort exception */
		uint32_t	hpfar;	/* IPA[39:12] at aborts on stage 2 address translations */
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
	bool		vgic_attached;
	struct vgic_distributor	vgic_distributor;
};
CTASSERT((offsetof(struct hyp, l1pd) & PAGE_MASK) == 0);

//uint64_t vmm_call_hyp(void *hyp_func_addr, ...);

//extern void vmm_stub_install(void *hypervisor_stub_vect);
//extern int hyp_enter_guest(struct hypctx *hypctx);

#define LOW(x)	(x & 0xFFFFFFFF)
#define HIGH(x)	LOW(x >> 32)

#define VMID_GENERATION_MASK ((1UL<<8) - 1)
#define BUILD_VTTBR(VMID, PTADDR) ((VMID << 48) | PTADDR);

#define MPIDR_SMP_MASK (0x3 << 30)
#define MPIDR_AFF1_LEVEL(x) ((x >> 2) << 8)
#define MPIDR_AFF0_LEVEL(x) ((x & 0x3) << 0)
