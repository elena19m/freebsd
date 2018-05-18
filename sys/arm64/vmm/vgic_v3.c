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

MALLOC_DEFINE(M_VGIC_V3, "ARM VMM VGIC V3", "ARM VMM VGIC V3");

extern uint64_t hypmode_enabled;

struct vgic_v3_virt_features {
	size_t lr_num;
	uint32_t prebits;
	uint32_t pribits;
	uint8_t min_prio;
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

static void vgic_bitmap_set_irq_val(uint32_t *irq_prv,
    uint32_t *irq_shr, int irq, int val);
static void vgic_update_state(struct hyp *hyp);
static void vgic_retire_disabled_irqs(struct hypctx *hypctx);
static void vgic_dispatch_sgi(struct hypctx *hypctx);

#if 0
/*
 * TODO
 */
static uint32_t vgic_dist_conf_expand(uint16_t val)
{
	uint32_t res;
	int i;

	res = 0;

	for (i = 0; i < 16; ++i) {
		res |= (val & 1) << (2 * i + 1);
		val = val >> 1;
	}

	return res;
}

static uint16_t vgic_dist_conf_compress(uint32_t val)
{
	uint32_t res;
	int i;

	res = 0;

	for (i = 0; i < 16; ++i) {
		val = val >> 1;
		res |= (val & 1) << i;
		val = val >> 1;
	}

	return res;
}
#endif

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
		eprintf("read: " #arr "<%zd>\n", idx);				\
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
		eprintf("read: GICR_PIDR2\n");
		/* GICR_PIDR2 has the same value as GICD_PIDR2 */
		*rval = dist->gicd_pidr2;

	} else if (off == GICR_TYPER) {
		eprintf("read: GICR_TYPER\n");
		*rval = redist->gicr_typer;

	} else if (off == GICR_WAKER) {
		eprintf("read: GICR_WAKER\n");
		/* Redistributor is always awake. */
		*rval = 0 & ~GICR_WAKER_PS & ~GICR_WAKER_CA;

	} else if (off == GICR_CTLR) {
		eprintf("read: GICR_CTLR\n");
		*rval = redist->gicr_ctlr;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_IGROUPR0) {
		eprintf("read: GICR_IGROUPR0\n");
		*rval = redist->gicr_igroupr0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICENABLER0) {
		eprintf("read: GICR_ICENABLER0\n");
		*rval = redist->gicr_icenabler0_isenabler0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ISENABLER0) {
		eprintf("read: GICR_ISENABLER0\n");
		*rval = redist->gicr_icenabler0_isenabler0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR0_BASE) {
		eprintf("read: GICR_ICFGR0\n");
		*rval = redist->gicr_icfgr0;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR1_BASE) {
		eprintf("read: GICR_ICFGR1\n");
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
		eprintf("write: " #reg "<%zd>\n", idx);				\
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
		eprintf("write: GICR_WAKER\n");

	} else if (off == GICR_CTLR) {
		eprintf("write: GICR_CTLR\n");
		/* Writes are never pending. */
		redist->gicr_ctlr = val & ~GICR_CTLR_RWP;;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_IGROUPR0) {
		eprintf("write: GICR_IGROUPR0\n");
		redist->gicr_igroupr0 = val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICENABLER0) {
		eprintf("write: GICR_ICENABLER0\n");
		/* A write of 1 to ICENABLER disables the interrupt. */
		redist->gicr_icenabler0_isenabler0 &= ~val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ISENABLER0) {
		eprintf("write: GICR_ISENABLER0\n");
		/* A write of 1 to ISENABLER enables the interrupt */
		redist->gicr_icenabler0_isenabler0 |= val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR0_BASE) {
		eprintf("write: GICR_ICFGR0\n");
		redist->gicr_icfgr0 = val;

	} else if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR1_BASE) {
		eprintf("write: GICR_ICFGR1\n");
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
		eprintf("read: GICD_CTLR\n");
		*rval = dist->gicd_ctlr;

	} else if (off == GICD_TYPER) {
		eprintf("read: GICD_TYPER\n");
		*rval = dist->gicd_typer;

	} else if (off == GICD_IIDR) {
		eprintf("read: GICD_IIDR not implemented\n");
		*rval = RES0;

	} else if (off == GICD_PIDR2) {
		eprintf("read: GICD_PIDR2\n");
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
		eprintf("write: GICD_CTLR\n");

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

	/* TODO: set this up correctly? */
	redist->gicr_ctlr = 0;

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
	 * ~ICH_VMCR_EL2_VEOIM: writes to EOI registers perform priority drop
	 * and interrupt deactivation.
	 * ICH_VMCR_EL2_VENG1: virtual Group 1 interrupts enabled.
	 */
	cpu_if->ich_vmcr_el2 = \
	    (virt_features.min_prio << ICH_VMCR_EL2_VPMR_SHIFT) | \
	    ICH_VMCR_EL2_VBPR1_NO_PREEMPTION;
	cpu_if->ich_vmcr_el2 &= ~ICH_VMCR_EL2_VEOIM;
	cpu_if->ich_vmcr_el2 |= ICH_VMCR_EL2_VENG1;
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

	/* Distributor is disabled at start, the guest will configure it. */
	dist->gicd_ctlr = GICD_CTLR_RES0;
	dist->gicd_typer = ro_regs.gicd_typer;
	dist->gicd_pidr2 = ro_regs.gicd_pidr2;

	dist->nirqs = GICD_TYPER_I_NUM(dist->gicd_typer);

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

static int
vgic_bitmap_get_irq_val(uint32_t *irq_prv, uint32_t *irq_shr, int irq)
{
	if (irq < VGIC_PRV_I_NUM)
		return bit_test((bitstr_t *)irq_prv, irq);

	return bit_test((bitstr_t *)irq_shr, irq - VGIC_PRV_I_NUM);
}

static void
vgic_bitmap_set_irq_val(uint32_t *irq_prv, uint32_t *irq_shr, int irq, int val)
{
	uint32_t *reg;

	if (irq < VGIC_PRV_I_NUM) {
		reg = irq_prv;
	} else {
		reg = irq_shr;
		irq -= VGIC_PRV_I_NUM;
	}

	if (val)
		bit_set((bitstr_t *)reg, irq);
	else
		bit_clear((bitstr_t *)reg, irq);
}

static bool
vgic_irq_is_edge(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	int irq_val;

	irq_val = vgic_bitmap_get_irq_val(dist->irq_conf_prv[hypctx->vcpu],
					dist->irq_conf_shr, irq);
	return irq_val == VGIC_CFG_EDGE;
}

static int
vgic_irq_is_enabled(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	return vgic_bitmap_get_irq_val(dist->irq_enabled_prv[hypctx->vcpu],
					dist->irq_enabled_shr, irq);
}

static int
vgic_irq_is_active(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	return vgic_bitmap_get_irq_val(dist->irq_active_prv[hypctx->vcpu],
					dist->irq_active_shr, irq);
}

static void
vgic_irq_set_active(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	vgic_bitmap_set_irq_val(dist->irq_active_prv[hypctx->vcpu],
					dist->irq_active_shr, irq, 1);
}

static void
vgic_irq_clear_active(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	vgic_bitmap_set_irq_val(dist->irq_active_prv[hypctx->vcpu],
					dist->irq_active_shr, irq, 0);
}

static int
vgic_dist_irq_is_pending(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	return vgic_bitmap_get_irq_val(dist->irq_state_prv[hypctx->vcpu],
					dist->irq_state_shr, irq);
}

static void
vgic_dist_irq_set(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	vgic_bitmap_set_irq_val(dist->irq_state_prv[hypctx->vcpu],
					dist->irq_state_shr, irq, 1);
}

static void
vgic_dist_irq_clear(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	vgic_bitmap_set_irq_val(dist->irq_state_prv[hypctx->vcpu],
					dist->irq_state_shr, irq, 0);
}

static void
vgic_cpu_irq_set(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;

	if (irq < VGIC_PRV_I_NUM)
		bit_set((bitstr_t *)cpu_if->pending_prv, irq);
	else
		bit_set((bitstr_t *)cpu_if->pending_shr,
				irq - VGIC_PRV_I_NUM);
}

static void
vgic_cpu_irq_clear(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;

	if (irq < VGIC_PRV_I_NUM)
		bit_clear((bitstr_t *)cpu_if->pending_prv, irq);
	else
		bit_clear((bitstr_t *)cpu_if->pending_shr,
				irq - VGIC_PRV_I_NUM);
}

static int
compute_pending_for_cpu(struct hyp *hyp, int vcpu)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_cpu_if *cpu_if = &hyp->ctx[vcpu].vgic_cpu_if;

	uint32_t *pending, *enabled, *pend_percpu, *pend_shared, *target;
	int32_t pending_private, pending_shared;

	pend_percpu = cpu_if->pending_prv;
	pend_shared = cpu_if->pending_shr;

	pending = dist->irq_state_prv[vcpu];
	enabled = dist->irq_enabled_prv[vcpu];
	bitstr_and((bitstr_t *)pend_percpu, (bitstr_t *)pending,
		       (bitstr_t *)enabled, VGIC_PRV_I_NUM);

	pending = dist->irq_state_shr;
	enabled = dist->irq_enabled_shr;
	target = dist->irq_target_shr;
	bitstr_and((bitstr_t *)pend_shared, (bitstr_t *)pending,
		       (bitstr_t *)enabled, VGIC_SHR_I_NUM);
	bitstr_and((bitstr_t *)pend_shared, (bitstr_t *)pend_shared,
		       (bitstr_t *)target, VGIC_SHR_I_NUM);

	bit_ffs((bitstr_t *)pend_percpu, VGIC_PRV_I_NUM, &pending_private);
	bit_ffs((bitstr_t *)pend_shared, VGIC_SHR_I_NUM, &pending_shared);

	return (pending_private > -1 || pending_shared > -1);
}

/*
 * TODO
 */
#if 0
static void
vgic_dispatch_sgi(struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	// TODO Get actual number of cpus on current machine
	int vcpu_num = VM_MAXCPU;
	int sgi, mode, cpu;
	uint8_t targets;

	sgi = dist->sgir & 0xf;
	targets = (dist->sgir >> 16) & 0xff;
	mode = (dist->sgir >> 24) & 3;

	switch (mode) {
	case 0:
		if (!targets)
			return;

	case 1:
		targets = ((1 << vcpu_num) - 1) & ~(1 << hypctx->vcpu) & 0xff;
		break;

	case 2:
		targets = 1 << hypctx->vcpu;
		break;
	}

	for (cpu = 0; cpu < vcpu_num; ++cpu) {
		if ((targets >> cpu) & 1) {
			vgic_dist_irq_set(hypctx, sgi);
			vgic_dist->irq_sgi_source[cpu][sgi] |= 1 << hypctx->vcpu;
			//printf("SGI%d from CPU%d to CPU%d\n", sgi, vcpu_id, c);
		}
	}
}
#endif

/*
 * TODO
 */
#if 0
static void
vgic_update_state(struct hyp *hyp)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	int cpu;

