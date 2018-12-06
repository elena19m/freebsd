/*-
 * Copyright (c) 2017 The FreeBSD Foundation
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
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/vmm.h>
#include <machine/armreg.h>

#include <arm64/vmm/arm64.h>

#include "vgic_v3.h"
#include "vtimer.h"

#define	RES1		0xffffffffffffffffUL

#define timer_enabled(ctl)	\
    (!((ctl) & CNTP_CTL_IMASK) && ((ctl) & CNTP_CTL_ENABLE))

static uint64_t cnthctl_el2_reg;
static uint32_t tmr_frq;

int
vtimer_attach_to_vm(void *arg, int phys_ns_irq)
{
	struct hyp *hyp = arg;
	struct vtimer *vtimer = &hyp->vtimer;

	vtimer->phys_ns_irq = phys_ns_irq;
	vtimer->attached = true;

	return (0);
}

/* TODO call this when shutting down the vm */
void
vtimer_detach_from_vm(void *arg)
{
	struct hyp *hyp;
	struct vtimer_cpu *vtimer_cpu;
	int i;

	hyp = (struct hyp *)arg;
	for (i = 0; i < VM_MAXCPU; i++) {
		vtimer_cpu = &hyp->ctx[i].vtimer_cpu;
		callout_drain(&vtimer_cpu->callout);
	}
}

static inline void
vtimer_inject_irq(struct hypctx *hypctx)
{
	struct hyp *hyp = hypctx->hyp;

	vgic_v3_inject_irq(hypctx, hyp->vtimer.phys_ns_irq, VGIC_IRQ_CLK);
}

static void
vtimer_inject_irq_callout_func(void *context)
{
	struct hypctx *hypctx;

	hypctx = (struct hypctx *)context;
	vtimer_inject_irq(hypctx);
}

int
vtimer_init(uint64_t cnthctl_el2)
{
	cnthctl_el2_reg = cnthctl_el2;
	/*
	 * The guest *MUST* use the same timer frequency as the host. The
	 * register CNTFRQ_EL0 is accessible to the guest and a different value
	 * in the guest dts file might have unforseen consequences.
	 */
	tmr_frq = READ_SPECIALREG(cntfrq_el0);

	return (0);
}

void
vtimer_vminit(void *arg)
{
	struct hyp *hyp;

	hyp = (struct hyp *)arg;
	/*
	 * Configure the Counter-timer Hypervisor Control Register for the VM.
	 *
	 * ~CNTHCTL_EL1PCEN: trap access to CNTP_{CTL, CVAL, TVAL}_EL0 from EL1
	 * CNTHCTL_EL1PCTEN: don't trap access to CNTPCT_EL0
	 */
	hyp->vtimer.cnthctl_el2 = cnthctl_el2_reg & ~CNTHCTL_EL1PCEN;
	hyp->vtimer.cnthctl_el2 |= CNTHCTL_EL1PCTEN;

	return;
}

void
vtimer_cpuinit(void *arg)
{
	struct hypctx *hypctx;
	struct vtimer_cpu *vtimer_cpu;

	hypctx = (struct hypctx *)arg;
	vtimer_cpu = &hypctx->vtimer_cpu;

	/*
	 * Configure timer interrupts for the VCPU.
	 *
	 * CNTP_CTL_IMASK: mask interrupts
	 * ~CNTP_CTL_ENABLE: disable the timer
	 */
	vtimer_cpu->cntp_ctl_el0 = CNTP_CTL_IMASK & ~CNTP_CTL_ENABLE;
	vtimer_cpu->cntv_ctl_el0 = CNTP_CTL_IMASK & ~CNTP_CTL_ENABLE;

	/*
	 * Callout function is MP_SAFE because the VGIC uses a spin mutex when
	 * modifying the list registers.
	 */
	callout_init(&vtimer_cpu->callout, 1);
}

