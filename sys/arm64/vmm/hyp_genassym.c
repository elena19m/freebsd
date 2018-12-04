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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/assym.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/vmm.h>

#include "arm64.h"

ASSYM(HYPCTX_REGS_X0, offsetof(struct hypctx, regs) + 0 * 8);
ASSYM(HYPCTX_REGS_X1, offsetof(struct hypctx, regs) + 1 * 8);
ASSYM(HYPCTX_REGS_X2, offsetof(struct hypctx, regs) + 2 * 8);
ASSYM(HYPCTX_REGS_X3, offsetof(struct hypctx, regs) + 3 * 8);
ASSYM(HYPCTX_REGS_X4, offsetof(struct hypctx, regs) + 4 * 8);
ASSYM(HYPCTX_REGS_X5, offsetof(struct hypctx, regs) + 5 * 8);
ASSYM(HYPCTX_REGS_X6, offsetof(struct hypctx, regs) + 6 * 8);
ASSYM(HYPCTX_REGS_X7, offsetof(struct hypctx, regs) + 7 * 8);
ASSYM(HYPCTX_REGS_X8, offsetof(struct hypctx, regs) + 8 * 8);
ASSYM(HYPCTX_REGS_X9, offsetof(struct hypctx, regs) + 9 * 8);
ASSYM(HYPCTX_REGS_X10, offsetof(struct hypctx, regs) + 10 * 8);
ASSYM(HYPCTX_REGS_X11, offsetof(struct hypctx, regs) + 11 * 8);
ASSYM(HYPCTX_REGS_X12, offsetof(struct hypctx, regs) + 12 * 8);
ASSYM(HYPCTX_REGS_X13, offsetof(struct hypctx, regs) + 13 * 8);
ASSYM(HYPCTX_REGS_X14, offsetof(struct hypctx, regs) + 14 * 8);
ASSYM(HYPCTX_REGS_X15, offsetof(struct hypctx, regs) + 15 * 8);
ASSYM(HYPCTX_REGS_X16, offsetof(struct hypctx, regs) + 16 * 8);
ASSYM(HYPCTX_REGS_X17, offsetof(struct hypctx, regs) + 17 * 8);
ASSYM(HYPCTX_REGS_X18, offsetof(struct hypctx, regs) + 18 * 8);
ASSYM(HYPCTX_REGS_X19, offsetof(struct hypctx, regs) + 19 * 8);
ASSYM(HYPCTX_REGS_X20, offsetof(struct hypctx, regs) + 20 * 8);
ASSYM(HYPCTX_REGS_X21, offsetof(struct hypctx, regs) + 21 * 8);
ASSYM(HYPCTX_REGS_X22, offsetof(struct hypctx, regs) + 22 * 8);
ASSYM(HYPCTX_REGS_X23, offsetof(struct hypctx, regs) + 23 * 8);
ASSYM(HYPCTX_REGS_X24, offsetof(struct hypctx, regs) + 24 * 8);
ASSYM(HYPCTX_REGS_X25, offsetof(struct hypctx, regs) + 25 * 8);
ASSYM(HYPCTX_REGS_X26, offsetof(struct hypctx, regs) + 26 * 8);
ASSYM(HYPCTX_REGS_X27, offsetof(struct hypctx, regs) + 27 * 8);
ASSYM(HYPCTX_REGS_X28, offsetof(struct hypctx, regs) + 28 * 8);
ASSYM(HYPCTX_REGS_X29, offsetof(struct hypctx, regs) + 29 * 8);
ASSYM(HYPCTX_REGS_LR, offsetof(struct hypctx, regs.lr));
ASSYM(HYPCTX_REGS_SP, offsetof(struct hypctx, regs.sp));
ASSYM(HYPCTX_REGS_ELR, offsetof(struct hypctx, regs.elr));
ASSYM(HYPCTX_REGS_SPSR, offsetof(struct hypctx, regs.spsr));

