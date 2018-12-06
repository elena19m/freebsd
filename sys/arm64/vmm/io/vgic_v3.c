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
#include <arm/arm/generic_timer.h>
#include <arm64/arm64/gic_v3_reg.h>
#include <arm64/arm64/gic_v3_var.h>

#include <arm64/vmm/hyp.h>
#include <arm64/vmm/mmu.h>
#include <arm64/vmm/arm64.h>

#include "vgic_v3.h"
#include "vgic_v3_reg.h"

#define VGIC_V3_DEVNAME		"vgic"
#define VGIC_V3_DEVSTR		"ARM Virtual Generic Interrupt Controller v3"

#define	RES0			0UL

#define	IRQBUF_SIZE_MIN	32
#define	IRQ_SIZE_MAX	(1 << 10)

#define	IRQ_SCHEDULED		(GIC_LAST_SPI + 1)

#define	lr_pending(lr)		\
    (ICH_LR_EL2_STATE(lr) == ICH_LR_EL2_STATE_PENDING)
#define	lr_inactive(lr)	\
    (ICH_LR_EL2_STATE(lr) == ICH_LR_EL2_STATE_INACTIVE)
#define	lr_not_active(lr) (lr_pending(lr) || lr_inactive(lr))

#define	lr_clear_irq(lr) ((lr) &= ~ICH_LR_EL2_STATE_MASK)

MALLOC_DEFINE(M_VGIC_V3, "ARM VMM VGIC V3", "ARM VMM VGIC V3");

struct vgic_v3_virt_features {
	uint8_t min_prio;
	size_t ich_lr_num;
	size_t ich_ap0r_num;
	size_t ich_ap1r_num;
};

struct vgic_v3_ro_regs {
	uint32_t gicd_icfgr0;
	uint32_t gicd_pidr2;
	uint32_t gicd_typer;
};

struct vgic_v3_irq {
	uint32_t irq;
	enum vgic_v3_irqtype irqtype;
	uint8_t group;
	uint8_t enabled;
	uint8_t priority;
};

#define	vip_to_lr(vip, lr)						\
do {									\
	lr = ICH_LR_EL2_STATE_PENDING;					\
	lr |= (uint64_t)vip->group << ICH_LR_EL2_GROUP_SHIFT;		\
	lr |= (uint64_t)vip->priority << ICH_LR_EL2_PRIO_SHIFT;		\
	lr |= vip->irq;							\
} while (0)

#define	lr_to_vip(lr, vip)						\
do {									\
	(vip)->irq = ICH_LR_EL2_VINTID(lr);				\
	(vip)->priority = \
	    (uint8_t)(((lr) & ICH_LR_EL2_PRIO_MASK) >> ICH_LR_EL2_PRIO_SHIFT); \
	(vip)->group = (uint8_t)(((lr) >> ICH_LR_EL2_GROUP_SHIFT) & 0x1); \
} while (0)

static struct vgic_v3_virt_features virt_features;
static struct vgic_v3_ro_regs ro_regs;

static struct gic_v3_softc *gic_sc;
static struct arm_tmr_softc *tmr_sc;
static device_t tmr_dev;

#include "vtimer.h"

void
vgic_v3_cpuinit(void *arg, bool last_vcpu)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	uint64_t aff, vmpidr_el2;
	int error;
	int i;

	error = bus_setup_intr(tmr_dev, tmr_sc->res[2], INTR_TYPE_CLK,
	    vtimer_virtual_timer_intr, NULL, hypctx, &tmr_sc->ihl[2]);
	if (error) {
		printf("Unable to set up the virtual timer interrupt handler\n");
		/* XXX Fallback to physical timer emulation or is it too late? */
		return;
	}

	vmpidr_el2 = hypctx->vmpidr_el2;
	/*
	 * Get affinity for the current CPU. The guest CPU affinity is taken
	 * from VMPIDR_EL2. The Redistributor corresponding to this CPU is
	 * the Redistributor with the same affinity from GICR_TYPER.
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
	for (i = 0; i < cpu_if->ich_lr_num; i++)
		cpu_if->ich_lr_el2[i] = 0UL;

	cpu_if->ich_ap0r_num = virt_features.ich_ap0r_num;
	cpu_if->ich_ap1r_num = virt_features.ich_ap1r_num;

	cpu_if->irqbuf = malloc(IRQBUF_SIZE_MIN * sizeof(*cpu_if->irqbuf),
	    M_VGIC_V3, M_WAITOK | M_ZERO);
	cpu_if->irqbuf_size = IRQBUF_SIZE_MIN;
	cpu_if->irqbuf_num = 0;
}

void
vgic_v3_vminit(void *arg)
{
	struct hyp *hyp = arg;
	struct vgic_v3_dist *dist = &hyp->vgic_dist;

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

	mtx_init(&dist->dist_mtx, "VGICv3 Distributor lock", NULL, MTX_SPIN);
}

int
vgic_v3_attach_to_vm(void *arg, uint64_t dist_start, size_t dist_size,
    uint64_t redist_start, size_t redist_size)
{
	struct hyp *hyp = arg;
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_redist *redist;
	int i;

	/* Set the distributor address and size for trapping guest access. */
	dist->start = dist_start;
	dist->end = dist_start + dist_size;

	for (i = 0; i < VM_MAXCPU; i++) {
		redist = &hyp->ctx[i].vgic_redist;
		/* Set the redistributor address and size. */
		redist->start = redist_start;
		redist->end = redist_start + redist_size;
	}
	vgic_v3_mmio_init(hyp);

	hyp->vgic_attached = true;

	return (0);
}

