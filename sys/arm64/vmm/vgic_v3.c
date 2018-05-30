/*
 * Copyright (C) 2018 Alexandru Elisei <alexandru.elisei@gmail.com>
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

#define	RES0			0UL

#define	PENDING_SIZE_MIN	32
#define	PENDING_SIZE_MAX	(1 << 10)
#define	PENDING_INVALID		(GIC_LAST_SPI + 1)

#define	lr_pending(lr)		\
    (ICH_LR_EL2_STATE(lr) == ICH_LR_EL2_STATE_PENDING)
#define	lr_inactive(lr)	\
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
		goto out;
	}

	if (off == GICR_TYPER) {
		*rval = redist->gicr_typer;
		goto out;
	}

	if (off == GICR_WAKER) {
		/* Redistributor is always awake. */
		*rval = 0 & ~GICR_WAKER_PS & ~GICR_WAKER_CA;
		goto out;
	}

	if (off == GICR_CTLR) {
		/* No writes pending */
		*rval = redist->gicr_ctlr & ~GICR_CTLR_RWP & ~GICR_CTLR_UWP;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_IGROUPR0) {
		*rval = redist->gicr_igroupr0;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ICENABLER0) {
		*rval = redist->gicr_ixenabler0;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ISENABLER0) {
		*rval = redist->gicr_ixenabler0;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR0_BASE) {
		*rval = redist->gicr_icfgr0;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR1_BASE) {
		*rval = redist->gicr_icfgr1;
		goto out;
	}

	if (off >= GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE &&
	    off < redist->gicr_ipriorityr_addr_max) {
		*rval = read_reg(redist->gicr_ipriorityr,
		    GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE, off);
		goto out;
	}

	eprintf("Unknown register offset: 0x%04lx\n", off);
	*rval = RES0;

	/* Return to userland for emulation */
	*retu = true;
	return (0);

out:
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
		goto out;
	}

	if (off == GICR_TYPER) {
		eprintf("Warning: Trying to write to read-only register GICR_TYPER.\n");
		goto out;
	}

	if (off == GICR_WAKER) {
		/*
		 * Ignore writes to GICRR_WAKER. The Redistributor will always
		 * be awake.
		 */
		;
		goto out;
	}

	if (off == GICR_CTLR) {
		redist->gicr_ctlr = val;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_IGROUPR0) {
		redist->gicr_igroupr0 = val;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ICENABLER0) {
		/* A write of 1 to ICENABLER disables the interrupt. */
		redist->gicr_ixenabler0 &= ~val;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ISENABLER0) {
		/* A write of 1 to ISENABLER enables the interrupt */
		redist->gicr_ixenabler0 |= val;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR0_BASE) {
		redist->gicr_icfgr0 = val;
		goto out;
	}

	if (off == GICR_SGI_BASE_SIZE + GICR_ICFGR1_BASE) {
		redist->gicr_icfgr1 = val;
		goto out;
	}

	if (off >= GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE &&
	    off < redist->gicr_ipriorityr_addr_max) {
		write_reg(redist->gicr_ipriorityr,
		    GICR_SGI_BASE_SIZE + GICD_IPRIORITYR_BASE, off, val);
		goto out;
	}

	eprintf("Unknown register offset: 0x%04lx\n", off);

	*retu = true;
	return (0);

out:
	*retu = false;
	return (0);
}