#define timer_condition_met(ctl)	((ctl) & CNTP_CTL_ISTATUS)

static int count = 0;

int
vtimer_virtual_timer_intr(void *arg)
{
	struct hypctx *hypctx;
	uint32_t cntv_ctl;

	hypctx = arg;

	cntv_ctl = READ_SPECIALREG(cntv_ctl_el0);

//	eprintf("cntv_ctl = 0x%08x\n", cntv_ctl);

	if (!timer_enabled(cntv_ctl)) {
		eprintf("Guest has timer interrupt disabled\n");
		goto out;
	}
	if (!timer_condition_met(cntv_ctl)) {
		eprintf("ISTATUS not set");
		goto out;
	}

	vgic_v3_inject_irq(arg, 27, VGIC_IRQ_CLK);
	count++;
//	eprintf("Injected interrupt. Total: %d\n", count);

out:
	/*
	 * Also mask the timer interrupt for the guest. This will prevent
	 * reasserting the timer interrupt as soon as we enter the guest, ending
	 * up in an infinite loop.
	 *
	 * This is safe to do because the guest masks the timer interrupt as
	 * part of the interrupt handling routine.
	 */
	cntv_ctl &= ~CNTP_CTL_ENABLE;
	WRITE_SPECIALREG(cntv_ctl_el0, cntv_ctl);

	return (FILTER_HANDLED);
}


static void
vtimer_schedule_irq(struct vtimer_cpu *vtimer_cpu, struct hypctx *hypctx)
{
	sbintime_t time;
	uint64_t cntpct_el0;
	uint64_t diff;

	cntpct_el0 = READ_SPECIALREG(cntpct_el0);
	if (vtimer_cpu->cntp_cval_el0 < cntpct_el0) {
		/* Timer set in the past, trigger interrupt */
		vtimer_inject_irq(hypctx);
	} else {
		diff = vtimer_cpu->cntp_cval_el0 - cntpct_el0;
		time = diff * SBT_1S / tmr_frq;
		callout_reset_sbt(&vtimer_cpu->callout, time, 0,
		    vtimer_inject_irq_callout_func, hypctx, 0);
	}
}

static void
vtimer_remove_irq(struct hypctx *hypctx)
{
	struct vtimer_cpu *vtimer_cpu;
	uint32_t irq;

	vtimer_cpu = &hypctx->vtimer_cpu;
	irq = hypctx->hyp->vtimer.phys_ns_irq;

	callout_drain(&vtimer_cpu->callout);
	/*
	 * The interrupt needs to be deactivated here regardless of the callout
	 * function having been executed. The timer interrupt can be masked with
	 * the CNTP_CTL_EL0.IMASK bit instead of reading the IAR register.
	 * Masking the interrupt doesn't remove it from the list registers.
	 */
	vgic_v3_remove_irq(hypctx, irq, true);
}

/*
 * Timer emulation functions.
 *
 * The guest dts is configured to use the physical timer because the Generic
 * Timer can only trap physical timer accesses. This is why we always read the
 * physical counter value when programming the time for the timer interrupt in
 * the guest.
 */

int
vtimer_phys_ctl_read(void *vm, int vcpuid, uint64_t *rval, void *arg)
{
	struct hyp *hyp;
	struct vtimer_cpu *vtimer_cpu;
	uint64_t cntpct_el0;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	vtimer_cpu = &hyp->ctx[vcpuid].vtimer_cpu;

	cntpct_el0 = READ_SPECIALREG(cntpct_el0);
	if (vtimer_cpu->cntp_cval_el0 < cntpct_el0)
		/* Timer condition met */
		*rval = vtimer_cpu->cntp_ctl_el0 | CNTP_CTL_ISTATUS;
	else
		*rval = vtimer_cpu->cntp_ctl_el0 & ~CNTP_CTL_ISTATUS;

	*retu = false;
	return (0);
}

