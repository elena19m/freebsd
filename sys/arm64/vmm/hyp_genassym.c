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

ASSYM(HYPCTX_HYP, offsetof(struct hypctx, hyp));
ASSYM(HYP_VTTBR, offsetof(struct hyp, vttbr));

ASSYM(HYP_VTIMER_ENABLED, offsetof(struct hyp, vtimer.enabled));
ASSYM(HYP_VTIMER_CNTVOFF, offsetof(struct hyp, vtimer.cntvoff));

ASSYM(HYPCTX_MIDR, offsetof(struct hypctx, midr));
ASSYM(HYPCTX_MPIDR, offsetof(struct hypctx, mpidr));
ASSYM(HYPCTX_HCR, offsetof(struct hypctx, hcr));

ASSYM(HYPCTX_SP_und, offsetof(struct hypctx, sp_und));
ASSYM(HYPCTX_LR_und, offsetof(struct hypctx, lr_und));
ASSYM(HYPCTX_SPSR_und, offsetof(struct hypctx, spsr_und));
ASSYM(HYPCTX_SP_svc, offsetof(struct hypctx, sp_svc));
ASSYM(HYPCTX_LR_svc, offsetof(struct hypctx, lr_svc));
ASSYM(HYPCTX_SPSR_svc, offsetof(struct hypctx, spsr_svc));
ASSYM(HYPCTX_SP_abt, offsetof(struct hypctx, sp_abt));
ASSYM(HYPCTX_LR_abt, offsetof(struct hypctx, lr_abt));
ASSYM(HYPCTX_SPSR_abt, offsetof(struct hypctx, spsr_abt));
ASSYM(HYPCTX_SP_irq, offsetof(struct hypctx, sp_irq));
ASSYM(HYPCTX_LR_irq, offsetof(struct hypctx, lr_irq));
ASSYM(HYPCTX_SPSR_irq, offsetof(struct hypctx, spsr_irq));
ASSYM(HYPCTX_SP_fiq, offsetof(struct hypctx, sp_fiq));
ASSYM(HYPCTX_LR_fiq, offsetof(struct hypctx, lr_fiq));
ASSYM(HYPCTX_SPSR_fiq, offsetof(struct hypctx, spsr_fiq));
ASSYM(HYPCTX_r8_fiq, offsetof(struct hypctx, r8_fiq));
ASSYM(HYPCTX_r9_fiq, offsetof(struct hypctx, r9_fiq));
ASSYM(HYPCTX_r10_fiq, offsetof(struct hypctx, r10_fiq));
ASSYM(HYPCTX_r11_fiq, offsetof(struct hypctx, r11_fiq));
ASSYM(HYPCTX_r12_fiq, offsetof(struct hypctx, r12_fiq));

ASSYM(HYPCTX_REGS, offsetof(struct hypctx, regs));
//ASSYM(HYPCTX_REGS_LR, offsetof(struct hypctx, regs.r_lr));
ASSYM(HYPCTX_REGS_LR, offsetof(struct hypctx, regs.lr));
//ASSYM(HYPCTX_REGS_SP, offsetof(struct hypctx, regs.r_sp));
ASSYM(HYPCTX_REGS_SP, offsetof(struct hypctx, regs.sp));
//ASSYM(HYPCTX_REGS_PC, offsetof(struct hypctx, regs.r_pc));
ASSYM(HYPCTX_REGS_PC, offsetof(struct hypctx, regs.elr));
//ASSYM(HYPCTX_REGS_CPSR, offsetof(struct hypctx, regs.r_cpsr));
ASSYM(HYPCTX_REGS_CPSR, offsetof(struct hypctx, regs.spsr));