int
vgic_v3_dist_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	struct vgic_v3_redist *redist;
	uint64_t off;
	size_t reg_size, n;
	bool *retu;

	retu = (bool *)arg;
	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;
	redist = &hyp->ctx[vcpuid].vgic_redist;

	/* Offset of distributor register */
	off = fault_ipa - dist->ipa;

	if (off == GICD_CTLR) {
		*rval = dist->gicd_ctlr;
		goto out;
	}

	if (off == GICD_TYPER) {
		*rval = dist->gicd_typer;
		goto out;
	}

	if (off == GICD_IIDR) {
		*rval = RES0;
		goto out;
	}

	if (off == GICD_PIDR2) {
		*rval = dist->gicd_pidr2;
		goto out;
	}

	if (off >= GICD_IGROUPR_BASE && off < dist->gicd_igroupr_addr_max) {
		if (off == GICD_IGROUPR_BASE)
			*rval = redist->gicr_igroupr0;
		else
			*rval = read_reg(dist->gicd_igroupr, GICD_IGROUPR_BASE,
			    off);
		goto out;
	}

	if (off >= GICD_ICFGR_BASE && off < dist->gicd_icfgr_addr_max) {
		*rval = read_reg(dist->gicd_icfgr, GICD_ICFGR_BASE, off);
		goto out;
	}

	if (off >= GICD_IPRIORITYR_BASE && off < dist->gicd_ipriorityr_addr_max) {
		reg_size = sizeof(*dist->gicd_ipriorityr);
		n = (off - GICD_IPRIORITYR_BASE) / reg_size;
		/*
		 * GIC Architecture specification, p 8-483: when affinity
		 * routing is enabled, GICD_IPRIORITYR<n> is RAZ/WI for
		 * n = 0 to 7.
		 */
		if ((dist->gicd_ctlr & GICD_CTLR_ARE_NS) && n <= 7)
			*rval = RES0;
		else
			*rval = read_reg(dist->gicd_ipriorityr,
			    GICD_IPRIORITYR_BASE, off);
		goto out;
	}

	if (off >= GICD_ICENABLER_BASE && off < dist->gicd_icenabler_addr_max) {
		/* GICD_ICENABLER<0> is equivalent to GICR_ICENABLER0 */
		if (off == GICD_ICENABLER_BASE)
			*rval = redist->gicr_ixenabler0;
		else
			*rval = read_reg(dist->gicd_ixenabler,
			    GICD_ICENABLER_BASE, off);
		goto out;
	}

	if (off >= GICD_ISENABLER_BASE && off < dist->gicd_isenabler_addr_max) {
		/* GICD_ISENABLER<0> is equivalent to GICR_ISENABLER0 */
		if (off == GICD_ISENABLER_BASE)
			*rval = redist->gicr_ixenabler0;
		else
			*rval = read_reg(dist->gicd_ixenabler,
			    GICD_ISENABLER_BASE, off);
		goto out;
	}

	if (off >= GICD_IROUTER_BASE && off < dist->gicd_irouter_addr_max) {
		*rval = read_reg(dist->gicd_irouter, GICD_IROUTER_BASE, off);
		goto out;
	}

	eprintf("Unknown register offset: 0x%04lx\n", off);
	*rval = RES0;

	*retu = true;
	return (0);

out:
	*retu = false;
	return (0);
}

int
vgic_v3_dist_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t val,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	struct vgic_v3_redist *redist;
	uint64_t off;
	uint32_t regval;
	size_t n, reg_size;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;
	redist = &hyp->ctx[vcpuid].vgic_redist;

	/* Offset of distributor register. */
	off = fault_ipa - dist->ipa;

	if (off == GICD_CTLR) {
		/* Writes are never pending. */
		dist->gicd_ctlr = val & ~GICD_CTLR_RWP;
		goto out;
	}

	if (off == GICD_TYPER) {
		eprintf("Warning: Trying to write to read-only register GICD_TYPER.\n");
		goto out;
	}

	if (off == GICD_PIDR2) {
		eprintf("Warning: Trying to write to read-only register GICD_PIDR2.\n");
		goto out;
	}

	if (off == GICD_IIDR) {
		eprintf("write: GICD_IIDR not implemented\n");
		goto out;
	}

	if (off >= GICD_IGROUPR_BASE && off < dist->gicd_igroupr_addr_max) {
		if (off == GICD_IGROUPR_BASE)
			redist->gicr_igroupr0 = val;
		else
			write_reg(dist->gicd_igroupr, GICD_IGROUPR_BASE, off,
			    val);
		goto out;
	}

	if (off >= GICD_ICFGR_BASE && off < dist->gicd_icfgr_addr_max) {
		if (off == GICD_ICFGR_BASE)
			eprintf("Warning: Trying to write to read-only register GICD_ICFGR0.\n");
		else
			write_reg(dist->gicd_icfgr, GICD_ICFGR_BASE, off, val);
		goto out;
	}

	if (off >= GICD_IPRIORITYR_BASE && off < dist->gicd_ipriorityr_addr_max) {
		reg_size = sizeof(*dist->gicd_ipriorityr);
		n = (off - GICD_IPRIORITYR_BASE) / reg_size;
		/* See vgic_v3_dist_read() */
		if ((dist->gicd_ctlr & GICD_CTLR_ARE_NS) && n <= 7)
			goto out;
		write_reg(dist->gicd_ipriorityr, GICD_IPRIORITYR_BASE, off, val);
		goto out;
	}

	if (off >= GICD_ICENABLER_BASE && off < dist->gicd_icenabler_addr_max) {
		/* A write of 1 to ICENABLER disables the interrupt. */
		if (off == GICD_ICENABLER_BASE) {
			redist->gicr_ixenabler0 &= ~val;
		} else {
			regval = read_reg(dist->gicd_ixenabler,
			    GICD_ICENABLER_BASE, off);
			regval &= ~val;
			write_reg(dist->gicd_ixenabler, GICD_ICENABLER_BASE,
			    off, regval);
		}
		goto out;
	}

	if (off >= GICD_ISENABLER_BASE && off < dist->gicd_isenabler_addr_max) {
		/* A write of 1 to ISENABLER enables the interrupt. */
		if (off == GICD_ISENABLER_BASE) {
			redist->gicr_ixenabler0 |= val;
		} else {
			regval = read_reg(dist->gicd_ixenabler,
			    GICD_ISENABLER_BASE, off);
			regval |= val;
			write_reg(dist->gicd_ixenabler, GICD_ISENABLER_BASE,
			    off, regval);
		}
		goto out;
	}

	if (off >= GICD_IROUTER_BASE && off < dist->gicd_irouter_addr_max) {
		write_reg(dist->gicd_irouter, GICD_IROUTER_BASE, off, val);
		goto out;
	}

	eprintf("Unknown register offset: 0x%04lx\n", off);

	*retu = true;
	return (0);

