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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/taskqueue.h>

#include "vmm.h"
#include "vgic_v3.h"
#include "arm64.h"
#include "vtimer.h"
#include "reg_emul.h"

#define	USECS_PER_SEC	1000000

#define	IRQ_LEVEL	1
#define	RES1		0xffffffffffffffffUL

#define vtimer_enabled(ctl)	\
    (!((ctl) & CNTP_CTL_IMASK) && ((ctl) & CNTP_CTL_ENABLE))

extern struct timecounter arm_tmr_timecount;

uint64_t cnthctl_el2_reg;

static inline uint64_t
vtimer_read_pct(void)
{
	uint64_t (*get_cntxct)(bool) = arm_tmr_timecount.tc_priv;

	return (get_cntxct(true));
}

int
vtimer_attach_to_vm(void *arg, int phys_ns_irq, int virt_irq)
{
	struct hyp *hyp;
	struct vtimer *vtimer;

	hyp = (struct hyp *)arg;
	vtimer = &hyp->vtimer;

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

static void
vtimer_inject_irq(struct hypctx *hypctx)
{
	struct hyp *hyp;
	struct virq virq;

	hyp = hypctx->hyp;

	virq.irq = hyp->vtimer.phys_ns_irq;
	virq.type = VIRQ_TYPE_CLK;
	vgic_v3_inject_irq(hypctx, &virq);
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

int
vtimer_read_reg(void *vm, int vcpuid, uint64_t *rval, uint32_t inst_syndrome,
    void *arg)
{
	struct hyp *hyp;
	struct vtimer_cpu *vtimer_cpu;
	uint64_t cntpct_el0;
	bool *retu;

	retu = (bool *)arg;
	hyp = vm_get_cookie(vm);
	vtimer_cpu = &hyp->ctx[vcpuid].vtimer_cpu;

	if (ISS_MATCH_REG(CNTP_CTL_EL0, inst_syndrome)) {
		cntpct_el0 = vtimer_read_pct();
		if (vtimer_cpu->cntp_cval_el0 < cntpct_el0)
			/* Timer condition met */
			*rval = vtimer_cpu->cntp_ctl_el0 | CNTP_CTL_ISTATUS;
		else
			*rval = vtimer_cpu->cntp_ctl_el0;

	} else if (ISS_MATCH_REG(CNTP_CVAL_EL0, inst_syndrome)) {
		*rval = vtimer_cpu->cntp_cval_el0;

	} else if (ISS_MATCH_REG(CNTP_TVAL_EL0, inst_syndrome)) {
		if (!(vtimer_cpu->cntp_ctl_el0 & CNTP_CTL_ENABLE)) {
			/*
			 * ARMv8 Architecture Manual, p. D7-2702: the result of
			 * reading TVAL when the timer is disabled is UNKNOWN. I
			 * have chosen to return the maximum value possible on
			 * 32 bits which means the timer will fire very far into
			 * the future.
			 */
			*rval = (uint32_t)RES1;
		} else {
			cntpct_el0 = vtimer_read_pct();
			*rval = vtimer_cpu->cntp_cval_el0 - cntpct_el0;
		}

	} else {
		eprintf("Uknown register\n");
		*rval = 0;
		goto out_user;
	}

	*retu = false;
	return (0);

out_user:
	eprintf("Exiting to user\n");
	*retu = true;
	return (0);
}

static void
vtimer_deactivate_irq(struct hypctx *hypctx)
{
	struct vtimer_cpu *vtimer_cpu;
	struct virq virq;

	vtimer_cpu = &hypctx->vtimer_cpu;

	callout_drain(&vtimer_cpu->callout);
	/*
	 * The interrupt needs to be deactivated here regardless of the callout
	 * function having been executed. The timer interrupt can be controlled
	 * by using the CNTP_CTL_EL0.IMASK bit instead of reading the IAR
	 * register which has the effect that disabling the timer interrupt
	 * doesn't remove it from the list registers.
	 */
	virq.irq = hypctx->hyp->vtimer.phys_ns_irq;
	virq.type = VIRQ_TYPE_CLK;
	vgic_v3_deactivate_irq(hypctx, &virq, true);
}

int
vtimer_write_reg(void *vm, int vcpuid, uint64_t wval, uint32_t inst_syndrome,
    void *arg)
{
	struct hyp *hyp;
	struct hypctx *hypctx;
	struct vtimer_cpu *vtimer_cpu;
	uint64_t cntpct_el0, ctl_el0, diff;
	sbintime_t time;
	bool int_toggled_on, int_toggled_off, cval_changed;
	bool *retu;

	retu = (bool *)arg;
	hyp = vm_get_cookie(vm);
	hypctx = &hyp->ctx[vcpuid];
	vtimer_cpu = &hypctx->vtimer_cpu;

	int_toggled_on = int_toggled_off = cval_changed = false;
	ctl_el0 = vtimer_cpu->cntp_ctl_el0;

	if (ISS_MATCH_REG(CNTP_CTL_EL0, inst_syndrome)) {
		if (!vtimer_enabled(ctl_el0) && vtimer_enabled(wval))
			int_toggled_on = true;
		if (vtimer_enabled(ctl_el0) && !vtimer_enabled(wval))
			int_toggled_off = true;
		/* ISTATUS will be set on read when timer condition is met */
		vtimer_cpu->cntp_ctl_el0 = wval & ~CNTP_CTL_ISTATUS;

	} else if (ISS_MATCH_REG(CNTP_CVAL_EL0, inst_syndrome)) {
		cval_changed = true;
		vtimer_cpu->cntp_cval_el0 = wval;

	} else if (ISS_MATCH_REG(CNTP_TVAL_EL0, inst_syndrome)) {
		cval_changed = true;
		cntpct_el0 = vtimer_read_pct();
		vtimer_cpu->cntp_cval_el0 = (int32_t)wval + cntpct_el0;

	} else {
		eprintf("Uknown register\n");
		goto out_user;
	}

	if (int_toggled_on || (cval_changed && vtimer_enabled(ctl_el0))) {
		if (cval_changed)
			vtimer_deactivate_irq(hypctx);
		cntpct_el0 = vtimer_read_pct();
		if (vtimer_cpu->cntp_cval_el0 < cntpct_el0) {
			vtimer_inject_irq(hypctx);
		} else {
			diff = vtimer_cpu->cntp_cval_el0 - cntpct_el0;
			time = diff * SBT_1S / arm_tmr_timecount.tc_frequency;
			callout_reset_sbt(&vtimer_cpu->callout, time, 0,
			    vtimer_inject_irq_callout_func, hypctx, 0);
		}
	} else if (int_toggled_off) {
		vtimer_deactivate_irq(hypctx);
		//eprintf("Interrupts toggled OFF\n");
	}

	*retu = false;
	return (0);

out_user:
	eprintf("Exiting to user\n");
	*retu = true;
	return (0);
}