ASSYM(HYPCTX_ACTLR_EL1, offsetof(struct hypctx, actlr_el1));
ASSYM(HYPCTX_AMAIR_EL1, offsetof(struct hypctx, amair_el1));
ASSYM(HYPCTX_ELR_EL1, offsetof(struct hypctx, elr_el1));
ASSYM(HYPCTX_FAR_EL1, offsetof(struct hypctx, far_el1));
ASSYM(HYPCTX_FP, offsetof(struct hypctx, fp));
ASSYM(HYPCTX_MAIR_EL1, offsetof(struct hypctx, mair_el1));
ASSYM(HYPCTX_PAR_EL1, offsetof(struct hypctx, par_el1));
ASSYM(HYPCTX_SP_EL0, offsetof(struct hypctx, sp_el0));
ASSYM(HYPCTX_TCR_EL1, offsetof(struct hypctx, tcr_el1));
ASSYM(HYPCTX_TPIDR_EL0, offsetof(struct hypctx, tpidr_el0));
ASSYM(HYPCTX_TPIDRRO_EL0, offsetof(struct hypctx, tpidrro_el0));
ASSYM(HYPCTX_TPIDR_EL1, offsetof(struct hypctx, tpidr_el1));
ASSYM(HYPCTX_TTBR0_EL1, offsetof(struct hypctx, ttbr0_el1));
ASSYM(HYPCTX_TTBR1_EL1, offsetof(struct hypctx, ttbr1_el1));
ASSYM(HYPCTX_VBAR_EL1, offsetof(struct hypctx, vbar_el1));
ASSYM(HYPCTX_AFSR0_EL1, offsetof(struct hypctx, afsr0_el1));
ASSYM(HYPCTX_AFSR1_EL1, offsetof(struct hypctx, afsr1_el1));
ASSYM(HYPCTX_CONTEXTIDR_EL1, offsetof(struct hypctx, contextidr_el1));
ASSYM(HYPCTX_CPACR_EL1, offsetof(struct hypctx, cpacr_el1));
ASSYM(HYPCTX_ESR_EL1, offsetof(struct hypctx, esr_el1));
ASSYM(HYPCTX_SCTLR_EL1, offsetof(struct hypctx, sctlr_el1));
ASSYM(HYPCTX_SPSR_EL1, offsetof(struct hypctx, spsr_el1));

ASSYM(HYPCTX_ELR_EL2, offsetof(struct hypctx, elr_el2));
ASSYM(HYPCTX_HCR_EL2, offsetof(struct hypctx, hcr_el2));
ASSYM(HYPCTX_VPIDR_EL2, offsetof(struct hypctx, vpidr_el2));
ASSYM(HYPCTX_VMPIDR_EL2, offsetof(struct hypctx, vmpidr_el2));
ASSYM(HYPCTX_CPTR_EL2, offsetof(struct hypctx, cptr_el2));
ASSYM(HYPCTX_SPSR_EL2, offsetof(struct hypctx, spsr_el2));

ASSYM(HYPCTX_HYP, offsetof(struct hypctx, hyp));

ASSYM(HYP_VTTBR_EL2, offsetof(struct hyp, vttbr_el2));
ASSYM(HYP_VTIMER_CNTHCTL_EL2, offsetof(struct hyp, vtimer.cnthctl_el2));

ASSYM(HYPCTX_EXIT_INFO_ESR_EL2, offsetof(struct hypctx, exit_info.esr_el2));
ASSYM(HYPCTX_EXIT_INFO_FAR_EL2, offsetof(struct hypctx, exit_info.far_el2));
ASSYM(HYPCTX_EXIT_INFO_HPFAR_EL2, offsetof(struct hypctx, exit_info.hpfar_el2));

ASSYM(HYPCTX_VGIC_ICH_LR_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_lr_el2));
ASSYM(HYPCTX_VGIC_ICH_LR_NUM, offsetof(struct hypctx, vgic_cpu_if.ich_lr_num));
ASSYM(HYPCTX_VGIC_ICH_AP0R_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_ap0r_el2));
ASSYM(HYPCTX_VGIC_ICH_AP0R_NUM, offsetof(struct hypctx, vgic_cpu_if.ich_ap0r_num));
ASSYM(HYPCTX_VGIC_ICH_AP1R_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_ap1r_el2));
ASSYM(HYPCTX_VGIC_ICH_AP1R_NUM, offsetof(struct hypctx, vgic_cpu_if.ich_ap1r_num));
ASSYM(HYPCTX_VGIC_ICH_EISR_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_eisr_el2));
ASSYM(HYPCTX_VGIC_ICH_ELSR_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_elsr_el2));
ASSYM(HYPCTX_VGIC_ICH_HCR_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_hcr_el2));
ASSYM(HYPCTX_VGIC_ICH_MISR_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_misr_el2));
ASSYM(HYPCTX_VGIC_ICH_VMCR_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_vmcr_el2));
ASSYM(HYPCTX_VGIC_ICH_LR_EL2, offsetof(struct hypctx, vgic_cpu_if.ich_lr_el2));

ASSYM(HYPCTX_VTIMER_CPU_CNTKCTL_EL1, offsetof(struct hypctx, vtimer_cpu.cntkctl_el1));
ASSYM(HYPCTX_VTIMER_CPU_CNTV_CVAL_EL0, offsetof(struct hypctx, vtimer_cpu.cntv_cval_el0));
ASSYM(HYPCTX_VTIMER_CPU_CNTV_CTL_EL0, offsetof(struct hypctx, vtimer_cpu.cntv_ctl_el0));

