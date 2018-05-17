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
		taskqueue_cancel_timeout(taskqueue_thread, &vtimer_cpu->task,
		    NULL);
		taskqueue_drain_timeout(taskqueue_thread, &vtimer_cpu->task);
	}
}

static void
vtimer_inject_irq(struct hypctx *hypctx)
{
	struct hyp *hyp;
	int irq;
	struct vtimer_cpu *vtimer_cpu = &hypctx->vtimer_cpu;

	hyp = hypctx->hyp;
	irq = hyp->vtimer.phys_ns_irq;

	vtimer_cpu->cntp_ctl_el0 |= 1 << 1;
	vgic_v3_inject_irq(hypctx, irq, IRQ_LEVEL);
}

static void
vtimer_inject_irq_task(void *context, int pending)
{
	eprintf("hello world!\n");
	/*
	struct hypctx *hypctx = context;

	if (hypctx->vtimer_cpu.started) {
		hypctx->vtimer_cpu.started = false;
		vtimer_inject_irq(hypctx);
	}
	*/
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
	hyp->vtimer.cnthctl_el2 = \
	    (cnthctl_el2_reg & ~CNTHCTL_EL1PCEN) | CNTHCTL_EL1PCTEN;

	return;
}

void
vtimer_cpu_init(void *arg)
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

	TIMEOUT_TASK_INIT(taskqueue_thread, &vtimer_cpu->task, 0,
	    vtimer_inject_irq_task, hypctx);
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
			vtimer_cpu->cntp_ctl_el0 |= CNTP_CTL_ISTATUS;
		*rval = vtimer_cpu->cntp_ctl_el0;
		eprintf("CNTP_CTL_EL0 = 0x%x\n", vtimer_cpu->cntp_ctl_el0);

	} else if (ISS_MATCH_REG(CNTP_CVAL_EL0, inst_syndrome)) {
		eprintf("CNTP_CVAL_EL0 = 0x%lx\n", vtimer_cpu->cntp_cval_el0);
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
		eprintf("CNTP_TVAL_EL0 = 0x%lx\n", *rval);

	} else {
		eprintf("Uknown register\n");
		*rval = 0;
		goto out_user;
	}

	*retu = false;
	return (0);

out_user:
	*retu = true;
	return (0);
}

static inline void
vtimer_enqueue_irq(uint64_t cval, uint64_t pct, struct timeout_task *task)
{
	uint64_t diff;

	if (cval < pct)
		diff = 0;
	else
		diff = cval - pct;
	taskqueue_enqueue_timeout(taskqueue_thread, task, diff);
}

int
vtimer_write_reg(void *vm, int vcpuid, uint64_t wval, uint32_t inst_syndrome,
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
		/* ISTATUS is set when timer condition is met */
		wval &= ~CNTP_CTL_ISTATUS;
		wval &= CNTP_CTL_RES0;
		vtimer_cpu->cntp_ctl_el0 = wval;
		eprintf("CNTP_CTL_EL0 = 0x%lx\n", wval);

	} else if (ISS_MATCH_REG(CNTP_CVAL_EL0, inst_syndrome)) {
		vtimer_cpu->cntp_cval_el0 = wval;
		cntpct_el0 = vtimer_read_pct();
		vtimer_enqueue_irq(vtimer_cpu->cntp_cval_el0, cntpct_el0,
		    &vtimer_cpu->task);
		eprintf("CNTP_CVAL_EL0 = 0x%lx\n", wval);

	} else if (ISS_MATCH_REG(CNTP_TVAL_EL0, inst_syndrome)) {
		cntpct_el0 = vtimer_read_pct();
		vtimer_cpu->cntp_cval_el0 = (int32_t)wval + cntpct_el0;
		vtimer_enqueue_irq(vtimer_cpu->cntp_cval_el0, cntpct_el0,
		    &vtimer_cpu->task);
		eprintf("CNTP_TVAL_EL0, wval = 0x%lx, cval = 0x%lx\n",
		    wval, vtimer_cpu->cntp_cval_el0);

	} else {
		eprintf("Uknown register\n");
		goto out_user;
	}

	*retu = false;
	return (0);

out_user:
	*retu = true;
	return (0);
}
