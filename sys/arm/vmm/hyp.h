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

#ifndef _VMM_HYP_H_
#define	_VMM_HYP_H_

/* Hyp Exceptions */
#define EXCEPTION_RESET		0
#define EXCEPTION_UNDEF		1
#define EXCEPTION_SVC		2
#define EXCEPTION_PABT		3
#define EXCEPTION_DABT		4
#define EXCEPTION_HVC		5
#define EXCEPTION_FIQ		6
#define EXCEPTION_IRQ		7



#define	HSR_EC_SHIFT		26
#define	HSR_IL_SHIFT		25
#define	HSR_IL_MASK		(1 << HSR_IL_SHIFT)
#define	HSR_ISS_MASK		((1 << 25) - 1)

#define HSR_EC(x)		(x >> HSR_EC_SHIFT)
#define	HSR_IL(x)		((x & HSR_IL_MASK) >> HSR_IL_SHIFT)
#define	HSR_ISS(x)		(x & HSR_ISS_MASK)

#define	HSR_EC_UNKN		0x00
#define	HSR_EC_WFI_WFE		0x01
#define	HSR_EC_MCR_MRC_CP15	0x03
#define	HSR_EC_MCRR_MRRC_CP15	0x04
#define	HSR_EC_MCR_MRC_CP14	0x05
#define	HSR_EC_LDC_STC_CP14	0x06
#define	HSR_EC_HCPTR_CP0_CP13	0x07
#define	HSR_EC_MRC_VMRS_CP10	0x08
#define	HSR_EC_BXJ		0x0A
#define	HSR_EC_MRRC_CP14	0x0C

#define	HSR_EC_SVC		0x11
#define	HSR_EC_HVC		0x12
#define	HSR_EC_SMC		0x13
#define	HSR_EC_PABT		0x20
#define	HSR_EC_PABT_HYP		0x21
#define	HSR_EC_DABT		0x24
#define	HSR_EC_DABT_HYP		0x25

#define HSR_ISS_ISV(x)		((x >> 24) & 1)
#define HSR_ISS_SAS(x)		((x >> 22) & 3)
#define HSR_ISS_SSE(x)		((x >> 21) & 1)
#define HSR_ISS_SRT(x)		((x >> 16) & 0xf)
#define HSR_ISS_EA(x)		((x >> 9) & 1)
#define HSR_ISS_CM(x)		((x >> 8) & 1)
#define HSR_ISS_S1PTW(x)	((x >> 7) & 1)
#define HSR_ISS_WnR(x)		((x >> 6) & 1)
#define HSR_ISS_DFSC(x)		((x >> 0) & 0x3f)

#define HSR_ISS_ACCESS_SIZE(x)	((x == 0) ? 1 : (x == 1) ? 2 : 4)


#define	VTTBR_VMID_SHIFT	16
#define	VTTBR_VMID_MASK		0xff

/* Hyp System Control Register (HSCTLR) bits */
#define HSCTLR_TE	(1 << 30)
#define HSCTLR_EE	(1 << 25)
#define HSCTLR_FI	(1 << 21)
#define HSCTLR_WXN	(1 << 19)
#define HSCTLR_I	(1 << 12)
#define HSCTLR_C	(1 << 2)
#define HSCTLR_A	(1 << 1)
#define HSCTLR_M	(1 << 0)
#define HSCTLR_MASK	(HSCTLR_M | HSCTLR_A | HSCTLR_C | HSCTLR_I | HSCTLR_WXN | HSCTLR_FI | HSCTLR_EE | HSCTLR_TE)
/* Hyp Coprocessor Trap Register */
#define HCPTR_TCP(x)	(1 << x)
#define HCPTR_TCP_MASK	(0x3fff)
#define HCPTR_TASE	(1 << 15)
#define HCPTR_TTA	(1 << 20)
#define HCPTR_TCPAC	(1 << 31)

/* TTBCR and HTCR Registers bits */
#define TTBCR_EAE	(1 << 31)
#define TTBCR_IMP	(1 << 30)
#define TTBCR_SH1	(3 << 28)
#define TTBCR_ORGN1	(3 << 26)
#define TTBCR_IRGN1	(3 << 24)
#define TTBCR_EPD1	(1 << 23)
#define TTBCR_A1	(1 << 22)
#define TTBCR_T1SZ	(7 << 16)
#define TTBCR_SH0	(3 << 12)
#define TTBCR_ORGN0	(3 << 10)
#define TTBCR_IRGN0	(3 << 8)
#define TTBCR_EPD0	(1 << 7)
#define TTBCR_T0SZ	(7 << 0)
#define HTCR_MASK	(TTBCR_T0SZ | TTBCR_IRGN0 | TTBCR_ORGN0 | TTBCR_SH0)