out:
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
	mtx_init(&cpu_if->lr_mtx, "VGICv3 ICH_LR_EL2 lock", NULL, MTX_SPIN);

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
	 * ICH_VMCR_EL2_VENG0: virtual Group 0 interrupts enabled.
	 * ICH_VMCR_EL2_VENG1: virtual Group 1 interrupts enabled.
	 */
	cpu_if->ich_vmcr_el2 = \
	    (virt_features.min_prio << ICH_VMCR_EL2_VPMR_SHIFT) | \
	    ICH_VMCR_EL2_VBPR1_NO_PREEMPTION | ICH_VMCR_EL2_VBPR0_NO_PREEMPTION;
	cpu_if->ich_vmcr_el2 &= ~ICH_VMCR_EL2_VEOIM;
	cpu_if->ich_vmcr_el2 |= ICH_VMCR_EL2_VENG0 | ICH_VMCR_EL2_VENG1;

	cpu_if->ich_lr_num = virt_features.ich_lr_num;
	cpu_if->ich_ap0r_num = virt_features.ich_ap0r_num;
	cpu_if->ich_ap1r_num = virt_features.ich_ap1r_num;

	cpu_if->pending = malloc(PENDING_SIZE_MIN * sizeof(*cpu_if->pending),
	    M_VGIC_V3, M_WAITOK | M_ZERO);
	cpu_if->pending_size = PENDING_SIZE_MIN;
	cpu_if->pending_num = 0;
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
vgic_v3_init_dist_regs(struct vgic_v3_dist *dist)
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

	/* Round up the number of registers to the nearest integer. */
	n = (dist->nirqs + 16 - 1) / 16;
	INIT_DIST_REG(gicd_icfgr, n, GICD_ICFGR_BASE, dist);

	n = (dist->nirqs + 32 - 1) / 32;
	INIT_DIST_REG(gicd_igroupr, n, GICD_IGROUPR_BASE, dist);

	/* ARM GIC Architecture Specification, page 8-483. */
	n = 8 * ((dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1);
	INIT_DIST_REG(gicd_ipriorityr, n, GICD_IPRIORITYR_BASE, dist);

	/* ARM GIC Architecture Specification, page 8-485. */
	n = 32 * (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK + 1) - 1;
	INIT_DIST_REG(gicd_irouter, n, GICD_IROUTER_BASE, dist);

	/* ARM GIC Architecture Specification, page 8-471. */
	n = (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1;
	reg_size = sizeof(*dist->gicd_ixenabler);
	dist->gicd_ixenabler = malloc(n * reg_size, M_VGIC_V3,
	    M_WAITOK | M_ZERO);
	dist->gicd_ixenabler_num = n;
	dist->gicd_icenabler_addr_max = GICD_ICENABLER_BASE + n * reg_size;
	dist->gicd_isenabler_addr_max = GICD_ISENABLER_BASE + n * reg_size;
}

void
vgic_v3_vminit(void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;

	hyp = (struct hyp *)arg;
	dist = &hyp->vgic_dist;
	vgic_v3_init_dist_regs(dist);
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
	struct hyp *hyp = arg;
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_cpu_if *cpu_if;
	int i;

	free(dist->gicd_igroupr, M_VGIC_V3);
	free(dist->gicd_icfgr, M_VGIC_V3);
	free(dist->gicd_ipriorityr, M_VGIC_V3);
	free(dist->gicd_ixenabler, M_VGIC_V3);
	free(dist->gicd_irouter, M_VGIC_V3);

	for (i = 0; i < VM_MAXCPU; i++) {
		cpu_if = &hyp->ctx[i].vgic_cpu_if;
		if (cpu_if->pending)
			free(cpu_if->pending, M_VGIC_V3);
	}
}

int
vgic_v3_vcpu_pending_irq(void *arg)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;

	return (cpu_if->pending_num);
}

