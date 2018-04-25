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
#include <machine/param.h>
#include <machine/cpufunc.h>
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

#define VGIC_V3_DEVNAME	"vgic"
#define VGIC_V3_DEVSTR	"ARM Virtual Generic Interrupt Controller v3"

extern uint64_t hypmode_enabled;
extern uint64_t ich_vtr_el2_reg;

struct vgic_v3_virt_features {
	size_t lr_num;
	uint32_t prebits;
	uint32_t pribits;
};
static struct vgic_v3_virt_features virt_features;

struct vgic_v3_ro_regs {
	uint32_t gicd_typer;
	uint32_t gicd_pidr2;
};
static struct vgic_v3_ro_regs ro_regs;

static uint64_t virtual_int_ctrl_vaddr;
static uint64_t virtual_int_ctrl_paddr;
static uint32_t virtual_int_ctrl_size;

static uint64_t virtual_cpu_int_paddr;
static uint32_t virtual_cpu_int_size;

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

static int
vgic_v3_redist_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
		int size, void *arg)
{
	uint64_t reg;
	struct hyp *hyp;
	struct vgic_v3_redist *redist;

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	/* Offset of redistributor register. */
	reg = fault_ipa - redist->ipa;

	eprintf("reg: 0x%04lx\n", reg);

	return (0);
}

static int
vgic_v3_redist_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t val,
		int size, void *arg)
{
	uint64_t reg;
	struct hyp *hyp;
	struct vgic_v3_redist *redist;

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	/* Offset of redistributor register. */
	reg = fault_ipa - redist->ipa;

	eprintf("reg: 0x%04lx\n", reg);

	return (0);
}

/*
 * TODO
 */
static int
vgic_v3_dist_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
		int size, void *arg)
{
	uint64_t reg;
	struct hyp *hyp;
	struct vgic_v3_dist *dist;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	/* Offset of distributor register. */
	reg = fault_ipa - dist->ipa;

	/* TODO: GICD_IROUTER<n> is 64 bits wide, the rest are 32 bits wide. */

	if (reg == GICD_CTLR) {
		eprintf("read: GICD_CTLR\n");
		*rval = dist->gicd_ctlr;
	} else if (reg == GICD_TYPER) {
		eprintf("read: GICD_TYPER\n");
		*rval = dist->gicd_typer;
	} else if (reg == GICD_IIDR) {
		eprintf("read: GICD_IIDR\n");
		*rval = 0;
	} else if (reg == GICD_PIDR2) {
		eprintf("read: GICD_PIDR2\n");
		*rval = dist->gicd_pidr2;
	} else {
		eprintf("Unknown register: 0x%04lx\n", reg);
		*rval = 0;
	}

	return (0);
}

static int
vgic_v3_dist_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t val,
		int size, void *arg)
{

	uint64_t reg;
	struct hyp *hyp;
	struct vgic_v3_dist *dist;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	/* Offset of distributor register. */
	reg = fault_ipa - dist->ipa;

	/* TODO: GICD_IROUTER<n> is 64 bits wide, the rest are 32 bits wide. */

	if (reg == GICD_CTLR) {
		/* Guest will always read that no writes are pending. */
		dist->gicd_ctlr = (uint32_t)val & GICD_CTLR_RES0 & ~GICD_CTLR_RWP;
		eprintf("write: GICD_CTLR\n");
		eprintf("\t\tval = 0x%08x\n", dist->gicd_ctlr);
	} else if (reg == GICD_TYPER) {
		eprintf("Warning: Trying to write to read-only register GICD_TYPER.\n");
	} else if (reg == GICD_PIDR2) {
		eprintf("Warning: Trying to write to read-only register GICD_PIDR2.\n");
	} else if (reg == GICD_IIDR) {
		eprintf("write: GICD_IIDR\n");
	} else {
		eprintf("Unknown register: 0x%04lx\n", reg);
	}

	/* TODO: update the emulated register with val. */

	return (0);
}

