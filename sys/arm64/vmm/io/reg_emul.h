/*
 * Copyright (C) 2018 Alexandru Elisei <alexandru.elisei@gmail.com>
 * All rights reserved.
 *
 * This software was developed by Alexandru Elisei under sponsorship from
 * the FreeBSD Foundation.
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

#ifndef _VMM_REG_EMUL_H_
#define	_VMM_REG_EMUL_H_

#define	CNTP_CTL_EL0_OP0	0b11
#define	CNTP_CTL_EL0_OP1	0b011
#define	CNTP_CTL_EL0_OP2	0b001
#define	CNTP_CTL_EL0_CRn	0b1110
#define	CNTP_CTL_EL0_CRm	0b0010

#define	CNTP_CVAL_EL0_OP0	0b11
#define	CNTP_CVAL_EL0_OP1	0b011
#define	CNTP_CVAL_EL0_OP2	0b010
#define	CNTP_CVAL_EL0_CRn	0b1110
#define	CNTP_CVAL_EL0_CRm	0b0010

#define	CNTP_TVAL_EL0_OP0	0b11
#define	CNTP_TVAL_EL0_OP1	0b011
#define	CNTP_TVAL_EL0_OP2	0b000
#define	CNTP_TVAL_EL0_CRn	0b1110
#define	CNTP_TVAL_EL0_CRm	0b0010

#define	ISS_MATCH_REG(reg, iss)			\
    (ISS_MSR_CRm(iss) == reg ##_CRm &&		\
    ISS_MSR_CRn(iss) == reg ##_CRn &&		\
    ISS_MSR_OP1(iss) == reg ##_OP1 &&		\
    ISS_MSR_OP2(iss) == reg ##_OP2 &&		\
    ISS_MSR_OP0(iss) == reg ##_OP0)

#endif /* !_VMM_REG_EMUL_H_ */