	//mtx_lock_spin(&vgic_dist->dist_lock);

	if (!dist->enabled) {
		bit_set((bitstr_t *)&dist->irq_pending_on_cpu, 0);
		goto end;
	}

	// TODO Get actual number of cpus on current machine
	for (cpu = 0; cpu < VM_MAXCPU; ++cpu) {
		if (compute_pending_for_cpu(hyp, cpu)) {
			printf("CPU%d has pending interrupts\n", cpu);
			bit_set((bitstr_t *)&dist->irq_pending_on_cpu, cpu);
		}
	}

end:
	;//mtx_unlock_spin(&dist->dist_lock);
}
#endif

#define LR_CPUID(lr)	\
	(((lr) & GICH_LR_PHYSID_CPUID) >> GICH_LR_PHYSID_CPUID_SHIFT)
#define MK_LR_PEND(src, irq)	\
	(GICH_LR_PENDING | ((src) << GICH_LR_PHYSID_CPUID_SHIFT) | (irq))

/*
 * TODO
 */
#if 0
static void
vgic_retire_disabled_irqs(struct hypctx *hypctx)
{
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	int lr_idx;

	for_each_set_bit(lr_idx, cpu_if->lr_used, cpu_if->lr_num) {

		int irq = cpu_if->lr[lr_idx] & GICH_LR_VIRTID;

		if (!vgic_irq_is_enabled(hypctx, irq)) {
			cpu_if->irq_to_lr[irq] = VGIC_LR_EMPTY;
			bit_clear((bitstr_t *)cpu_if->lr_used, lr_idx);
			cpu_if->ich_lr_el2[lr_idx] &= ~GICH_LR_STATE;
			if (vgic_irq_is_active(hypctx, irq))
				vgic_irq_clear_active(hypctx, irq);
		}
	}
}
#endif