int
vgic_v3_do_emulation(void *arg, int vcpuid, struct vm_exit *vme, bool *retu)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	struct vgic_v3_redist *redist;
	uint64_t fault_ipa;
	int error;

	hyp = (struct hyp *)arg;

	if (!hyp->vgic_attached) {
		*retu = true;
		return (0);
	}

	fault_ipa = vme->u.inst_emul.gpa;
	dist = &hyp->vgic_dist;
	redist = &hyp->ctx[vcpuid].vgic_redist;

	if (fault_ipa >= dist->ipa && fault_ipa < dist->ipa + dist->size) {
		/* Emulate distributor. */
		*retu = false;
		error = vmm_emulate_instruction(hyp->vm, vcpuid, fault_ipa,
				&vme->u.inst_emul.vie, vgic_v3_dist_read,
				vgic_v3_dist_write, retu);
	} else if (fault_ipa >= redist->ipa && fault_ipa < redist->ipa + redist->size) {
		/* Emulate redistributor. */
		*retu = false;
		error = vmm_emulate_instruction(hyp->vm, vcpuid, fault_ipa,
				&vme->u.inst_emul.vie, vgic_v3_redist_read,
				vgic_v3_redist_write, retu);
	} else {
		/*
		 * We cannot emulate the instruction in kernel space, return to
		 * user space for emulation.
		 */
		*retu = true;
		error = 0;
	}

	return (error);
}

int
vgic_v3_attach_to_vm(void *arg, uint64_t dist_ipa, size_t dist_size,
		uint64_t redist_ipa, size_t redist_size)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	struct vgic_v3_redist *redist;

	printf("[vgic_v3: vgic_v3_attach_to_vm()]\n");

	hyp = (struct hyp *)arg;
	dist = &hyp->vgic_dist;
	/* XXX Only one CPU per virtual machine supported. */
	redist = &hyp->ctx[0].vgic_redist;

	/* Set the distributor address and size for trapping guest access. */
	dist->ipa = dist_ipa;
	dist->size = dist_size;

	/* Distributor is disabled at start, the guest will configure it. */
	dist->gicd_ctlr = 0;
	dist->gicd_typer = ro_regs.gicd_typer;
	dist->gicd_pidr2 = ro_regs.gicd_pidr2;

	/* Set the redistributor address and size for trapping guest access. */
	redist->ipa = redist_ipa;
	redist->size = redist_size;

#if 0
	/*
	 * Set the Virtual Interface Control address to save/restore registers
	 * at context switch and initiate the List Registers.
	 */
	for (i = 0; i < VM_MAXCPU; i++) {
		hypctx = &hyp->ctx[i];
		hypctx->vgic_cpu_if.lr_num = virt_features.lr_num;
		hypctx->vgic_cpu_if.ich_hcr_el2 = ICH_HCR_EL2_EN;

		/*
		 * Set up the Virtual Machine Control Register:
		 *
		 * ICH_VMCR_EL2_VPMR_PRIO_LOWEST: all interrupts will be
		 * asserted regardless of their priority.
		 * ~ICH_VMCR_EL2_VEOIM: an EOI write performs priority drop and
		 * deactivation.
		 * ICH_VMCR_EL2_VENG1: virtual Group 1 interrupts are enabled.
		 */
		hypctx->vgic_cpu_if.ich_vmcr_el2 = (ICH_VMCR_EL2_VPMR_PRIO_LOWEST | \
					    ICH_VMCR_EL2_VENG1) & \
					    ~ICH_VMCR_EL2_VEOIM;

		for (j = 0; j < GIC_I_NUM_MAX; j++) {
			if (j < VGIC_PPI_NUM)
				vgic_bitmap_set_irq_val(hyp->vgic_dist.irq_enabled_prv[i],
										hyp->vgic_dist.irq_enabled_shr, j, 1);

			if (j < VGIC_PRV_INT_NUM)
				vgic_bitmap_set_irq_val(hyp->vgic_dist.irq_conf_prv[i],
										hyp->vgic_dist.irq_conf_shr, j, VGIC_CFG_EDGE);

			hypctx->vgic_cpu_if.irq_to_lr[j] = VGIC_LR_EMPTY;
		}
	}
#endif

	/* TODO: Map the CPU Interface over the Virtual CPU Interface */