/* TODO: call this on VM destroy. */
void
vgic_v3_detach_from_vm(void *arg)
{
	struct hyp *hyp = arg;

	vgic_v3_mmio_destroy(hyp);
}

int
vgic_v3_vcpu_pending_irq(void *arg)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;

	return (cpu_if->irqbuf_num);
}

/* Removes ALL instances of interrupt 'irq' */
static int
vgic_v3_irqbuf_remove_nolock(uint32_t irq, struct vgic_v3_cpu_if *cpu_if)
{
	size_t dest = 0;
	size_t from = cpu_if->irqbuf_num;

	while (dest < cpu_if->irqbuf_num) {
		if (cpu_if->irqbuf[dest].irq == irq) {
			for (from = dest + 1; from < cpu_if->irqbuf_num; from++) {
				if (cpu_if->irqbuf[from].irq == irq)
					continue;
				cpu_if->irqbuf[dest++] = cpu_if->irqbuf[from];
			}
			cpu_if->irqbuf_num = dest;
		} else {
			dest++;
		}
	}

	return (from - dest);
}

int
vgic_v3_remove_irq(void *arg, uint32_t irq, bool ignore_state)
{
        struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	size_t i;

	if (irq >= dist->nirqs) {
		eprintf("Malformed IRQ %u.\n", irq);
		return (1);
	}

	mtx_lock_spin(&cpu_if->lr_mtx);

	for (i = 0; i < cpu_if->ich_lr_num; i++) {
		if (ICH_LR_EL2_VINTID(cpu_if->ich_lr_el2[i]) == irq &&
		    (lr_not_active(cpu_if->ich_lr_el2[i]) || ignore_state))
			lr_clear_irq(cpu_if->ich_lr_el2[i]);
	}
	vgic_v3_irqbuf_remove_nolock(irq, cpu_if);

	mtx_unlock_spin(&cpu_if->lr_mtx);

	return (0);
}

static struct vgic_v3_irq *
vgic_v3_irqbuf_add_nolock(struct vgic_v3_cpu_if *cpu_if)
{
	struct vgic_v3_irq *new_irqbuf, *old_irqbuf;
	size_t new_size;

	if (cpu_if->irqbuf_num == cpu_if->irqbuf_size) {
		/* Double the size of the buffered interrupts list */
		new_size = cpu_if->irqbuf_size << 1;
		if (new_size > IRQ_SIZE_MAX)
			return (NULL);

		new_irqbuf = NULL;
		/* TODO: malloc sleeps here and causes a panic */
		while (new_irqbuf == NULL)
			new_irqbuf = malloc(new_size * sizeof(*cpu_if->irqbuf),
			    M_VGIC_V3, M_NOWAIT | M_ZERO);
		memcpy(new_irqbuf, cpu_if->irqbuf,
		    cpu_if->irqbuf_size * sizeof(*cpu_if->irqbuf));

		old_irqbuf = cpu_if->irqbuf;
		cpu_if->irqbuf = new_irqbuf;
		cpu_if->irqbuf_size = new_size;
		free(old_irqbuf, M_VGIC_V3);
	}

	cpu_if->irqbuf_num++;

	return (&cpu_if->irqbuf[cpu_if->irqbuf_num - 1]);
}