static bool
vgic_queue_irq(struct hypctx *hypctx, uint8_t sgi_source_cpu, int irq)
{
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	int lr_idx;

	//printf("Queue IRQ%d\n", irq);

	lr_idx = cpu_if->irq_to_lr[irq];

	if (lr_idx != VGIC_LR_EMPTY &&
	    (LR_CPUID(cpu_if->ich_lr_el2[lr_idx]) == sgi_source_cpu)) {

		//printf("LR%d piggyback for IRQ%d %x\n", lr, irq, cpu_if->vgic_lr[lr]);

		cpu_if->ich_lr_el2[lr_idx] |= GICH_LR_PENDING;

		goto end;
	}

	bit_ffc((bitstr_t *)cpu_if->lr_used, cpu_if->lr_num, &lr_idx);
	if (lr_idx == -1)
		return false;

	//printf("LR%d allocated for IRQ%d %x\n", lr, irq, sgi_source_id);
	cpu_if->ich_lr_el2[lr_idx] = MK_LR_PEND(sgi_source_cpu, irq);
	cpu_if->irq_to_lr[irq] = lr_idx;
	bit_set((bitstr_t *)cpu_if->lr_used, lr_idx);

end:
	if (!vgic_irq_is_edge(hypctx, irq))
		cpu_if->ich_lr_el2[lr_idx] |= GICH_LR_EOI;

	return true;
}

