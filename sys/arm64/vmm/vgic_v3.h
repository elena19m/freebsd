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

#include <arm64/arm64/gic_v3_reg.h>

#define VGIC_SGI_NUM		(GIC_LAST_SGI - GIC_FIRST_SGI + 1)
#define VGIC_PPI_NUM		(GIC_LAST_PPI - GIC_FIRST_PPI + 1)
#define VGIC_SPI_NUM		(GIC_LAST_SPI - GIC_FIRST_SPI + 1)
#define VGIC_PRV_INT_NUM	(VGIC_SGI_NUM + VGIC_PPI_NUM)
#define VGIC_SHR_INT_NUM	(VGIC_SPI_NUM)

#define VGIC_LR_NUM_MAX		16
#define VGIC_LR_EMPTY		0xff

#define VGIC_MAXCPU		VM_MAXCPU

#define VGIC_CFG_LEVEL	0
#define VGIC_CFG_EDGE	1

struct vm;
struct vm_exit;

struct vgic_distributor {
	struct mtx distributor_lock;

	uint64_t distributor_base;
	uint64_t cpu_int_base;

	uint32_t enabled;

	/* Bitmaps for IRQ state in the distributor*/

	/* Interrupt enabled */
	uint32_t irq_enabled_prv[VGIC_MAXCPU][VGIC_PRV_INT_NUM / (sizeof(uint32_t) * 8)];
	uint32_t irq_enabled_shr[VGIC_SHR_INT_NUM / (sizeof(uint32_t) * 8)];

	/* Interrupt level */
	uint32_t irq_state_prv[VGIC_MAXCPU][VGIC_PRV_INT_NUM / (sizeof(uint32_t) * 8)];
	uint32_t irq_state_shr[VGIC_SHR_INT_NUM / (sizeof(uint32_t) * 8)];

	/* Level interrupts in progress */
	uint32_t irq_active_prv[VGIC_MAXCPU][VGIC_PRV_INT_NUM / (sizeof(uint32_t) * 8)];
	uint32_t irq_active_shr[VGIC_SHR_INT_NUM / (sizeof(uint32_t) * 8)];

	/* Configure type of IRQ: level or edge triggered */
	uint32_t irq_conf_prv[VGIC_MAXCPU][VGIC_PRV_INT_NUM / (sizeof(uint32_t) * 8)];
	uint32_t irq_conf_shr[VGIC_SHR_INT_NUM / (sizeof(uint32_t) * 8)];

	/* Interrupt targets */
	uint32_t irq_target_shr[VGIC_SHR_INT_NUM / sizeof(uint32_t)];

	uint8_t irq_sgi_source[VGIC_MAXCPU][VGIC_SGI_NUM];

	uint32_t sgir;

	uint32_t irq_pending_on_cpu;
};

struct vgic_v3_cpu_if {
	/* Bitmaps for pending IRQs */
	uint32_t	pending_prv[VGIC_PRV_INT_NUM / (sizeof(uint32_t) * 8)];
	uint32_t	pending_shr[VGIC_SHR_INT_NUM / (sizeof(uint32_t) * 8)];

	/* ICH_AP{0, 1}R<n>_EL2 are used for legacy VMs, not supported. */

	uint32_t	ich_eisr_el2;	/* End of Interrupt Status Register. */
	uint32_t	ich_elsr_el2;	/* Empty List register Status Register. */
	uint32_t	ich_hcr_el2;	/* Hyp Control Register. */
	uint32_t	ich_misr_el2;	/* Maintenance Interrupt State Register. */
	uint32_t	ich_vmcr_el2;	/* Virtual Machine Control Register. */

	uint64_t	ich_lr_el2[VGIC_LR_NUM_MAX];	/* List Registers. */
	size_t		lr_num;				/* Number of used List Registers. */
	uint8_t		lr_used[VGIC_LR_NUM_MAX];	/* Bitmap for used List Registers. */
	uint8_t		irq_to_lr[GIC_I_NUM_MAX];
};

int vgic_v3_map(pmap_t el2_pmap);

int vgic_v3_emulate_distributor(void *arg, int vcpuid,
		struct vm_exit *vme, bool *retu);

int vgic_v3_attach_to_vm(void *arg, uint64_t distributor_paddr,
		uint64_t cpu_int_paddr);

void vgic_v3_sync_hwstate(void *arg);

void vgic_v3_flush_hwstate(void *arg);

int vgic_v3_vcpu_pending_irq(void *arg);

int vgic_v3_inject_irq(void *arg, unsigned int irq, bool level);

struct vgic_v3_softc {
	struct resource *maintenance_int_res;		/* Not used. */
	void 		*maintenance_int_cookie;	/* Not used. */
	device_t 	gic_v3_dev;
	device_t 	vgic_v3_dev;
};

DECLARE_CLASS(arm_vgic_driver);

#endif /* !_VMM_VGIC_V3_H_ */