ASSYM(HYPCTX_CP15_SCTLR, offsetof(struct hypctx, cp15_sctlr));
ASSYM(HYPCTX_CP15_CPACR, offsetof(struct hypctx, cp15_cpacr));
ASSYM(HYPCTX_CP15_TTBCR, offsetof(struct hypctx, cp15_ttbcr));
ASSYM(HYPCTX_CP15_DACR, offsetof(struct hypctx, cp15_dacr));
ASSYM(HYPCTX_CP15_TTBR0, offsetof(struct hypctx, cp15_ttbr0));
ASSYM(HYPCTX_CP15_TTBR1, offsetof(struct hypctx, cp15_ttbr1));
ASSYM(HYPCTX_CP15_PRRR, offsetof(struct hypctx, cp15_prrr));
ASSYM(HYPCTX_CP15_NMRR, offsetof(struct hypctx, cp15_nmrr));
ASSYM(HYPCTX_CP15_CSSELR, offsetof(struct hypctx, cp15_csselr));
ASSYM(HYPCTX_CP15_CID, offsetof(struct hypctx, cp15_cid));
ASSYM(HYPCTX_CP15_TID_URW, offsetof(struct hypctx, cp15_tid_urw));
ASSYM(HYPCTX_CP15_TID_URO, offsetof(struct hypctx, cp15_tid_uro));
ASSYM(HYPCTX_CP15_TID_PRIV, offsetof(struct hypctx, cp15_tid_priv));
ASSYM(HYPCTX_CP15_DFSR, offsetof(struct hypctx, cp15_dfsr));
ASSYM(HYPCTX_CP15_IFSR, offsetof(struct hypctx, cp15_ifsr));
ASSYM(HYPCTX_CP15_ADFSR, offsetof(struct hypctx, cp15_adfsr));
ASSYM(HYPCTX_CP15_AIFSR, offsetof(struct hypctx, cp15_aifsr));
ASSYM(HYPCTX_CP15_DFAR, offsetof(struct hypctx, cp15_dfar));
ASSYM(HYPCTX_CP15_IFAR, offsetof(struct hypctx, cp15_ifar));
ASSYM(HYPCTX_CP15_VBAR, offsetof(struct hypctx, cp15_vbar));
ASSYM(HYPCTX_CP15_CNTKCTL, offsetof(struct hypctx, cp15_cntkctl));
ASSYM(HYPCTX_CP15_PAR, offsetof(struct hypctx, cp15_par));
ASSYM(HYPCTX_CP15_AMAIR0, offsetof(struct hypctx, cp15_amair0));
ASSYM(HYPCTX_CP15_AMAIR1, offsetof(struct hypctx, cp15_amair1));

ASSYM(HYPCTX_EXIT_INFO_HSR, offsetof(struct hypctx, exit_info.hsr));
ASSYM(HYPCTX_EXIT_INFO_HDFAR, offsetof(struct hypctx, exit_info.hdfar));
ASSYM(HYPCTX_EXIT_INFO_HIFAR, offsetof(struct hypctx, exit_info.hifar));
ASSYM(HYPCTX_EXIT_INFO_HPFAR, offsetof(struct hypctx, exit_info.hpfar));

ASSYM(HYPCTX_VGIC_INT_CTRL, offsetof(struct hypctx, vgic_cpu_int.virtual_int_ctrl));
ASSYM(HYPCTX_VGIC_LR_NUM, offsetof(struct hypctx, vgic_cpu_int.lr_num));
ASSYM(HYPCTX_VGIC_HCR, offsetof(struct hypctx, vgic_cpu_int.hcr));
ASSYM(HYPCTX_VGIC_VMCR, offsetof(struct hypctx, vgic_cpu_int.vmcr));
ASSYM(HYPCTX_VGIC_MISR, offsetof(struct hypctx, vgic_cpu_int.misr));
ASSYM(HYPCTX_VGIC_EISR, offsetof(struct hypctx, vgic_cpu_int.eisr));
ASSYM(HYPCTX_VGIC_ELSR, offsetof(struct hypctx, vgic_cpu_int.elsr));
ASSYM(HYPCTX_VGIC_APR, offsetof(struct hypctx, vgic_cpu_int.apr));
ASSYM(HYPCTX_VGIC_LR, offsetof(struct hypctx, vgic_cpu_int.lr));

ASSYM(HYPCTX_VTIMER_CPU_CNTV_CTL, offsetof(struct hypctx, vtimer_cpu.cntv_ctl));
ASSYM(HYPCTX_VTIMER_CPU_CNTV_CVAL, offsetof(struct hypctx, vtimer_cpu.cntv_cval));

#ifdef VFP
ASSYM(HYPCTX_HOST_VFP_STATE, offsetof(struct hypctx, host_vfp_state));
ASSYM(HYPCTX_GUEST_VFP_STATE, offsetof(struct hypctx, guest_vfp_state));
#endif