#if 0
	lpae_vmmmap_set(arg,
	    (lpae_vm_vaddr_t)cpu_int_paddr,
	    (lpae_vm_paddr_t)virtual_cpu_int_paddr,
	    virtual_cpu_int_size,
	    VM_PROT_READ | VM_PROT_WRITE);
#endif
	hyp->vgic_attached = true;

	return (0);
}

static int
vgic_bitmap_get_irq_val(uint32_t *irq_prv, uint32_t *irq_shr, int irq)
{
	if (irq < VGIC_PRV_INT_NUM)
		return bit_test((bitstr_t *)irq_prv, irq);

	return bit_test((bitstr_t *)irq_shr, irq - VGIC_PRV_INT_NUM);
}

static void
vgic_bitmap_set_irq_val(uint32_t *irq_prv, uint32_t *irq_shr, int irq, int val)
{
	uint32_t *reg;

	if (irq < VGIC_PRV_INT_NUM) {
		reg = irq_prv;
	} else {
		reg = irq_shr;
		irq -= VGIC_PRV_INT_NUM;
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

	if (irq < VGIC_PRV_INT_NUM)
		bit_set((bitstr_t *)cpu_if->pending_prv, irq);
	else
		bit_set((bitstr_t *)cpu_if->pending_shr,
				irq - VGIC_PRV_INT_NUM);
}

static void
vgic_cpu_irq_clear(struct hypctx *hypctx, int irq)
{
	struct vgic_v3_cpu_if *cpu_if = &hypctx->vgic_cpu_if;

	if (irq < VGIC_PRV_INT_NUM)
		bit_clear((bitstr_t *)cpu_if->pending_prv, irq);
	else
		bit_clear((bitstr_t *)cpu_if->pending_shr,
				irq - VGIC_PRV_INT_NUM);
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
		       (bitstr_t *)enabled, VGIC_PRV_INT_NUM);

	pending = dist->irq_state_shr;
	enabled = dist->irq_enabled_shr;
	target = dist->irq_target_shr;
	bitstr_and((bitstr_t *)pend_shared, (bitstr_t *)pending,
		       (bitstr_t *)enabled, VGIC_SHR_INT_NUM);
	bitstr_and((bitstr_t *)pend_shared, (bitstr_t *)pend_shared,
		       (bitstr_t *)target, VGIC_SHR_INT_NUM);

	bit_ffs((bitstr_t *)pend_percpu, VGIC_PRV_INT_NUM, &pending_private);
	bit_ffs((bitstr_t *)pend_shared, VGIC_SHR_INT_NUM, &pending_shared);

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
		if (!vgic_queue_hwirq(hypctx, i + VGIC_PRV_INT_NUM))
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

        if (irq >= VGIC_PRV_INT_NUM) {
                cpu = 0;//vgic_dist->irq_spi_cpu[irq - VGIC_PRV_INT_NUM];
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

/*
 * TODO: map the GICD and GICR in el2_pmap.
 */
int
vgic_v3_map(pmap_t el2_pmap)
{
#if 0
	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)virtual_int_ctrl_vaddr,
	    (lpae_vm_paddr_t)virtual_int_ctrl_paddr,
	    virtual_int_ctrl_size,
	    VM_PROT_READ | VM_PROT_WRITE);
#endif

	virtual_int_ctrl_vaddr = 0;
	virtual_int_ctrl_paddr = 0;
	virtual_int_ctrl_size = 0;

	/* Virtual CPU Interface */
	virtual_cpu_int_paddr = 0;
	virtual_cpu_int_size = 0;

	return (0);
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

static inline void vgic_v3_set_virt_features()
{
	virt_features.lr_num = ICH_VTR_EL2_LISTREGS(ich_vtr_el2_reg);
	virt_features.pribits = ICH_VTR_EL2_PRIBITS(ich_vtr_el2_reg);
	virt_features.prebits = ICH_VTR_EL2_PREBITS(ich_vtr_el2_reg);
}

static int arm_vgic_attach(device_t dev)
{
	int error;

	vgic_v3_set_ro_regs(dev);
	vgic_v3_set_virt_features();

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
