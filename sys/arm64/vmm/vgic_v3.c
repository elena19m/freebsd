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
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/smp.h>
#include <sys/bitstring.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/bitops.h>
#include <machine/cpufunc.h>
#include <machine/cpu.h>
#include <machine/param.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <machine/intr.h>
#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>

#include <arm/arm/gic_common.h>
#include <arm64/arm64/gic_v3_reg.h>
#include <arm64/arm64/gic_v3_var.h>

#include "hyp.h"
#include "mmu.h"
#include "vgic_v3.h"
#include "vgic_v3_reg.h"
#include "arm64.h"

#define VGIC_V3_DEVNAME		"vgic"
#define VGIC_V3_DEVSTR		"ARM Virtual Generic Interrupt Controller v3"

#define	RES0			(0)

#define	int_pending(lr)		\
    (ICH_LR_EL2_STATE(lr) == ICH_LR_EL2_STATE_PENDING)
#define	int_inactive(lr)		\
    (ICH_LR_EL2_STATE(lr) == ICH_LR_EL2_STATE_INACTIVE)

MALLOC_DEFINE(M_VGIC_V3, "ARM VMM VGIC V3", "ARM VMM VGIC V3");

extern uint64_t hypmode_enabled;

struct vgic_v3_virt_features {
	uint8_t min_prio;
	size_t ich_lr_num;
	size_t ich_ap0r_num;
	size_t ich_ap1r_num;
};
static struct vgic_v3_virt_features virt_features;

struct vgic_v3_ro_regs {
	uint32_t gicd_icfgr0;
	uint32_t gicd_pidr2;
	uint32_t gicd_typer;
};
static struct vgic_v3_ro_regs ro_regs;

/* TODO: Do not manage the softc directly and use the device's softc */
static struct vgic_v3_softc softc;

#define	read_reg(arr, base, off)						\
({										\
	size_t size = sizeof(*arr);						\
	size_t idx;								\
	uint64_t val;								\
										\
	if (((off) & (size - 1)) != 0) {					\
		eprintf("Warning: Reading invalid register offset 0x%016lx\n",	\
				(off));						\
		val = RES0;							\
	} else {								\
		idx = ((off) - (base)) / size;					\
		val = arr[idx];							\
	}									\
	val;									\
})

int
vgic_v3_redist_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	struct vgic_v3_dist *dist;
	uint64_t off;
	bool *retu;

	retu = (bool *)arg;
	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;
	dist = &hyp->vgic_dist;

	/* Offset of redistributor register. */
	off = fault_ipa - redist->ipa;

	if (off == GICR_PIDR2) {
		/* GICR_PIDR2 has the same value as GICD_PIDR2 */
		*rval = dist->gicd_pidr2;

	} else if (off == GICR_TYPER) {
		*rval = redist->gicr_typer;

	} else if (off == GICR_WAKER) {
		/* Redistributor is always awake. */
		*rval = 0 & ~GICR_WAKER_PS & ~GICR_WAKER_CA;

	} else if (off == GICR_CTLR) {
		/* No writes pending */
		*rval = redist->gicr_ctlr & ~GICR_CTLR_RWP & ~GICR_CTLR_UWP;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_IGROUPR0) {
		*rval = redist->gicr_igroupr0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICENABLER0) {
		*rval = redist->gicr_icenabler0_isenabler0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ISENABLER0) {
		*rval = redist->gicr_icenabler0_isenabler0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR0_BASE) {
		*rval = redist->gicr_icfgr0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR1_BASE) {
		*rval = redist->gicr_icfgr1;

	} else if (off >= GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE &&
	    off < redist->gicr_ipriorityr_addr_max) {
		*rval = read_reg(redist->gicr_ipriorityr,
		    GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE, off);

	} else {
		eprintf("Unknown register offset: 0x%04lx\n", off);
		*rval = RES0;
	}

	*retu = false;
	return (0);
}