static bool
vgic_queue_sgi(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	uint8_t source;
	int cpu;

	source = dist->irq_sgi_source[hypctx->vcpu][irq];

	for_each_set_bit(cpu, &source, VGIC_MAXCPU) {
		if (vgic_queue_irq(hypctx, cpu, irq))
			bit_clear((bitstr_t *)&source, cpu);
	}

	dist->irq_sgi_source[hypctx->vcpu][irq] = source;

	if (!source) {
		vgic_dist_irq_clear(hypctx, irq);
		vgic_cpu_irq_clear(hypctx, irq);
		return true;
	}

	return false;
}

static bool
vgic_queue_hwirq(struct hypctx *hypctx, int irq)
{
	if (vgic_irq_is_active(hypctx, irq))
		return true; /* already queued */

	if (vgic_queue_irq(hypctx, 0, irq)) {
		if (vgic_irq_is_edge(hypctx, irq)) {
			vgic_dist_irq_clear(hypctx, irq);
			vgic_cpu_irq_clear(hypctx, irq);
		} else {
			vgic_irq_set_active(hypctx, irq);
		}

		return true;
	}

	return false;
}

static bool
vgic_process_maintenance(struct hypctx *hypctx)
{
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	int lr_idx, irq;
	bool level_pending = false;

	//printf("MISR = %08x\n", vgic->misr);

	if (cpu_if->ich_misr_el2 & GICH_MISR_EOI) {

		for_each_set_bit(lr_idx, &cpu_if->ich_eisr_el2, cpu_if->lr_num) {

			irq = cpu_if->ich_lr_el2[lr_idx] & GICH_LR_VIRTID;

			vgic_irq_clear_active(hypctx, irq);
			cpu_if->ich_lr_el2[lr_idx] &= ~GICH_LR_EOI;

			if (vgic_dist_irq_is_pending(hypctx, irq)) {
				vgic_cpu_irq_set(hypctx, irq);
				level_pending = true;
			} else {
				vgic_cpu_irq_clear(hypctx, irq);
			}
		}
	}

	if (cpu_if->ich_misr_el2 & GICH_MISR_U)
		cpu_if->ich_hcr_el2 &= ~GICH_HCR_UIE;

	return level_pending;
}

