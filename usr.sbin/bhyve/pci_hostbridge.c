/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "devemu.h"

static int
pci_hostbridge_init(struct vmctx *ctx, struct devemu_inst *di, char *opts)
{

	/* config space */
	devemu_set_cfgdata16(di, PCIR_VENDOR, 0x1275);	/* NetApp */
	devemu_set_cfgdata16(di, PCIR_DEVICE, 0x1275);	/* NetApp */
	devemu_set_cfgdata8(di, PCIR_HDRTYPE, PCIM_HDRTYPE_NORMAL);
	devemu_set_cfgdata8(di, PCIR_CLASS, PCIC_BRIDGE);
	devemu_set_cfgdata8(di, PCIR_SUBCLASS, PCIS_BRIDGE_HOST);

	pci_emul_add_pciecap(di, PCIEM_TYPE_ROOT_PORT);

	return (0);
}

static int
pci_amd_hostbridge_init(struct vmctx *ctx, struct devemu_inst *di, char *opts)
{
	(void) pci_hostbridge_init(ctx, di, opts);
	devemu_set_cfgdata16(di, PCIR_VENDOR, 0x1022);	/* AMD */
	devemu_set_cfgdata16(di, PCIR_DEVICE, 0x7432);	/* made up */

	return (0);
}

struct devemu_dev pci_de_amd_hostbridge = {
	.de_emu = "amd_hostbridge",
	.de_init = pci_amd_hostbridge_init,
};
DEVEMU_SET(pci_de_amd_hostbridge);

struct devemu_dev pci_de_hostbridge = {
	.de_emu = "hostbridge",
	.de_init = pci_hostbridge_init,
};
DEVEMU_SET(pci_de_hostbridge);