#define write_reg(reg, base, off, val)						\
do {										\
	size_t size = sizeof(*reg);						\
	size_t idx;								\
										\
	if (((off) & (size - 1)) != 0) {					\
		eprintf("Warning: Writing invalid register offset 0x%016lx\n",	\
				(off));						\
	} else {								\
		idx = ((off) - (base)) / size;					\
		reg[idx] = val;							\
	}									\
} while (0)

int
vgic_v3_redist_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t val,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	uint64_t off;
	bool *retu;

	retu = (bool *)arg;
	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	/* Offset of redistributor register. */
	off = fault_ipa - redist->ipa;

	if (off == GICR_PIDR2) {
		eprintf("Warning: Trying to write to read-only register GICR_PIDR2.\n");

	} else if (off == GICR_TYPER) {
		eprintf("Warning: Trying to write to read-only register GICR_TYPER.\n");

	} else if (off == GICR_WAKER) {
		/*
		 * Ignore writes to GICRR_WAKER. The Redistributor will always
		 * be awake.
		 */
		;

	} else if (off == GICR_CTLR) {
		/* Writes are never pending. */
		redist->gicr_ctlr = val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_IGROUPR0) {
		redist->gicr_igroupr0 = val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICENABLER0) {
		/* A write of 1 to ICENABLER disables the interrupt. */
		redist->gicr_icenabler0_isenabler0 &= ~val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ISENABLER0) {
		/* A write of 1 to ISENABLER enables the interrupt */
		redist->gicr_icenabler0_isenabler0 |= val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR0_BASE) {
		redist->gicr_icfgr0 = val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR1_BASE) {
		redist->gicr_icfgr1 = val;

	} else if (off >= GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE &&
	    off < redist->gicr_ipriorityr_addr_max) {
		write_reg(redist->gicr_ipriorityr,
		    GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE, off, val);
	} else {
		eprintf("Unknown register offset: 0x%04lx\n", off);
	}


	*retu = false;
	return (0);
}

int
vgic_v3_dist_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	uint64_t off;
	bool *retu;

	retu = (bool *)arg;
	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	/* Offset of distributor register. */
	off = fault_ipa - dist->ipa;

	if (off == GICD_CTLR) {
		*rval = dist->gicd_ctlr;

	} else if (off == GICD_TYPER) {
		*rval = dist->gicd_typer;

	} else if (off == GICD_IIDR) {
		*rval = RES0;

	} else if (off == GICD_PIDR2) {
		*rval = dist->gicd_pidr2;

	} else if (off >= GICD_IGROUPR_BASE && off < dist->gicd_igroupr_addr_max) {
		*rval = read_reg(dist->gicd_igroupr, GICD_IGROUPR_BASE, off);

	} else if (off >= GICD_ICFGR_BASE && off < dist->gicd_icfgr_addr_max) {
		*rval = read_reg(dist->gicd_icfgr, GICD_ICFGR_BASE, off);

	} else if (off >= GICD_IPRIORITYR_BASE &&
	    off < dist->gicd_ipriorityr_addr_max) {
		*rval = read_reg(dist->gicd_ipriorityr, GICD_IPRIORITYR_BASE,
		    off);

	} else if (off >= GICD_ICENABLER_BASE &&
	    off < dist->gicd_icenabler_addr_max) {
		*rval = read_reg(dist->gicd_icenabler_isenabler,
		    GICD_ICENABLER_BASE, off);

	} else if (off >= GICD_ISENABLER_BASE &&
	    off < dist->gicd_isenabler_addr_max) {
		*rval = read_reg(dist->gicd_icenabler_isenabler,
		    GICD_ISENABLER_BASE, off);

	} else if (off >= GICD_IROUTER_BASE && off < dist->gicd_irouter_addr_max) {
		*rval = read_reg(dist->gicd_irouter, GICD_IROUTER_BASE, off);

	} else {
		eprintf("Unknown register offset: 0x%04lx\n", off);
		*rval = RES0;
	}

	*retu = false;
	return (0);
}