void
vgic_v3_flush_hwstate(void *arg)
{
	struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	struct vgic_v3_dist *dist;
	int i, overflow = 0;

	hypctx = arg;
	cpu_if = &hypctx->vgic_cpu_if;
	dist = &hypctx->hyp->vgic_dist;

	//printf("vgic_flush_hwstate\n");

	//mtx_lock_spin(&vgic_dist->dist_lock);

	if (!vgic_v3_vcpu_pending_irq(hypctx)) {
		//printf("CPU%d has no pending interrupt\n", hypctx->vcpu);
		goto end;
	}

	/* SGIs */
	/* TODO - check bounds for i */
	i = GIC_FIRST_SGI;
	for_each_set_bit_from(i, cpu_if->pending_prv, GIC_LAST_SGI + 1) {
		//printf("Pending SGI %d\n", i);
		if (!vgic_queue_sgi(hypctx, i))
			overflow = 1;
	}

	/* PPIs */
	i = GIC_FIRST_PPI;
	for_each_set_bit_from(i, cpu_if->pending_prv, GIC_LAST_PPI + 1) {
		//printf("Pending PPI %d\n", i);
		if (!vgic_queue_hwirq(hypctx, i))
			overflow = 1;
	}

	/* SPIs */
	i = 0;
	for_each_set_bit(i, cpu_if->pending_shr, VGIC_SPI_NUM) {
		//printf("Pending SPI %d\n", i);
		if (!vgic_queue_hwirq(hypctx, i + VGIC_PRV_I_NUM))
			overflow = 1;
	}

end:
	if (overflow) {
		cpu_if->ich_hcr_el2 |= GICH_HCR_UIE;
	} else {
		cpu_if->ich_hcr_el2 &= ~GICH_HCR_UIE;
		bit_clear((bitstr_t *)&dist->irq_pending_on_cpu, hypctx->vcpu);
	}
	//mtx_unlock_spin(&vgic_dist->dist_lock);
}

void
vgic_v3_sync_hwstate(void *arg)
{
	struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	struct vgic_v3_dist *dist;
	int lr_idx, pending, irq;
	bool level_pending;

	hypctx = arg;
	cpu_if = &hypctx->vgic_cpu_if;
	dist = &hypctx->hyp->vgic_dist;

	//printf("vgic_sync_hwstate\n");

	level_pending = vgic_process_maintenance(hypctx);

	for_each_set_bit(lr_idx, &cpu_if->ich_elsr_el2, cpu_if->lr_num) {

		if (!bit_test_and_clear((bitstr_t *)cpu_if->lr_used, lr_idx))
			continue;

		irq = cpu_if->ich_lr_el2[lr_idx] & GICH_LR_VIRTID;
		cpu_if->irq_to_lr[irq] = VGIC_LR_EMPTY;
	}

	bit_ffc((bitstr_t *)&cpu_if->ich_elsr_el2, cpu_if->lr_num, &pending);
	if (level_pending || pending > -1)
		bit_set((bitstr_t *)&dist->irq_pending_on_cpu, hypctx->vcpu);
}

int
vgic_v3_vcpu_pending_irq(void *arg)
{
	struct hypctx *hypctx;
	struct vgic_v3_dist *dist;

	hypctx = arg;
	dist = &hypctx->hyp->vgic_dist;

	return bit_test((bitstr_t *)&dist->irq_pending_on_cpu, hypctx->vcpu);
}

static int
vgic_validate_injection(struct hypctx *hypctx, int irq, int level)
{
        int is_edge = vgic_irq_is_edge(hypctx, irq);
        int state = vgic_dist_irq_is_pending(hypctx, irq);

        return (is_edge ? (level > state) : (level != state));
}