static int
vgic_v3_remove_pending_unsafe(struct virq *virq, struct vgic_v3_cpu_if *cpu_if)
{
	size_t dest = 0;
	size_t from = cpu_if->pending_num;

	while (dest < cpu_if->pending_num) {
		if (cpu_if->pending[dest].irq == virq->irq) {
			for (from = dest + 1; from < cpu_if->pending_num; from++) {
				if (cpu_if->pending[from].irq == virq->irq)
					continue;
				cpu_if->pending[dest++] = cpu_if->pending[from];
			}
			cpu_if->pending_num = dest;
		} else {
			dest++;
		}
	}

	return (from - dest);
}

int
vgic_v3_deactivate_irq(void *arg, struct virq *virq, bool ignore_state)
{
        struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	size_t i;

	if (virq->irq >= dist->nirqs ||
	    virq->type >= VIRQ_TYPE_INVALID) {
		eprintf("Malformed virq\n");
		return (1);
	}

	mtx_lock_spin(&cpu_if->lr_mtx);

	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (ICH_LR_EL2_VINTID(cpu_if->ich_lr_el2[i]) == virq->irq &&
		    (ignore_state || lr_pending(cpu_if->ich_lr_el2[i])))
			cpu_if->ich_lr_el2[i] &= ~ICH_LR_EL2_STATE_MASK;

	vgic_v3_remove_pending_unsafe(virq, cpu_if);

	mtx_unlock_spin(&cpu_if->lr_mtx);

	return (0);
}

static int
vgic_v3_add_pending_unsafe(struct virq *virq, struct vgic_v3_cpu_if *cpu_if)
{
	struct virq *new_pending, *old_pending;
	size_t new_size;

	if (cpu_if->pending_num == cpu_if->pending_size) {
		/* Double the size of the pending list */
		new_size = cpu_if->pending_size << 1;
		if (new_size > PENDING_SIZE_MAX)
			return (1);

		new_pending = malloc(new_size * sizeof(*cpu_if->pending),
		    M_VGIC_V3, M_WAITOK | M_ZERO);
		memcpy(new_pending, cpu_if->pending,
		    cpu_if->pending_size * sizeof(*virq));

		old_pending = cpu_if->pending;
		cpu_if->pending = new_pending;
		cpu_if->pending_size = new_size;
		free(old_pending, M_VGIC_V3);
	}

	memcpy(&cpu_if->pending[cpu_if->pending_num], virq, sizeof(*virq));
	cpu_if->pending_num++;

	return (0);
}

int
vgic_v3_inject_irq(void *arg, struct virq *virq)
{
        struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	int error;

	if (virq->irq >= dist->nirqs ||
	    virq->type >= VIRQ_TYPE_INVALID) {
		eprintf("Malformed IRQ %u.\n", virq->irq);
		return (1);
	}

	mtx_lock_spin(&cpu_if->lr_mtx);
	error = vgic_v3_add_pending_unsafe(virq, cpu_if);
	if (error)
		eprintf("Unable to mark IRQ %u as pending.\n", virq->irq);
	mtx_unlock_spin(&cpu_if->lr_mtx);

	return (error);
}

static uint8_t
vgic_v3_get_priority(struct virq *virq, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	size_t n;
	uint32_t off, mask;
	uint8_t priority;

	n = virq->irq / 4;
	off = n % 4;
	mask = 0xff << off;
	/*
	 * When affinity routing is enabled, for SGIs and PPIs the
	 * Redistributor registers are used, and for the SPIs the corresponding
	 * Distributor registers. When affinity routing is not enabled, the
	 * Distributor registers are used for all interrupts.
	 */
	if ((dist->gicd_ctlr & GICD_CTLR_ARE_NS) && n <= 7)
		priority = (redist->gicr_ipriorityr[n] & mask) >> off;
	else
		priority = (dist->gicd_ipriorityr[n] & mask) >> off;

	return (priority);
}