int
vgic_v3_dist_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t val,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	uint64_t off;
	uint32_t icenabler, isenabler;
	bool *retu;

	retu = (bool *)arg;
	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	/* Offset of distributor register. */
	off = fault_ipa - dist->ipa;

	if (off == GICD_CTLR) {
		/* Writes are never pending. */
		dist->gicd_ctlr = val & ~GICD_CTLR_RWP;

	} else if (off == GICD_TYPER) {
		eprintf("Warning: Trying to write to read-only register GICD_TYPER.\n");

	} else if (off == GICD_PIDR2) {
		eprintf("Warning: Trying to write to read-only register GICD_PIDR2.\n");

	} else if (off == GICD_IIDR) {
		eprintf("write: GICD_IIDR not implemented\n");

	} else if (off >= GICD_IGROUPR_BASE && off < dist->gicd_igroupr_addr_max) {
		write_reg(dist->gicd_igroupr, GICD_IGROUPR_BASE, off, val);

	} else if (off >= GICD_ICFGR_BASE && off < dist->gicd_icfgr_addr_max) {
		if (off == GICD_ICFGR_BASE)
			eprintf("Warning: Trying to write to read-only register GICD_ICFGR0.\n");
		else
			write_reg(dist->gicd_icfgr, GICD_ICFGR_BASE, off, val);

	} else if (off >= GICD_IPRIORITYR_BASE &&
	    off < dist->gicd_ipriorityr_addr_max) {
		write_reg(dist->gicd_ipriorityr, GICD_IPRIORITYR_BASE, off, val);

	} else if (off >= GICD_ICENABLER_BASE &&
	    off < dist->gicd_icenabler_addr_max) {
		icenabler = read_reg(dist->gicd_icenabler_isenabler,
		    GICD_ICENABLER_BASE, off);
		/* A write of 1 to ICENABLER disables the interrupt. */
		icenabler &= ~val;
		write_reg(dist->gicd_icenabler_isenabler, GICD_ICENABLER_BASE,
		    off, icenabler);

	} else if (off >= GICD_ISENABLER_BASE &&
	    off < dist->gicd_isenabler_addr_max) {
	       	isenabler = read_reg(dist->gicd_icenabler_isenabler,
		    GICD_ISENABLER_BASE, off);
		/* A write of 1 to ISENABLER enables the interrupt. */
		isenabler |= val;
		write_reg(dist->gicd_icenabler_isenabler, GICD_ISENABLER_BASE,
		    off, isenabler);

	} else if (off >= GICD_IROUTER_BASE &&
	    off < dist->gicd_irouter_addr_max) {
		write_reg(dist->gicd_irouter, GICD_IROUTER_BASE, off, val);

	} else {
		eprintf("Unknown register offset: 0x%04lx\n", off);
	}

	*retu = false;
	return (0);
}

static void
vgic_v3_init_redist_regs(struct vgic_v3_redist *redist,struct hypctx *hypctx,
    bool last_vcpu)
{
	uint64_t aff, vmpidr_el2;

	redist->gicr_typer = 0;
	vmpidr_el2 = hypctx->vmpidr_el2;
	/*
	 * Get affinity for the current CPU. The affinity from MPIDR_EL1
	 * matches the affinity from GICR_TYPER and this is how the CPU finds
	 * its corresponding Redistributor.
	 */
	aff = (CPU_AFF3(vmpidr_el2) << 24) | (CPU_AFF2(vmpidr_el2) << 16) |
	    (CPU_AFF1(vmpidr_el2) << 8) | CPU_AFF0(vmpidr_el2);

	/* Set up GICR_TYPER. */
	redist->gicr_typer = aff << GICR_TYPER_AFF_SHIFT;
	/* Redistributor doesn't support virtual or physical LPIS. */
	redist->gicr_typer &= ~GICR_TYPER_VLPIS;
	redist->gicr_typer &= ~GICR_TYPER_PLPIS;

	if (last_vcpu)
		/* Mark the last Redistributor */
		redist->gicr_typer |= GICR_TYPER_LAST;

	/*
	 * Configure the Redistributor Control Register.
	 *
	 * ~GICR_CTLR_LPI_ENABLE: LPIs are disabled
	 */
	redist->gicr_ctlr = 0 & ~GICR_CTLR_LPI_ENABLE;

	redist->gicr_ipriorityr_addr_max = \
	    GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE + \
	    sizeof(redist->gicr_ipriorityr);
}