static bool
vgic_update_irq_state(struct hypctx *hypctx, unsigned int irq, bool level)
{
        struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
        int is_edge, cpu = hypctx->vcpu;
        int enabled;
        bool ret = true;

        //mtx_lock_spin(&vgic_dist->dist_lock);

        is_edge = vgic_irq_is_edge(hypctx, irq);

        if (!vgic_validate_injection(hypctx, irq, level)) {
                ret = false;
                goto end;
        }

        if (irq >= VGIC_PRV_I_NUM) {
                cpu = 0;//vgic_dist->irq_spi_cpu[irq - VGIC_PRV_I_NUM];
                hypctx = &hypctx->hyp->ctx[cpu];
        }

        //printf("Inject IRQ%d level %d CPU%d\n", irq, level, cpu);

        if (level)
                vgic_dist_irq_set(hypctx, irq);
        else
                vgic_dist_irq_clear(hypctx, irq);

        enabled = vgic_irq_is_enabled(hypctx, irq);

        if (!enabled) {
                ret = false;
                goto end;
        }

        if (!is_edge && vgic_irq_is_active(hypctx, irq)) {
                ret = false;
                goto end;
        }

        if (level) {
                vgic_cpu_irq_set(hypctx, irq);
                bit_set((bitstr_t *)&dist->irq_pending_on_cpu, cpu);
        }

end:
        //mtx_unlock_spin(&vgic_dist->dist_lock);

        return ret;
}

static void
vgic_kick_vcpus(struct hyp *hyp)
{
        int cpu;

        for (cpu = 0; cpu < VGIC_MAXCPU; ++cpu) {
                if (vgic_v3_vcpu_pending_irq(&hyp->ctx[cpu]))
                        ;//TODO kick vcpu
        }
}

int
vgic_v3_inject_irq(void *arg, unsigned int irq, bool level)
{
        struct hypctx *hypctx = arg;

        //printf("Injecting %d\n", irq);
        if (vgic_update_irq_state(hypctx, irq, level))
                vgic_kick_vcpus(hypctx->hyp);

        return 0;
}

static int
arm_vgic_maintenance_intr(void *arg)
{

	struct vgic_v3_softc *vgic_sc;
	struct arm_gic_softc *gic_sc;
	int maintenance_intr;

	vgic_sc = arg;
	gic_sc = device_get_softc(vgic_sc->gic_v3_dev);

	/*
	maintenance_intr = bus_space_read_4(gic_sc->gic_h_bst,
					    gic_sc->gic_h_bsh, GICH_MISR);

					    */
	maintenance_intr = 0;
	printf("%s: %x\n", __func__, maintenance_intr);

	return (FILTER_HANDLED);
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
	virt_features.pribits = ICH_VTR_EL2_PRIBITS(ich_vtr_el2);
	switch (virt_features.pribits) {
	case 5:
		virt_features.min_prio = 0xf8;
	case 6:
		virt_features.min_prio = 0xfc;
	case 7:
		virt_features.min_prio = 0xfe;
	case 8:
		virt_features.min_prio = 0xff;
	}

	virt_features.prebits = ICH_VTR_EL2_PREBITS(ich_vtr_el2);
	virt_features.lr_num = ICH_VTR_EL2_LISTREGS(ich_vtr_el2);
}

static int
arm_vgic_attach(device_t dev)
{
	int error;

	vgic_v3_set_ro_regs(dev);

	softc.maintenance_int_res = gic_get_maintenance_intr_res(dev);
	/*
	 * TODO: gic_v3.c registers interrupts by using intr_isrc_register()
	 */
	error = bus_setup_intr(dev, softc.maintenance_int_res,
			INTR_TYPE_CLK | INTR_MPSAFE,
			arm_vgic_maintenance_intr, NULL,
			&softc, &softc.maintenance_int_cookie);
	if (error) {
		device_printf(dev, "Cannot set up the Maintenance Interrupt.\n");
		//goto error_disable_virtualization;
		device_printf(dev, "Ignoring error %d\n", error);
	}

	return (0);

	printf("[vgic.c:arm_vgic_attach] Error happened.\n");

	/*
error_disable_virtualization:
	hypmode_enabled = 0;
	printf("Virtualization has been disabled.\n");
	return (ENXIO);
	*/
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