static bool
vgic_v3_int_enabled(struct virq *virq, struct hypctx *hypctx, int *group)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	uint32_t irq_off, irq_mask;
	int n;

	irq_off = virq->irq % 32;
	irq_mask = 1 << irq_off;
	n = virq->irq / 32;

	if (n == 0)
		*group = (redist->gicr_igroupr0 & irq_mask) ? 1 : 0;
	else
		*group = (dist->gicd_igroupr[n] & irq_mask) ? 1 : 0;

	/* Check that the interrupt group hasn't been disabled */
	if (*group == 1) {
		if (!(dist->gicd_ctlr & GICD_CTLR_G1A))
			return (false);
		if (!(cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VENG1))
			return (false);
	} else {
		if (!(dist->gicd_ctlr & GICD_CTLR_G1))
			return (false);
		if (!(cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VENG0))
			return (false);
	}

	/* Check that the interrupt ID hasn't been disabled */
	if (virq->irq <= GIC_LAST_PPI) {
		if (!(redist->gicr_ixenabler0 & irq_mask))
			return (false);
	} else {
		if (!(dist->gicd_ixenabler[n] & irq_mask))
			return (false);
	}

	return (true);
}

static struct virq *
vgic_v3_highest_priority_pending(struct vgic_v3_cpu_if *cpu_if,
    struct hypctx *hypctx, int *group)
{
	int i, max_idx;
	uint8_t priority, max_priority;
	uint8_t vpmr;
	bool enabled;

	vpmr = (cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VPMR_MASK) >> \
	    ICH_VMCR_EL2_VPMR_SHIFT;

	max_idx = -1;
	max_priority = 0xff;
	for (i = 0; i < cpu_if->pending_num; i++) {
		/* Check that the interrupt hasn't been already scheduled */
		if (cpu_if->pending[i].irq == PENDING_INVALID)
			continue;

		enabled = vgic_v3_int_enabled(&cpu_if->pending[i], hypctx,
		    group);
		if (!enabled)
			continue;

		priority = vgic_v3_get_priority(&cpu_if->pending[i], hypctx);
		if (priority >= vpmr)
			continue;

		/* XXX Interrupt preemption not supported. */

		if (max_idx == -1) {
			max_idx = i;
			max_priority = priority;
		} else if (priority > max_priority) {
			max_idx = i;
			max_priority = priority;
		} else if (priority == max_priority &&
		    cpu_if->pending[i].type < cpu_if->pending[max_idx].type) {
			max_idx = i;
			max_priority = priority;
		}
	}

	if (max_idx == -1)
		return (NULL);
	return (&cpu_if->pending[max_idx]);
}

void
vgic_v3_sync_hwstate(void *arg)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct virq *virq;
	struct virq invalid_virq, tmp;
	int group;
	int i;
	int error;

	mtx_lock_spin(&cpu_if->lr_mtx);

	if (cpu_if->pending_num == 0)
		goto out;

	invalid_virq.irq = PENDING_INVALID;
	invalid_virq.type = VIRQ_TYPE_INVALID;

	/*
	 * Add all interrupts from the list registers that are not active to
	 * pending buffer to be rescheduled in the next step.
	 */
	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (lr_pending(cpu_if->ich_lr_el2[i])) {
			tmp.irq = cpu_if->ich_lr_el2[i] & ICH_LR_EL2_VINTID_MASK;
			tmp.type = VIRQ_TYPE_MAXPRIO;
			error = vgic_v3_add_pending_unsafe(&tmp, cpu_if);
			if (error)
				goto out;
			cpu_if->ich_lr_el2[i] &= ~ICH_LR_EL2_STATE_MASK;
		}

	for (i = 0; i < cpu_if->ich_lr_num; i++) {
		if (!lr_inactive(cpu_if->ich_lr_el2[i]))
			continue;

		virq = vgic_v3_highest_priority_pending(cpu_if, hypctx, &group);
		if (virq == NULL)
			/* No more pending interrupts */
			break;

		cpu_if->ich_lr_el2[i] = ICH_LR_EL2_STATE_PENDING | \
		    ((uint64_t)group << ICH_LR_EL2_GROUP_SHIFT) | virq->irq;
		/* Mark the scheduled pending interrupt as invalid */
		*virq = invalid_virq;
	}
	/* Remove all scheduled interrupts */
	vgic_v3_remove_pending_unsafe(&invalid_virq, cpu_if);

out:
	mtx_unlock_spin(&cpu_if->lr_mtx);

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
