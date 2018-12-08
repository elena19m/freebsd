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

#ifndef _VMM_VGIC_V3_H_
#define	_VMM_VGIC_V3_H_

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/vmm_instruction_emul.h>

#include <arm64/arm64/gic_v3_reg.h>
#include <arm/arm/gic_common.h>

#define VGIC_SGI_NUM		(GIC_LAST_SGI - GIC_FIRST_SGI + 1)
#define VGIC_PPI_NUM		(GIC_LAST_PPI - GIC_FIRST_PPI + 1)
#define VGIC_SPI_NUM		(GIC_LAST_SPI - GIC_FIRST_SPI + 1)
#define VGIC_PRV_I_NUM		(VGIC_SGI_NUM + VGIC_PPI_NUM)
#define VGIC_SHR_I_NUM		(VGIC_SPI_NUM)

#define VGIC_ICH_LR_NUM_MAX	16
#define	VGIC_ICH_AP0R_NUM_MAX	4
#define	VGIC_ICH_AP1R_NUM_MAX	VGIC_ICH_AP0R_NUM_MAX

/* Order matters, a lower value means a higher precedence */
enum vgic_v3_irqtype {
	VGIC_IRQ_MAXPRIO,
	VGIC_IRQ_CLK,
	VGIC_IRQ_VIRTIO,
	VGIC_IRQ_MISC,
	VGIC_IRQ_INVALID,
};

struct vgic_mmio_region {
	vm_offset_t start;
	vm_offset_t end;
	mem_region_read_t read;
	mem_region_write_t write;
};

struct vm;
struct vm_exit;
struct hyp;

struct vgic_v3_dist {
	struct mtx 	dist_mtx;

	uint64_t 	start;
	size_t   	end;
	size_t		nirqs;

	uint32_t 	gicd_ctlr;	/* Distributor Control Register */
	uint32_t 	gicd_typer;	/* Interrupt Controller Type Register */
	uint32_t 	gicd_pidr2;	/* Distributor Peripheral ID2 Register */
	/* Interrupt Configuration Registers. */
	uint32_t	*gicd_icfgr;
	/* Interrupt Priority Registers. */
	uint32_t	*gicd_ipriorityr;
	/* Interrupt Routing Registers. */
	uint64_t	*gicd_irouter;
	/* Interrupt Clear-Enable and Set-Enable Registers. */
	uint32_t	*gicd_ixenabler;
};

#define	aff_routing_en(distp)	(distp->gicd_ctlr & GICD_CTLR_ARE_NS)

struct vgic_v3_redist {
	uint64_t 	start;
	uint64_t 	end;

	uint64_t	gicr_typer;	/* Redistributor Type Register */
	uint32_t	gicr_ctlr;	/* Redistributor Control Regiser */
	uint32_t	gicr_ixenabler0;
	/* Interrupt Priority Registers. */
	uint32_t	gicr_ipriorityr[VGIC_PRV_I_NUM / 4];
	/* Interupt Configuration Registers */
	uint32_t	gicr_icfgr0, gicr_icfgr1;
};

struct vgic_v3_irq;
struct vgic_v3_cpu_if {
	uint32_t	ich_eisr_el2;	/* End of Interrupt Status Register */
	uint32_t	ich_elsr_el2;	/* Empty List register Status Register (ICH_ELRSR_EL2) */
	uint32_t	ich_hcr_el2;	/* Hyp Control Register */
	uint32_t	ich_misr_el2;	/* Maintenance Interrupt State Register */
	uint32_t	ich_vmcr_el2;	/* Virtual Machine Control Register */

	/*
	 * The List Registers are part of the VM context and are modified on a
	 * world switch. They need to be allocated statically so they are
	 * mapped in the EL2 translation tables when struct hypctx is mapped.
	 */
	uint64_t	ich_lr_el2[VGIC_ICH_LR_NUM_MAX];
	size_t		ich_lr_num;

	/*
	 * We need a mutex for accessing the list registers because they are
	 * modified asynchronously by the virtual timer.
	 *
	 * Note that the mutex *MUST* be a spin mutex because an interrupt can
	 * be injected by a callout callback function, thereby modifying the
	 * list registers from a context where sleeping is forbidden.
	 */
	struct mtx	lr_mtx;

	/* Active Priorities Registers for Group 0 and 1 interrupts */
	uint32_t	ich_ap0r_el2[VGIC_ICH_AP0R_NUM_MAX];
	size_t		ich_ap0r_num;
	uint32_t	ich_ap1r_el2[VGIC_ICH_AP1R_NUM_MAX];
	size_t		ich_ap1r_num;

	struct vgic_v3_irq *irqbuf;
	size_t		irqbuf_size;
	size_t		irqbuf_num;
};

int 	vgic_v3_attach_to_vm(void *arg, uint64_t dist_start, size_t dist_size,
			     uint64_t redist_start, size_t redist_size);
void	vgic_v3_detach_from_vm(void *arg);
void	vgic_v3_init(uint64_t ich_vtr_el2);
void	vgic_v3_vminit(void *arg);
void	vgic_v3_cpuinit(void *arg, bool last_vcpu);
void 	vgic_v3_sync_hwstate(void *arg);

void	vgic_v3_mmio_init(struct hyp *hyp);
void	vgic_v3_mmio_destroy(struct hyp *hyp);

int 	vgic_v3_vcpu_pending_irq(void *arg);
int 	vgic_v3_inject_irq(void *arg, uint32_t irq,
			   enum vgic_v3_irqtype irqtype);
int 	vgic_v3_remove_irq(void *arg, uint32_t irq, bool ignore_state);

void	vgic_v3_irq_set_priority(uint32_t irq, uint8_t priority,
				 struct hyp *hyp, int vcpuid);
void	vgic_v3_group_toggle_enabled(bool enabled, struct hyp *hyp);
int	vgic_v3_irq_toggle_enabled(uint32_t irq, bool enabled,
				   struct hyp *hyp, int vcpuid);

DECLARE_CLASS(arm_vgic_driver);

#endif /* !_VMM_VGIC_V3_H_ */