/* Virtualization Translation Control Register (VTCR) bits */
#define VTCR_RES	(1 << 31)
#define VTCR_SH0	(3 << 12)
#define VTCR_ORGN0	(3 << 10)
#define VTCR_IRGN0	(3 << 8)
#define VTCR_SL0	(3 << 6)
#define VTCR_S		(1 << 4)
#define VTCR_T0SZ	(0xf)
#define VTCR_MASK	(VTCR_SH0 | VTCR_ORGN0 | VTCR_IRGN0 | VTCR_SL0 | VTCR_S | VTCR_T0SZ)
#define VTCR_HTCR_SH	(VTCR_SH0 | VTCR_ORGN0 | VTCR_IRGN0)
#define VTCR_SL_L1	(1 << 6)	/* Starting-level: 1 */
/* Stage 2 address input size is 2^(32-VTCR T0SZ) (ARM - B4.1.159) */
#define VMM_IPA_LEN	32
#define VMM_VTCR_T0SZ	((32 - VMM_IPA_LEN) & VTCR_T0SZ)
/* The sign bit VTCR.S = VTCR.T0SZ[4] */
#define VMM_VTCR_S	(((VMM_VTCR_T0SZ) << 1) & VTCR_S)

/* Hyp Configuration Register (HCR) bits */
#define HCR_TGE		(1 << 27)
#define HCR_TVM		(1 << 26)
#define HCR_TTLB	(1 << 25)
#define HCR_TPU		(1 << 24)
#define HCR_TPC		(1 << 23)
#define HCR_TSW		(1 << 22)
#define HCR_TAC		(1 << 21)
#define HCR_TIDCP	(1 << 20)
#define HCR_TSC		(1 << 19)
#define HCR_TID3	(1 << 18)
#define HCR_TID2	(1 << 17)
#define HCR_TID1	(1 << 16)
#define HCR_TID0	(1 << 15)
#define HCR_TWE		(1 << 14)
#define HCR_TWI		(1 << 13)
#define HCR_DC		(1 << 12)
#define HCR_BSU		(3 << 10)
#define HCR_BSU_IS	(1 << 10)
#define HCR_FB		(1 << 9)
#define HCR_VA		(1 << 8)
#define HCR_VI		(1 << 7)
#define HCR_VF		(1 << 6)
#define HCR_AMO		(1 << 5)
#define HCR_IMO		(1 << 4)
#define HCR_FMO		(1 << 3)
#define HCR_PTW		(1 << 2)
#define HCR_SWIO	(1 << 1)
#define HCR_VM		1
/* 
 * B4.1.65 HCR, Hyp Configuration Register,
 *
 * HCR_TSW - Trap set/way cache maintenance operations
 * HCR_TAC - Trap ACTLR accessses
 * HCR_TIDCP - Trap lockdown
 * HCR_TSC - Trap SMC instruction
 * HCR_TWE - Trap WFE instruction
 * HCR_TWI - Trap WFI instruction
 * HCR_BSU_IS - Barrier shareability upgrade
 * HCR_FB - Force broadcast TLB/branch predictor/ cache invalidate across ISB
 * HCR_AMO - Overrides the CPSR.A bit, and enables signaling by the VA bit
 * HCR_IMO - Overrides the CPSR.I bit, and enables signaling by the VI bit
 * HCR_FMO - Overrides the CPSR.F bit, and enables signaling by the VF bit
 * HCR_SWIO - Set/way invalidation override
 * HCR_VM - Virtualization MMU enable (stage 2)
 */
#define HCR_GUEST_MASK (HCR_TSW | HCR_TAC | HCR_TIDCP | \
    HCR_TSC | HCR_TWI | HCR_BSU_IS | HCR_FB | \
    HCR_AMO | HCR_IMO | HCR_FMO | HCR_SWIO | HCR_VM)

/* Hyp Coprocessor Trap Register */
#define HCPTR_TCP(x)	(1 << x)
#define HCPTR_TCP_MASK	(0x3fff)
#define HCPTR_TASE	(1 << 15)
#define HCPTR_TTA	(1 << 20)
#define HCPTR_TCPAC	(1 << 31)

/* Hyp System Trap Register */
#define HSTR_T(x)	(1 << x)
#define HSTR_TTEE	(1 << 16)
#define HSTR_TJDBX	(1 << 17)

/*
 * Memory region attributes for LPAE (defined in pgtable-3level.h):
 *
 *   n = AttrIndx[2:0]
 *
 *                        n     MAIR
 *   UNCACHED		000	00000000
 *   BUFFERABLE		001	01000100
 *   DEV_WC		001	01000100
 *   WRITETHROUGH	010	10101010
 *   WRITEBACK		011	11101110
 *   DEV_CACHED		011	11101110
 *   DEV_SHARED		100	00000100
 *   DEV_NONSHARED	100	00000100
 *   unused		101
 *   unused		110
 *   WRITEALLOC		111	11111111
 */
#define MAIR0		0xeeaa4400
#define MAIR1		0xff000004
#define HMAIR0		MAIR0
#define HMAIR1		MAIR1

#define	HYPCTX_REGS_R(x)	(HYPCTX_REGS + x * 4)

#endif

