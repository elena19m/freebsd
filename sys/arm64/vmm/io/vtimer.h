/*-
 * Copyright (c) 2017 The FreeBSD Foundation
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

#ifndef _VMM_VTIMER_H_
#define _VMM_VTIMER_H_

#define	CNTP_CTL_EL0_OP0	0b11
#define	CNTP_CTL_EL0_OP2	0b001
#define	CNTP_CTL_EL0_OP1	0b011
#define	CNTP_CTL_EL0_CRn	0b1110
#define	CNTP_CTL_EL0_CRm	0b0010
#define	ISS_CNTP_CTL_EL0	\
    (CNTP_CTL_EL0_OP0 << ISS_MSR_OP0_SHIFT | 	\
     CNTP_CTL_EL0_OP2 << ISS_MSR_OP2_SHIFT |	\
     CNTP_CTL_EL0_OP1 << ISS_MSR_OP1_SHIFT | 	\
     CNTP_CTL_EL0_CRn << ISS_MSR_CRn_SHIFT |	\
     CNTP_CTL_EL0_CRm << ISS_MSR_CRm_SHIFT)

#define	CNTP_CVAL_EL0_OP0	0b11
#define	CNTP_CVAL_EL0_OP1	0b011
#define	CNTP_CVAL_EL0_OP2	0b010
#define	CNTP_CVAL_EL0_CRn	0b1110
#define	CNTP_CVAL_EL0_CRm	0b0010
#define	ISS_CNTP_CVAL_EL0	\
    (CNTP_CVAL_EL0_OP0 << ISS_MSR_OP0_SHIFT | 	\
     CNTP_CVAL_EL0_OP2 << ISS_MSR_OP2_SHIFT |	\
     CNTP_CVAL_EL0_OP1 << ISS_MSR_OP1_SHIFT | 	\
     CNTP_CVAL_EL0_CRn << ISS_MSR_CRn_SHIFT |	\
     CNTP_CVAL_EL0_CRm << ISS_MSR_CRm_SHIFT)

#define	CNTP_TVAL_EL0_OP0	0b11
#define	CNTP_TVAL_EL0_OP1	0b011
#define	CNTP_TVAL_EL0_OP2	0b000
#define	CNTP_TVAL_EL0_CRn	0b1110
#define	CNTP_TVAL_EL0_CRm	0b0010
#define	ISS_CNTP_TVAL_EL0	\
    (CNTP_TVAL_EL0_OP0 << ISS_MSR_OP0_SHIFT | 	\
     CNTP_TVAL_EL0_OP2 << ISS_MSR_OP2_SHIFT |	\
     CNTP_TVAL_EL0_OP1 << ISS_MSR_OP1_SHIFT | 	\
     CNTP_TVAL_EL0_CRn << ISS_MSR_CRn_SHIFT |	\
     CNTP_TVAL_EL0_CRm << ISS_MSR_CRm_SHIFT)

struct vtimer
{
	uint64_t	cnthctl_el2;
	uint64_t	cntvoff_el2;
	int		phys_ns_irq;
	bool		attached;
};

struct vtimer_cpu
{
	struct callout	callout;
	uint64_t	tmr_freq;
	uint32_t	cntkctl_el1;
	/*
	 * Emulated registers:
	 *
	 * CNTP_CTL_EL0:  Counter-timer Physical Timer Control Register
	 * CNTP_CVAL_EL0: Counter-timer Physical Timer CompareValue Register
	 */
	uint64_t	cntp_cval_el0;
	uint32_t	cntp_ctl_el0;
	/*
	 * The virtual machine has full access to the virtual timer. The
	 * following registers are part of the VM context for the current CPU:
	 *
	 * CNTV_CTL_EL0:  Counter-timer Virtuel Timer Control Register
	 * CNTV_CVAL_EL0: Counter-timer Virtual Timer CompareValue Register
	 */
	uint64_t	cntv_cval_el0;
	uint32_t	cntv_ctl_el0;
};

int	vtimer_attach_to_vm(void *arg, int phys_ns_irq);
void	vtimer_detach_from_vm(void *arg);
int 	vtimer_init(uint64_t cnthctl_el2);
void 	vtimer_vminit(void *arg);
void 	vtimer_cpuinit(void *arg);

int 	vtimer_phys_ctl_read(void *vm, int vcpuid, uint64_t *rval, void *arg);
int 	vtimer_phys_ctl_write(void *vm, int vcpuid, uint64_t wval, void *arg);
int 	vtimer_phys_cval_read(void *vm, int vcpuid, uint64_t *rval, void *arg);
int 	vtimer_phys_cval_write(void *vm, int vcpuid, uint64_t wval, void *arg);
int 	vtimer_phys_tval_read(void *vm, int vcpuid, uint64_t *rval, void *arg);
int 	vtimer_phys_tval_write(void *vm, int vcpuid, uint64_t wval, void *arg);
#endif