static bool
vgic_v3_int_target(uint32_t irq, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	uint64_t irouter;
	uint64_t aff;
	uint32_t irq_off, irq_mask;
	int n, group;

	if (irq <= GIC_LAST_PPI)
		return (true);

	/* XXX Affinity routing disabled not implemented */
	if (!aff_routing_en(dist))
		return (true);

	irq_off = irq % 32;
	irq_mask = 1 << irq_off;
	n = irq / 32;

	if (n == 0)
		group = (redist->gicr_igroupr0 & irq_mask) ? 1 : 0;
	else
		group = (dist->gicd_igroupr[n] & irq_mask) ? 1 : 0;

	irouter = dist->gicd_irouter[irq];
	/* Check if 1-of-N routing is active */
	if (irouter & GICD_IROUTER_IRM) {
		/* Check if the VCPU is participating */
		switch (group) {
		case (0):
			return (redist->gicr_ctlr & GICR_CTLR_DPG0 ? true : false);
		case (1):
			return (redist->gicr_ctlr & GICR_CTLR_DPG1NS ? true : false);
		}
	}

	aff = redist->gicr_typer >> GICR_TYPER_AFF_SHIFT;
	/* Affinity in format for comparison with irouter */
	aff = GICR_TYPER_AFF0(redist->gicr_typer) | \
	    (GICR_TYPER_AFF1(redist->gicr_typer) << 8) | \
	    (GICR_TYPER_AFF2(redist->gicr_typer) << 16) | \
	    (GICR_TYPER_AFF3(redist->gicr_typer) << 32);
	if ((irouter & aff) == aff)
		return (true);
	else
		return (false);
}

static uint8_t
vgic_v3_get_priority(uint32_t irq, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;
	size_t n;
	uint32_t off, mask;
	uint8_t priority;

	n = irq / 4;
	off = n % 4;
	mask = 0xff << off;
	/*
	 * When affinity routing is enabled, the Redistributor is used for
	 * SGIs and PPIs and the Distributor for SPIs. When affinity routing
	 * is not enabled, the Distributor registers are used for all
	 * interrupts.
	 */
	if (aff_routing_en(dist) && (n <= 7))
		priority = (redist->gicr_ipriorityr[n] & mask) >> off;
	else
		priority = (dist->gicd_ipriorityr[n] & mask) >> off;

	return (priority);
}

static bool
vgic_v3_intid_enabled(uint32_t irq, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist;
	struct vgic_v3_redist *redist;
	uint32_t irq_off, irq_mask;
	int n;

	irq_off = irq % 32;
	irq_mask = 1 << irq_off;
	n = irq / 32;

	if (irq <= GIC_LAST_PPI) {
		redist = &hypctx->vgic_redist;
		if (!(redist->gicr_ixenabler0 & irq_mask))
			return (false);
	} else {
		dist = &hypctx->hyp->vgic_dist;
		if (!(dist->gicd_ixenabler[n] & irq_mask))
			return (false);
	}

	return (true);
}

/* Check in the Distributor that the interrupt group hasn't been disabled */
static bool
vgic_v3_group_enabled(int group, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;

	if (group == 1 && !(dist->gicd_ctlr & GICD_CTLR_G1A))
		return (false);

	if (group == 0 && !(dist->gicd_ctlr & GICD_CTLR_G1))
		return (false);

	return (true);
}

static inline int
vgic_v3_get_int_group(unsigned int irq, struct hypctx *hypctx)
{
	struct vgic_v3_dist *dist;
	struct vgic_v3_redist *redist;
 	uint32_t irq_mask;
	int n;
	int group;

	irq_mask = 1 << irq;
	n = irq / 32;

 	if (irq <= GIC_LAST_PPI) {
		redist = &hypctx->vgic_redist;
		group = (redist->gicr_igroupr0 & irq_mask) ? 1 : 0;
	} else {
		dist = &hypctx->hyp->vgic_dist;
		group = (dist->gicd_igroupr[n] & irq_mask) ? 1 : 0;
	}

	return group;
}