void
vgic_v3_cpuinit(void *arg, bool last_vcpu)
{
	struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	struct vgic_v3_redist *redist;

	hypctx = (struct hypctx *)arg;
	redist = &hypctx->vgic_redist;
	vgic_v3_init_redist_regs(redist, hypctx, last_vcpu);

	cpu_if = &hypctx->vgic_cpu_if;
	/*
	 * Configure the Interrupt Controller Hyp Control Register.
	 *
	 * ICH_HCR_EL2_En: enable virtual CPU interface.
	 *
	 * Maintenance interrupts are disabled.
	 */
	cpu_if->ich_hcr_el2 = ICH_HCR_EL2_En;

	/*
	 * Configure the Interrupt Controller Virtual Machine Control Register.
	 *
	 * ICH_VMCR_EL2_VPMR: lowest priority mask for the VCPU interface
	 * ICH_VMCR_EL2_VBPR1_NO_PREEMPTION: disable interrupt preemption for
	 * Group 1 interrupts
	 * ICH_VMCR_EL2_VBPR0_NO_PREEMPTION: disable interrupt preemption for
	 * Group 0 interrupts
	 * ~ICH_VMCR_EL2_VEOIM: writes to EOI registers perform priority drop
	 * and interrupt deactivation.
	 * ICH_VMCR_EL2_VENG1: virtual Group 1 interrupts enabled.
	 */
	cpu_if->ich_vmcr_el2 = \
	    (virt_features.min_prio << ICH_VMCR_EL2_VPMR_SHIFT) | \
	    ICH_VMCR_EL2_VBPR1_NO_PREEMPTION | ICH_VMCR_EL2_VBPR0_NO_PREEMPTION;
	cpu_if->ich_vmcr_el2 &= ~ICH_VMCR_EL2_VEOIM;
	cpu_if->ich_vmcr_el2 |= ICH_VMCR_EL2_VENG1;

	cpu_if->ich_lr_num = virt_features.ich_lr_num;
	cpu_if->ich_ap0r_num = virt_features.ich_ap0r_num;
	cpu_if->ich_ap1r_num = virt_features.ich_ap1r_num;
}

#define	INIT_DIST_REG(name, n, base, dist)				\
do {									\
	(dist)->name = malloc((n) * sizeof(*(dist)->name),		\
			M_VGIC_V3, M_WAITOK | M_ZERO);			\
	/* TODO num is not necessary? */				\
	(dist)->name##_num = (n);					\
	(dist)->name##_addr_max = (base)+ (n) * sizeof(*(dist)->name);	\
} while (0)

