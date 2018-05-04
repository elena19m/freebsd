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

#define	USECS_PER_SEC	1000000

#define	VTIMER_IRQ	27
#define	IRQ_LEVEL	1

extern struct timecounter arm_tmr_timecount;

static inline uint64_t
vtimer_read_ptimer(void)
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
	vtimer->virt_irq = virt_irq;

	return (0);
}

static bool
vtimer_started(struct vtimer_cpu *vtimer_cpu)
{
	/* TODO */
	return (false);
	//return (vtimer_cpu->started);
}

static void
vtimer_callout_cb(void *arg)
{
	struct hypctx *hypctx = arg;

	taskqueue_enqueue(taskqueue_thread, &hypctx->vtimer_cpu.task);
}

static void
vtimer_start(struct hypctx *hypctx, uint64_t usecs)
{
	hypctx->vtimer_cpu.started = true;
	(void)callout_reset_sbt(&hypctx->vtimer_cpu.callout, usecs * SBT_1US, 0,
						vtimer_callout_cb, hypctx, 0);
}

static void
vtimer_stop(struct hypctx *hypctx)
{
	if (vtimer_started(&hypctx->vtimer_cpu)) {
		callout_drain(&hypctx->vtimer_cpu.callout);
		//taskqueue_cancel(taskqueue_thread, &hypctx->vtimer_cpu.task, NULL);
		hypctx->vtimer_cpu.started = false;
	}
}

static void
vtimer_inject_irq(struct hypctx *hypctx)
{
	struct vtimer_cpu *vtimer_cpu = &hypctx->vtimer_cpu;

	vtimer_cpu->cntv_ctl_el0 |= 1 << 1;
	vgic_v3_inject_irq(hypctx, VTIMER_IRQ, IRQ_LEVEL);
}

static void
vtimer_inject_irq_task(void *context, int pending)
{
	struct hypctx *hypctx = context;

	if (hypctx->vtimer_cpu.started) {
		hypctx->vtimer_cpu.started = false;
		vtimer_inject_irq(hypctx);
	}
}

void
vtimer_flush_hwstate(void *arg)
{
	struct hypctx *hypctx = arg;

	vtimer_stop(hypctx);
}

void
vtimer_sync_hwstate(void *arg)
{
	struct hypctx *hypctx = arg;
	uint64_t cval, diff, usecs;

	if ((hypctx->vtimer_cpu.cntv_ctl_el0 & 3) != 1)
		return;

	cval = hypctx->vtimer_cpu.cntv_cval_el0;
	diff = vtimer_read_ptimer() - hypctx->hyp->vtimer.cntvoff;

	if (cval <= diff) {
		vtimer_inject_irq(hypctx);
		return;
	}

	usecs = (USECS_PER_SEC * (cval - diff)) / arm_tmr_timecount.tc_frequency;
	vtimer_start(hypctx, usecs);
}

void
vtimer_cpu_init(void *arg)
{
	struct hypctx *hypctx = arg;
	struct vtimer_cpu *vtimer_cpu = &hypctx->vtimer_cpu;

	callout_init(&vtimer_cpu->callout, 0);

	TASK_INIT(&vtimer_cpu->task, 0, vtimer_inject_irq_task, hypctx);
}

void
vtimer_cpu_terminate(void *arg)
{
	struct hypctx *hypctx = arg;

	vtimer_stop(hypctx);
}

int
vtimer_hyp_init(void)
{
	// TODO Get interrupt number

	return (0);
}

int
vtimer_init(void *arg)
{
	struct hyp *hyp = arg;

	hyp->vtimer.cntvoff = vtimer_read_ptimer();
	hyp->vtimer.enabled = true;

	return (0);
}
