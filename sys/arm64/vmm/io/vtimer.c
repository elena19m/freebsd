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

#define vtimer_enabled(ctl)	\
    (!((ctl) & CNTP_CTL_IMASK) && ((ctl) & CNTP_CTL_ENABLE))

static uint64_t cnthctl_el2_reg;

int
vtimer_attach_to_vm(void *arg, int phys_ns_irq, uint64_t tmr_freq)
{
	struct hyp *hyp = arg;
	struct vtimer *vtimer = &hyp->vtimer;
	int i;

	vtimer->phys_ns_irq = phys_ns_irq;
	vtimer->attached = true;
	for (i = 0; i < VM_MAXCPU; i++)
		hyp->ctx[i].vtimer_cpu.tmr_freq = tmr_freq;

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

	/*
	 * Callout function is MP_SAFE because the VGIC uses a spin mutex when
	 * modifying the list registers.
	 */
	callout_init(&vtimer_cpu->callout, 1);
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
		time = diff * SBT_1S / vtimer_cpu->tmr_freq;
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
	vgic_v3_remove_irq(hypctx, irq);
}

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

	if (!vtimer_enabled(ctl_el0) && vtimer_enabled(wval))
		timer_toggled_on = true;
	if (vtimer_enabled(ctl_el0) && !vtimer_enabled(wval))
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

	if (vtimer_enabled(vtimer_cpu->cntp_ctl_el0)) {
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

	if (vtimer_enabled(vtimer_cpu->cntp_ctl_el0)) {
		vtimer_remove_irq(hypctx);
		vtimer_schedule_irq(vtimer_cpu, hypctx);
	}

	*retu = false;
	return (0);
}