int
vtimer_phys_ctl_write(void *vm, int vcpuid, uint64_t wval, void *arg)
{
	struct hyp *hyp;
	struct hypctx *hypctx;
	struct vtimer_cpu *vtimer_cpu;
	uint64_t ctl_el0;
	bool timer_toggled_on, timer_toggled_off;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	hypctx = &hyp->ctx[vcpuid];
	vtimer_cpu = &hypctx->vtimer_cpu;

	timer_toggled_on = timer_toggled_off = false;
	ctl_el0 = vtimer_cpu->cntp_ctl_el0;

	if (!timer_enabled(ctl_el0) && timer_enabled(wval))
		timer_toggled_on = true;
	if (timer_enabled(ctl_el0) && !timer_enabled(wval))
		timer_toggled_off = true;

	vtimer_cpu->cntp_ctl_el0 = wval;

	if (timer_toggled_on)
		vtimer_schedule_irq(vtimer_cpu, hypctx);
	else if (timer_toggled_off)
		vtimer_remove_irq(hypctx);

	*retu = false;
	return (0);
}

int
vtimer_phys_cval_read(void *vm, int vcpuid, uint64_t *rval, void *arg)
{
	struct hyp *hyp;
	struct vtimer_cpu *vtimer_cpu;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	vtimer_cpu = &hyp->ctx[vcpuid].vtimer_cpu;

	*rval = vtimer_cpu->cntp_cval_el0;

	*retu = false;
	return (0);
}

int
vtimer_phys_cval_write(void *vm, int vcpuid, uint64_t wval, void *arg)
{
	struct hyp *hyp;
	struct hypctx *hypctx;
	struct vtimer_cpu *vtimer_cpu;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	hypctx = &hyp->ctx[vcpuid];
	vtimer_cpu = &hypctx->vtimer_cpu;

	vtimer_cpu->cntp_cval_el0 = wval;

	if (timer_enabled(vtimer_cpu->cntp_ctl_el0)) {
		vtimer_remove_irq(hypctx);
		vtimer_schedule_irq(vtimer_cpu, hypctx);
	}

	*retu = false;
	return (0);
}

int
vtimer_phys_tval_read(void *vm, int vcpuid, uint64_t *rval, void *arg)
{
	struct hyp *hyp;
	struct vtimer_cpu *vtimer_cpu;
	uint32_t cntpct_el0;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	vtimer_cpu = &hyp->ctx[vcpuid].vtimer_cpu;

	if (!(vtimer_cpu->cntp_ctl_el0 & CNTP_CTL_ENABLE)) {
		/*
		 * ARMv8 Architecture Manual, p. D7-2702: the result of reading
		 * TVAL when the timer is disabled is UNKNOWN. I have chosen to
		 * return the maximum value possible on 32 bits which means the
		 * timer will fire very far into the future.
		 */
		*rval = (uint32_t)RES1;
	} else {
		cntpct_el0 = READ_SPECIALREG(cntpct_el0);
		*rval = vtimer_cpu->cntp_cval_el0 - cntpct_el0;
	}

	*retu = false;
	return (0);
}

int
vtimer_phys_tval_write(void *vm, int vcpuid, uint64_t wval, void *arg)
{
	struct hyp *hyp;
	struct hypctx *hypctx;
	struct vtimer_cpu *vtimer_cpu;
	uint64_t cntpct_el0;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	hypctx = &hyp->ctx[vcpuid];
	vtimer_cpu = &hypctx->vtimer_cpu;

	cntpct_el0 = READ_SPECIALREG(cntpct_el0);
	vtimer_cpu->cntp_cval_el0 = (int32_t)wval + cntpct_el0;

	if (timer_enabled(vtimer_cpu->cntp_ctl_el0)) {
		vtimer_remove_irq(hypctx);
		vtimer_schedule_irq(vtimer_cpu, hypctx);
	}

	*retu = false;
	return (0);
}