int
vgic_v3_inject_irq(void *arg, uint32_t irq, enum vgic_v3_irqtype irqtype)
{
        struct hypctx *hypctx = arg;
	struct vgic_v3_dist *dist = &hypctx->hyp->vgic_dist;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_irq *vip;
	int group;
	int error;
	uint8_t priority;
	bool enabled;

	KASSERT(irq > GIC_LAST_SGI, ("SGI interrupts not implemented"));

	if (irq >= dist->nirqs || irqtype >= VGIC_IRQ_INVALID) {
		eprintf("Malformed IRQ %u.\n", irq);
		return (1);
	}

#if 0
	int i, cnt;
	cnt = 0;
	for (i = 0; i < cpu_if->irqbuf_num; i++)
		if (cpu_if->irqbuf[i].irq == irq)
			cnt++;
	if (irq == 27)
		eprintf("Injecting %u, existing instances = %d\n", irq, cnt);
	cnt = 0;
	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if ((cpu_if->ich_lr_el2[i] & (uint64_t)0xffffffff) == (uint64_t)irq)
			cnt++;
	if (irq == 27)
		eprintf("Injecting %u, lr pending = %d\n", irq, cnt);
#endif

	error = 0;
	mtx_lock_spin(&dist->dist_mtx);

	/* XXX GIC{R, D}_IGROUPMODR set the secure/non-secure bit */
	group  = vgic_v3_get_int_group(irq, hypctx);
	enabled = vgic_v3_group_enabled(group, hypctx);
	enabled = enabled && vgic_v3_intid_enabled(irq, hypctx);
	enabled = enabled && vgic_v3_int_target(irq, hypctx);
	priority = vgic_v3_get_priority(irq, hypctx);

	mtx_lock_spin(&cpu_if->lr_mtx);

	vip = vgic_v3_irqbuf_add_nolock(cpu_if);
	if (!vip) {
		eprintf("Error adding IRQ %u to the IRQ buffer.\n", irq);
		error = 1;
		goto out_unlock;
	}
	vip->irq = irq;
	vip->irqtype = irqtype;
	vip->group = group;
	vip->enabled = enabled;
	vip->priority = priority;

out_unlock:
	mtx_unlock_spin(&cpu_if->lr_mtx);
	mtx_unlock_spin(&dist->dist_mtx);

	return (error);
}

static void
vgic_v3_irq_set_priority_vcpu(uint32_t irq, uint8_t priority,
    struct vgic_v3_cpu_if *cpu_if)
{
	int i;

	mtx_lock_spin(&cpu_if->lr_mtx);

	for (i = 0; i < cpu_if->irqbuf_num; i++)
		if (cpu_if->irqbuf[i].irq == irq)
			cpu_if->irqbuf[i].priority = priority;

	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (lr_pending(cpu_if->ich_lr_el2[i])) {
			cpu_if->ich_lr_el2[i] &= ~ICH_LR_EL2_PRIO_MASK;
			cpu_if->ich_lr_el2[i] |= \
			    (uint64_t)priority << ICH_LR_EL2_PRIO_SHIFT;
		}

	mtx_unlock_spin(&cpu_if->lr_mtx);
}

void
vgic_v3_irq_set_priority(uint32_t irq, uint8_t priority,
    struct hyp *hyp, int vcpuid)
{
	struct vgic_v3_cpu_if *cpu_if;
	int i;

	if (irq <= GIC_LAST_PPI) {
		cpu_if = &hyp->ctx[vcpuid].vgic_cpu_if;
		vgic_v3_irq_set_priority_vcpu(irq, priority, cpu_if);
	} else {
		/* TODO: IRQ is SPI, update irqbuf for all VCPUs */
		for (i = 0; i < 1; i++) {
			cpu_if = &hyp->ctx[i].vgic_cpu_if;
			vgic_v3_irq_set_priority_vcpu(irq, priority, cpu_if);
		}
	}
}

static void
vgic_v3_irq_set_group_vcpu(uint32_t irq, uint8_t group,
    struct vgic_v3_cpu_if *cpu_if)
{
	int i;

	mtx_lock_spin(&cpu_if->lr_mtx);

	for (i = 0; i < cpu_if->irqbuf_num; i++)
		if (cpu_if->irqbuf[i].irq == irq)
			cpu_if->irqbuf[i].group = group;

	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (lr_pending(cpu_if->ich_lr_el2[i])) {
			cpu_if->ich_lr_el2[i] &= ~(1UL << ICH_LR_EL2_GROUP_SHIFT);
			cpu_if->ich_lr_el2[i] |= \
			    (uint64_t)group << ICH_LR_EL2_GROUP_SHIFT;
		}