static void
init_dist_regs(struct vgic_v3_dist *dist)
{
	size_t n;
	size_t reg_size;

	/*
	 * Configure the Distributor control register.
	 *
	 * GICD_CTLR_G1: enable Group 0 interrupts
	 * GICD_CTLR_G1A: enable Group 1 interrupts
	 * GICD_CTLR_ARE_NS: enable affinity routing
	 * GICD_CTLR_DS: ARM GIC Architecture Specification for GICv3 and
	 * GICv4, p. 4-464: when the distributor supports a single security
	 * state, this bit is RAO/WI
	 */
	dist->gicd_ctlr = GICD_CTLR_G1 | GICD_CTLR_G1A | GICD_CTLR_ARE_NS | \
	    GICD_CTLR_DS;

	dist->gicd_typer = ro_regs.gicd_typer;
	dist->nirqs = GICD_TYPER_I_NUM(dist->gicd_typer);
	dist->gicd_pidr2 = ro_regs.gicd_pidr2;

	/* TODO: sort them alphabeticaly. */

	/* Round up the number of registers to the nearest integer. */
	n = (dist->nirqs + 32 - 1) / 32;
	INIT_DIST_REG(gicd_igroupr, n, GICD_IGROUPR_BASE, dist);

	/* ARM GIC Architecture Specification, page 8-471. */
	n = (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1;
	reg_size = sizeof(*dist->gicd_icenabler_isenabler);
	dist->gicd_icenabler_isenabler = malloc(n * reg_size, M_VGIC_V3,
	    M_WAITOK | M_ZERO);
	dist->gicd_icenabler_isenabler_num = n;
	dist->gicd_icenabler_addr_max = GICD_ICENABLER_BASE + n * reg_size;
	dist->gicd_isenabler_addr_max = GICD_ISENABLER_BASE + n * reg_size;

	/* ARM GIC Architecture Specification, page 8-483. */
	n = 8 * ((dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1);
	INIT_DIST_REG(gicd_ipriorityr, n, GICD_IPRIORITYR_BASE, dist);

	n = (dist->nirqs + 16 - 1) / 16;
	INIT_DIST_REG(gicd_icfgr, n, GICD_ICFGR_BASE, dist);

	/* ARM GIC Architecture Specification, page 8-485. */
	n = 32 * (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK + 1) - 1;
	INIT_DIST_REG(gicd_irouter, n, GICD_IROUTER_BASE, dist);
}

void
vgic_v3_vminit(void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;

	hyp = (struct hyp *)arg;
	dist = &hyp->vgic_dist;
	init_dist_regs(dist);
}

int
vgic_v3_attach_to_vm(void *arg, uint64_t dist_ipa, size_t dist_size,
    uint64_t redist_ipa, size_t redist_size)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	struct vgic_v3_redist *redist;
	int i;

	hyp = (struct hyp *)arg;
	dist = &hyp->vgic_dist;

	/* Set the distributor address and size for trapping guest access. */
	dist->ipa = dist_ipa;
	dist->size = dist_size;

	for (i = 0; i < VM_MAXCPU; i++) {
		redist = &hyp->ctx[i].vgic_redist;
		/* Set the redistributor address and size. */
		redist->ipa = redist_ipa;
		redist->size = redist_size;
	}

	hyp->vgic_attached = true;

	return (0);
}

/* TODO: call this on VM destroy. */
static void vgic_v3_detach_from_vm(void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;

	hyp = (struct hyp *)arg;
	dist = &hyp->vgic_dist;

	free(dist->gicd_igroupr, M_VGIC_V3);
	free(dist->gicd_icfgr, M_VGIC_V3);
	free(dist->gicd_ipriorityr, M_VGIC_V3);
	free(dist->gicd_icenabler_isenabler, M_VGIC_V3);
	free(dist->gicd_irouter, M_VGIC_V3);
}

/* Called before entering the VM */
void
vgic_v3_flush_hwstate(void *arg)
{
	struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	struct vgic_v3_dist *dist;

	hypctx = arg;
	cpu_if = &hypctx->vgic_cpu_if;
	dist = &hypctx->hyp->vgic_dist;


	//mtx_lock_spin(&vgic_dist->dist_lock);
	
	/*
	 * TODO:
	 *
	 * 1. Check if there are any pending interrupts on this CPU.
	 * 2. If there are:
	 * 	a. Check if there are empty lr regs. A lr reg might become
	 * 	empty when the guest disables the interrupt (like with the timer
	 * 	interrupt).
	 * 	b. If there are no more pending interrupts, goto 3. Else,
	 * 	proceed forward.
	 *	c. Enable maintenance interrupts.
	 *	d. Set a variable stating that maintenance interrupts are
	 *	enabled. This will be read by the code in EL2 to check the
	 *	cause for the interrupt.
	 * 3. Else, disable maintenance interrupts.
	 */

	//mtx_unlock_spin(&vgic_dist->dist_lock);
}

/*
 * What's the purpose? Why is it called after exiting the VM, just before
 * calling vgic_v3_flush_hwstate?
 */
void
vgic_v3_sync_hwstate(void *arg)
{
	struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	struct vgic_v3_dist *dist;
	int lr_idx, irq;
	bool level_pending;

	hypctx = arg;
	cpu_if = &hypctx->vgic_cpu_if;
	dist = &hypctx->hyp->vgic_dist;

	//printf("vgic_sync_hwstate\n");

	//level_pending = vgic_process_maintenance(hypctx);
	level_pending = false;

	for_each_set_bit(lr_idx, &cpu_if->ich_elsr_el2, cpu_if->ich_lr_num) {
		irq = cpu_if->ich_lr_el2[lr_idx] & GICH_LR_VIRTID;
		cpu_if->irq_to_lr[irq] = VGIC_ICH_LR_EMPTY;
	}
}

int 
vgic_v3_vcpu_pending_irq(void *arg)
{
	return (0);
}

static inline ssize_t
vgic_v3_get_free_lr(const uint64_t *ich_lr_el2, size_t ich_lr_num)
{
	ssize_t i;

	for (i = 0; i < ich_lr_num; i++)
		if (int_inactive(ich_lr_el2[i]))
			return (i);

	return (-1);
}

int
vgic_v3_remove_irq(void *arg, unsigned int irq, bool ignore_state)
{
        struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	size_t i;

	hypctx = (struct hypctx *)arg;
	cpu_if = &hypctx->vgic_cpu_if;

	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (ICH_LR_EL2_VINTID(cpu_if->ich_lr_el2[i]) == irq &&
		    (ignore_state || int_pending(cpu_if->ich_lr_el2[i])))
			cpu_if->ich_lr_el2[i] &= ~ICH_LR_EL2_STATE_MASK;

	/* TODO check if the interrupt is pending and disable it there too */

	return (0);
}

int
vgic_v3_inject_irq(void *arg, unsigned int irq, bool level)
{
        struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	ssize_t lr_idx;

	hypctx = (struct hypctx *)arg;
	cpu_if = &hypctx->vgic_cpu_if;

	lr_idx = vgic_v3_get_free_lr(cpu_if->ich_lr_el2, cpu_if->ich_lr_num);
	if (lr_idx == -1) {
		/*
		 * TODO:
		 *
		 * 1. Implement pending interrupts.
		 * 2. Check if there are interrupts in the lr regs with a lower
		 * priority, and if so, replace one with irq.
		 * 3. Implement interrupt types.
		 * 4. Check if there are interrupts in the lr regs with the same
		 * priority, but a lower type priority (CLK > anything). If so,
		 * replace one with irq.
		 */
		eprintf("All ICH_LR<n>_EL2 registers are used\n");
		return (0);
	}

	if (lr_idx != 0)
		eprintf("lr_idx = %ld\n", lr_idx);

	cpu_if->ich_lr_el2[lr_idx] = \
	    ICH_LR_EL2_STATE_PENDING | ICH_LR_EL2_GROUP_1 | irq;

        //if (vgic_update_irq_state(hypctx, irq, level))
        //        vgic_kick_vcpus(hypctx->hyp);

        return (0);
}

static int
arm_vgic_detach(device_t dev)
{
	/*
	device_t parent;
	*/
	int error;

	printf("\n[arm64.c:arm_vgic_detach] dev nameunit = %s\n", device_get_nameunit(dev));

	if (softc.vgic_v3_dev == NULL) {
		printf("[arm64.c:arm_vgic_detach] softc.vgic_v3_dev is NULL, returning 0\n");
		return (0);
	}

	softc.vgic_v3_dev = NULL;
	softc.gic_v3_dev = NULL;

	error = 0;
#if 0
	printf("[arm64.c:arm_vgic_detach] before device_get_parent()\n");
	parent = device_get_parent(dev);
	if (parent != NULL) {
		printf("[arm64.c:arm_vgic_detach] before device_delete_child()\n");
		error = device_delete_child(parent, dev);
	}
#endif

	printf("[arm64.c:arm_vgic_detach] returning %d\n", error);
	return (error);
}

static void vgic_v3_set_ro_regs(device_t dev)
{
	device_t gic;
	struct gic_v3_softc *gic_sc;

	gic = device_get_parent(dev);
	gic_sc = device_get_softc(gic);

	/* GICD_ICFGR0 configures SGIs and it is read-only. */
	ro_regs.gicd_icfgr0 = gic_d_read(gic_sc, 4, GICD_ICFGR(0));

	/*
	 * Configure the GIC type register for the guest.
	 *
	 * ~GICD_TYPER_SECURITYEXTN: disable security extensions.
	 * ~GICD_TYPER_DVIS: direct injection for virtual LPIs not supported.
	 * ~GICD_TYPER_LPIS: LPIs not supported.
	 */
	ro_regs.gicd_typer = gic_d_read(gic_sc, 4, GICD_TYPER);
	ro_regs.gicd_typer &= ~GICD_TYPER_SECURITYEXTN;
	ro_regs.gicd_typer &= ~GICD_TYPER_DVIS;
	ro_regs.gicd_typer &= ~GICD_TYPER_LPIS;

	/*
	 * XXX. Guest reads of GICD_PIDR2 should return the same ArchRev as
	 * specified in the guest FDT.
	 */
	ro_regs.gicd_pidr2 = gic_d_read(gic_sc, 4, GICD_PIDR2);
}

void
vgic_v3_init(uint64_t ich_vtr_el2)
{
	uint32_t pribits, prebits;

	pribits = ICH_VTR_EL2_PRIBITS(ich_vtr_el2);
	switch (pribits) {
	case 5:
		virt_features.min_prio = 0xf8;
	case 6:
		virt_features.min_prio = 0xfc;
	case 7:
		virt_features.min_prio = 0xfe;
	case 8:
		virt_features.min_prio = 0xff;
	}

	prebits = ICH_VTR_EL2_PREBITS(ich_vtr_el2);
	switch (prebits) {
	case 5:
		virt_features.ich_ap0r_num = 1;
		virt_features.ich_ap1r_num = 1;
	case 6:
		virt_features.ich_ap0r_num = 2;
		virt_features.ich_ap1r_num = 2;
	case 7:
		virt_features.ich_ap0r_num = 4;
		virt_features.ich_ap1r_num = 4;
	}

	virt_features.ich_lr_num = ICH_VTR_EL2_LISTREGS(ich_vtr_el2);
}

static int
arm_vgic_attach(device_t dev)
{
	vgic_v3_set_ro_regs(dev);

	return (0);
}

static void
arm_vgic_identify(driver_t *driver, device_t parent)
{
	device_t dev = NULL;
	int order;

	printf("[vgic.c:arm_vgic_identify] parent nameunit = %s\n", device_get_nameunit(parent));

	if (softc.vgic_v3_dev == NULL) {
		order = BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE;
		dev = device_add_child_ordered(parent, order, VGIC_V3_DEVNAME, -1);
		if (dev != NULL) {
			printf("[vgic.c:arm_vgic_identify] dev nameunit = %s\n", device_get_nameunit(dev));
			softc.vgic_v3_dev = dev;
			softc.gic_v3_dev = parent;
		} else {
			printf("Cannot create the Virtual Generic Interrupt Controller device.\n");
		}
	}
}

static int
arm_vgic_probe(device_t dev)
{
	printf("[vgic.c:arm_vgic_probe] dev nameunit = %s\n", device_get_nameunit(dev));
	if (softc.vgic_v3_dev == NULL)
		goto error_disable_virtualization;

	device_set_desc(dev, VGIC_V3_DEVSTR);
	return (BUS_PROBE_DEFAULT);

error_disable_virtualization:
	hypmode_enabled = 0;
	printf("Virtualization has been disabled.\n");
	return (ENXIO);
}

static device_method_t arm_vgic_methods[] = {
	DEVMETHOD(device_identify,	arm_vgic_identify),
	DEVMETHOD(device_probe,		arm_vgic_probe),
	DEVMETHOD(device_attach,	arm_vgic_attach),
	DEVMETHOD(device_detach,	arm_vgic_detach),
	DEVMETHOD_END
};

static devclass_t arm_vgic_devclass;

DEFINE_CLASS_1(vgic, arm_vgic_driver, arm_vgic_methods,
    sizeof(struct vgic_v3_softc), gic_v3_driver);

DRIVER_MODULE(vgic, gic, arm_vgic_driver, arm_vgic_devclass, 0, 0);
