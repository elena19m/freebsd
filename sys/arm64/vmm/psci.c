/*
 * Copyright (C) 2018 Alexandru Elisei <alexandru.elisei@gmail.com>
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
#include <sys/kernel.h>

#include <dev/psci/psci.h>

#include "arm64.h"
#include "psci.h"

#define PSCI_VERSION_0_2	0x2

int
handle_psci_call(struct hypctx *hypctx, bool *retu)
{
	uint64_t func_id;
	uint32_t esr_el2, esr_iss;
	int error;

	esr_el2 = hypctx->exit_info.esr_el2;
	esr_iss = esr_el2 & ESR_ELx_ISS_MASK;

	if (esr_iss != 0) {
		eprintf("Malformed HVC instruction with immediate: 0x%x\n",
		    esr_iss);
		error = 1;
		goto out;
	}

	func_id = hypctx->regs.x[0];

	error = 0;
	switch (func_id) {
	case PSCI_FNID_VERSION:
		hypctx->regs.x[0] = PSCI_VERSION_0_2;
		break;
	default:
		eprintf("Unimplemented PSCI function: 0x%016lx\n", func_id);
		hypctx->regs.x[0] = PSCI_RETVAL_NOT_SUPPORTED;
		error = 1;
	}

out:
	*retu = false;
	return (error);
}