	mtx_unlock_spin(&cpu_if->lr_mtx);
}

void
vgic_v3_irq_set_group(uint32_t irq, uint8_t group, struct hyp *hyp, int vcpuid)
{
	struct vgic_v3_cpu_if *cpu_if;
	int i;

	if (irq <= GIC_LAST_PPI) {
		cpu_if = &hyp->ctx[vcpuid].vgic_cpu_if;
		vgic_v3_irq_set_group_vcpu(irq, group, cpu_if);
	} else {
		/* TODO: Update irqbuf for all VCPUs, not just VCPU 0 */
		for (i = 0; i < 1; i++) {
			cpu_if = &hyp->ctx[i].vgic_cpu_if;
			vgic_v3_irq_set_group_vcpu(irq, group, cpu_if);
		}
	}
}

void
vgic_v3_irq_toggle_group_enabled(int group, bool enabled, struct hyp *hyp)
{
	struct hypctx *hypctx;
	struct vgic_v3_cpu_if *cpu_if;
	struct vgic_v3_irq *vip;
	int i, j;

	for (i = 0; i < VM_MAXCPU; i++) {
		hypctx = &hyp->ctx[i];
		cpu_if = &hypctx->vgic_cpu_if;

		mtx_lock_spin(&cpu_if->lr_mtx);

		for (j = 0; j < cpu_if->irqbuf_num; j++) {
			vip = &cpu_if->irqbuf[j];
			if (vip->group != group)
				continue;
			if (!enabled)
				vip->enabled = 0;
			else if (vgic_v3_intid_enabled(vip->irq, hypctx))
				vip->enabled = 1;
		}

		mtx_unlock_spin(&cpu_if->lr_mtx);
	}
}

static int
vgic_v3_irq_toggle_enabled_vcpu(uint32_t irq, bool enabled,
    struct vgic_v3_cpu_if *cpu_if)
{
	//struct vgic_v3_irq *vip;
	//int group;
	int i;

	mtx_lock_spin(&cpu_if->lr_mtx);

	if (enabled) {
		/*
		 * Enable IRQs that were injected when the interrupt ID was
		 * disabled
		 */
		for (i = 0; i < cpu_if->irqbuf_num; i++)
			if (cpu_if->irqbuf[i].irq == irq) {
				/* TODO: Check if group is enabled */
				//group = vgic_v3_get_int_group(irq, cpu_if);
				cpu_if->irqbuf[i].enabled = true;
			}
	} else {
		/* Remove the disabled IRQ from the LR regs if it is pending */
		for (i = 0; i < cpu_if->ich_lr_num; i++)
			if (lr_pending(cpu_if->ich_lr_el2[i]) &&
			    ICH_LR_EL2_VINTID(cpu_if->ich_lr_el2[i]) == irq)
				lr_clear_irq(cpu_if->ich_lr_el2[i]);

		/* Remove the IRQ from the interrupt buffer */
		vgic_v3_irqbuf_remove_nolock(irq, cpu_if);
	}

	mtx_unlock_spin(&cpu_if->lr_mtx);

	return (0);
}

int
vgic_v3_irq_toggle_enabled(uint32_t irq, bool enabled,
    struct hyp *hyp, int vcpuid)
{
	struct vgic_v3_cpu_if *cpu_if;
	int error;
	int i;

	if (irq <= GIC_LAST_PPI) {
		cpu_if = &hyp->ctx[vcpuid].vgic_cpu_if;
		return (vgic_v3_irq_toggle_enabled_vcpu(irq, enabled, cpu_if));
	} else {
		/* TODO: Update irqbuf for all VCPUs, not just VCPU 0 */
		for (i = 0; i < 1; i++) {
			cpu_if = &hyp->ctx[i].vgic_cpu_if;
			error = vgic_v3_irq_toggle_enabled_vcpu(irq, enabled, cpu_if);
			if (error)
				return (error);
		}
	}

	return (0);
}

static struct vgic_v3_irq *
vgic_v3_highest_priority_pending(struct vgic_v3_cpu_if *cpu_if,
    struct hypctx *hypctx)
{
	uint32_t irq;
	int i, max_idx, group;
	uint8_t priority, max_priority;
	uint8_t vpmr;

	vpmr = (cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VPMR_MASK) >> \
	    ICH_VMCR_EL2_VPMR_SHIFT;