#ifdef VFP
ASSYM(HYPCTX_VFPSTATE_Q0, offsetof(struct hypctx, vfpstate.vfp_regs) + 0 * 16);
ASSYM(HYPCTX_VFPSTATE_Q1, offsetof(struct hypctx, vfpstate.vfp_regs) + 1 * 16);
ASSYM(HYPCTX_VFPSTATE_Q2, offsetof(struct hypctx, vfpstate.vfp_regs) + 2 * 16);
ASSYM(HYPCTX_VFPSTATE_Q3, offsetof(struct hypctx, vfpstate.vfp_regs) + 3 * 16);
ASSYM(HYPCTX_VFPSTATE_Q4, offsetof(struct hypctx, vfpstate.vfp_regs) + 4 * 16);
ASSYM(HYPCTX_VFPSTATE_Q5, offsetof(struct hypctx, vfpstate.vfp_regs) + 5 * 16);
ASSYM(HYPCTX_VFPSTATE_Q6, offsetof(struct hypctx, vfpstate.vfp_regs) + 6 * 16);
ASSYM(HYPCTX_VFPSTATE_Q7, offsetof(struct hypctx, vfpstate.vfp_regs) + 7 * 16);
ASSYM(HYPCTX_VFPSTATE_Q8, offsetof(struct hypctx, vfpstate.vfp_regs) + 8 * 16);
ASSYM(HYPCTX_VFPSTATE_Q9, offsetof(struct hypctx, vfpstate.vfp_regs) + 9 * 16);
ASSYM(HYPCTX_VFPSTATE_Q10, offsetof(struct hypctx, vfpstate.vfp_regs) + 10 * 16);
ASSYM(HYPCTX_VFPSTATE_Q11, offsetof(struct hypctx, vfpstate.vfp_regs) + 11 * 16);
ASSYM(HYPCTX_VFPSTATE_Q12, offsetof(struct hypctx, vfpstate.vfp_regs) + 12 * 16);
ASSYM(HYPCTX_VFPSTATE_Q13, offsetof(struct hypctx, vfpstate.vfp_regs) + 13 * 16);
ASSYM(HYPCTX_VFPSTATE_Q14, offsetof(struct hypctx, vfpstate.vfp_regs) + 14 * 16);
ASSYM(HYPCTX_VFPSTATE_Q15, offsetof(struct hypctx, vfpstate.vfp_regs) + 15 * 16);
ASSYM(HYPCTX_VFPSTATE_Q16, offsetof(struct hypctx, vfpstate.vfp_regs) + 16 * 16);
ASSYM(HYPCTX_VFPSTATE_Q17, offsetof(struct hypctx, vfpstate.vfp_regs) + 17 * 16);
ASSYM(HYPCTX_VFPSTATE_Q18, offsetof(struct hypctx, vfpstate.vfp_regs) + 18 * 16);
ASSYM(HYPCTX_VFPSTATE_Q19, offsetof(struct hypctx, vfpstate.vfp_regs) + 19 * 16);
ASSYM(HYPCTX_VFPSTATE_Q20, offsetof(struct hypctx, vfpstate.vfp_regs) + 20 * 16);
ASSYM(HYPCTX_VFPSTATE_Q21, offsetof(struct hypctx, vfpstate.vfp_regs) + 21 * 16);
ASSYM(HYPCTX_VFPSTATE_Q22, offsetof(struct hypctx, vfpstate.vfp_regs) + 22 * 16);
ASSYM(HYPCTX_VFPSTATE_Q23, offsetof(struct hypctx, vfpstate.vfp_regs) + 23 * 16);
ASSYM(HYPCTX_VFPSTATE_Q24, offsetof(struct hypctx, vfpstate.vfp_regs) + 24 * 16);
ASSYM(HYPCTX_VFPSTATE_Q25, offsetof(struct hypctx, vfpstate.vfp_regs) + 25 * 16);
ASSYM(HYPCTX_VFPSTATE_Q26, offsetof(struct hypctx, vfpstate.vfp_regs) + 26 * 16);
ASSYM(HYPCTX_VFPSTATE_Q27, offsetof(struct hypctx, vfpstate.vfp_regs) + 27 * 16);
ASSYM(HYPCTX_VFPSTATE_Q28, offsetof(struct hypctx, vfpstate.vfp_regs) + 28 * 16);
ASSYM(HYPCTX_VFPSTATE_Q29, offsetof(struct hypctx, vfpstate.vfp_regs) + 29 * 16);
ASSYM(HYPCTX_VFPSTATE_Q30, offsetof(struct hypctx, vfpstate.vfp_regs) + 30 * 16);
ASSYM(HYPCTX_VFPSTATE_Q31, offsetof(struct hypctx, vfpstate.vfp_regs) + 31 * 16);


ASSYM(HYPCTX_VFPSTATE_FPCR, offsetof(struct hypctx, vfpstate.vfp_fpcr));
ASSYM(HYPCTX_VFPSTATE_FPSR, offsetof(struct hypctx, vfpstate.vfp_fpsr));
#endif