	max_idx = -1;
	max_priority = 0xff;
	for (i = 0; i < cpu_if->irqbuf_num; i++) {
		irq = cpu_if->irqbuf[i].irq;
		/* Check that the interrupt hasn't been already scheduled */
		if (irq == IRQ_SCHEDULED)
			continue;

		group = vgic_v3_get_int_group(irq, hypctx);
		if (!vgic_v3_group_enabled(group, hypctx))
			continue;
		if (!vgic_v3_intid_enabled(irq, hypctx))
			continue;

		if (!vgic_v3_int_target(irq, hypctx))
			continue;

		//priority = vgic_v3_get_priority(irq, hypctx);
		priority = cpu_if->irqbuf[i].priority;
		if (priority >= vpmr)
			continue;

		if (max_idx == -1) {
			max_idx = i;
			max_priority = priority;
		} else if (priority > max_priority) {
			max_idx = i;
			max_priority = priority;
		} else if (priority == max_priority &&
		    cpu_if->irqbuf[i].irqtype < cpu_if->irqbuf[max_idx].irqtype) {
			max_idx = i;
			max_priority = priority;
		}
	}

	if (max_idx == -1)
		return (NULL);
	return (&cpu_if->irqbuf[max_idx]);
}

static void
vgic_v3_move_irqbuf_to_lr(struct hypctx *hypctx, struct vgic_v3_cpu_if *cpu_if)
{
	struct vgic_v3_irq *vip;
	int irqbuf_idx;
	int group;
	int i;

	irqbuf_idx = 0;
	for (i = 0; i < cpu_if->ich_lr_num; i++) {
		/* Find the first enabled buffered interrupt */
find_next_buffered_interrupt:
		for (; irqbuf_idx < cpu_if->irqbuf_num; irqbuf_idx++) {
			vip = &cpu_if->irqbuf[irqbuf_idx];
			if (!vip->enabled)
				continue;
			group = vgic_v3_get_int_group(vip->irq, hypctx);
			if (group == 1 &&
			    !(cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VENG1))
				continue;
			if (group == 0 &&
			    !(cpu_if->ich_vmcr_el2 & ICH_VMCR_EL2_VENG0))
				continue;
			break;
		}

		/* All buffered interrupts have been scheduled */
		if (irqbuf_idx == cpu_if->irqbuf_num)
			break;

		/* Find an empty LR register */
		/* TODO: this is very inefficient. Move it up again. */
		if (vip->irq == 27 && !lr_inactive(cpu_if->ich_lr_el2[i])) {
			/* Guest is behind timer interrupts. Don't swamp the
			 * guest with interrupts and move to the next buffered
			 * interupt. */
			irqbuf_idx++;
			goto find_next_buffered_interrupt;
		}

		if (!lr_inactive(cpu_if->ich_lr_el2[i]))
			continue;

		/* Copy the IRQ to the LR register */
		vip_to_lr(vip, cpu_if->ich_lr_el2[i]);

		/*
		printf("IRQ %u, group = %u\n", vip->irq, vip->group);
		printf("cpu_if->ich_lr_el2[%d] = 0x%016lx\n",
				i, cpu_if->ich_lr_el2[i]);
				*/

		/* Mark the buffered interrupt as scheduled... */
		vip->irq = IRQ_SCHEDULED;
		/* ... and proceed to the next buffered interrupt */
		irqbuf_idx++;
		if (irqbuf_idx == cpu_if->irqbuf_num)
			break;
	}

	/* Remove all interrupts that were scheduled now */
	vgic_v3_irqbuf_remove_nolock(IRQ_SCHEDULED, cpu_if);
}

void
vgic_v3_sync_hwstate(void *arg)
{
	struct hypctx *hypctx = arg;
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;
	struct vgic_v3_irq *vip;
	uint64_t *lrp;
	uint32_t irq;
	int lr_free;
	int i;

	/*
	 * All Distributor writes have been executed at this point, do not
	 * protect reads with a  mutex.
	 */

	mtx_lock_spin(&cpu_if->lr_mtx);

	/* Exit early if there are no buffered interrupts */
	if (cpu_if->irqbuf_num == 0)
		goto out;

	/* Test if all buffered interrupts can fit in the LR regs */
	lr_free = 0;
	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (lr_inactive(cpu_if->ich_lr_el2[i]))
			lr_free++;

	/* Move buffered interrupts to the LR regs and exit early */
	if (cpu_if->irqbuf_num <= lr_free) {
		vgic_v3_move_irqbuf_to_lr(hypctx, cpu_if);
		goto out;
	}

	/* This is bad. This shouldn't happen */
	eprintf("RESHUFFLING! lr_free = %d, irqbuf_num = %zu\n",
	    lr_free, cpu_if->irqbuf_num);

	/* TODO: Update this part for better efficiency */
	/* TODO: This cauuses a panic still? where? */

	/*
	 * Add all interrupts from the list registers that are not active to
	 * the pending buffer to be rescheduled in the next step.
	 */
	for (i = 0; i < cpu_if->ich_lr_num; i++)
		if (lr_pending(cpu_if->ich_lr_el2[i])) {
			lrp = &cpu_if->ich_lr_el2[i];
			irq = *lrp & ICH_LR_EL2_VINTID_MASK;

			vip = vgic_v3_irqbuf_add_nolock(cpu_if);
			if (!vip)
				/* Pending list full, stop it */
				break;
			lr_to_vip(*lrp, vip);
			/*
			 * Interrupts from the LR regs are always enabled.
			 * Distributor emulation will remove then if they become
			 * disabled.
			 */
			vip->enabled = 1;
			vip->irqtype = VGIC_IRQ_MAXPRIO;

			/* Mark it as inactive */
			lr_clear_irq(*lrp);
		}

	eprintf("before filling the list registers\n");
	for (i = 0; i < cpu_if->ich_lr_num; i++) {
		if (!lr_inactive(cpu_if->ich_lr_el2[i]))
			continue;

		vip = vgic_v3_highest_priority_pending(cpu_if, hypctx);
		if (vip == NULL)
			/* No more pending interrupts */
			break;
		vip_to_lr(vip, cpu_if->ich_lr_el2[i]);

		/* Mark the scheduled pending interrupt as invalid */
		vip->irq = IRQ_SCHEDULED;
	}

	/* Remove all scheduled interrupts */
	eprintf("before removing the scheduled interrupts\n");
	vgic_v3_irqbuf_remove_nolock(IRQ_SCHEDULED, cpu_if);

	/* TODO Enable maintenance interrupts if interrupts are still pending */
	panic("reshuffled");

out:
	mtx_unlock_spin(&cpu_if->lr_mtx);
}

static void
vgic_v3_get_ro_regs()
{
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
vgic_v3_init(uint64_t ich_vtr_el2) {
	uint32_t pribits, prebits;

	KASSERT(gic_sc != NULL, ("GIC softc is NULL"));
	KASSERT(tmr_sc != NULL, ("Generic Timer softc is NULL"));

	vgic_v3_get_ro_regs();

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
arm_vgic_detach(device_t dev)
{
	gic_sc = NULL;
	tmr_sc = NULL;

	return (0);
}

static int
arm_vgic_attach(device_t dev)
{
	return (0);
}

static void
arm_vgic_identify(driver_t *driver, device_t parent)
{
	device_t dev;

	if (strcmp(device_get_name(parent), "gic") == 0) {
		dev = device_add_child(parent, VGIC_V3_DEVNAME, -1);
		gic_sc = device_get_softc(parent);
	}

	if (strcmp(device_get_name(parent), "generic_timer") == 0) {
		tmr_dev = parent;
		tmr_sc = device_get_softc(tmr_dev);
	}
}

static int
arm_vgic_probe(device_t dev)
{
	device_t parent;

	parent = device_get_parent(dev);
	if (strcmp(device_get_name(parent), "gic") == 0) {
		device_set_desc(dev, VGIC_V3_DEVSTR);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static device_method_t arm_vgic_methods[] = {
	DEVMETHOD(device_identify,	arm_vgic_identify),
	DEVMETHOD(device_probe,		arm_vgic_probe),
	DEVMETHOD(device_attach,	arm_vgic_attach),
	DEVMETHOD(device_detach,	arm_vgic_detach),
	DEVMETHOD_END
};

DEFINE_CLASS_1(vgic, arm_vgic_driver, arm_vgic_methods, 0, gic_v3_driver);

static devclass_t arm_vgic_devclass1, arm_vgic_devclass2;
DRIVER_MODULE(vgic, gic, arm_vgic_driver, arm_vgic_devclass1, 0, 0);
DRIVER_MODULE(vgic, generic_timer, arm_vgic_driver, arm_vgic_devclass2, 0, 0);
