/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 NetApp, Inc.
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

#include <sys/types.h>

#include <stdio.h>

#include "bhyverun.h"
#include "debug.h"
#include "devemu.h"
#include "uart_emul.h"

/*
 * Pick a PCI vid/did of a chip with a single uart at
 * BAR0, that most versions of FreeBSD can understand:
 * Siig CyberSerial 1-port.
 */
#define COM_VENDOR	0x131f
#define COM_DEV		0x2000

static void
pci_uart_intr_assert(void *arg)
{
	struct devemu_inst *di = arg;

	devemu_lintr_assert(di);
}

static void
pci_uart_intr_deassert(void *arg)
{
	struct devemu_inst *di = arg;

	devemu_lintr_deassert(di);
}

static void
pci_uart_write(struct vmctx *ctx, int vcpu, struct devemu_inst *di,
	       int baridx, uint64_t offset, int size, uint64_t value)
{

	assert(baridx == 0);
	assert(size == 1);

	uart_write(di->di_arg, offset, value);
}

uint64_t
pci_uart_read(struct vmctx *ctx, int vcpu, struct devemu_inst *di,
	      int baridx, uint64_t offset, int size)
{
	uint8_t val;

	assert(baridx == 0);
	assert(size == 1);

	val = uart_read(di->di_arg, offset);
	return (val);
}

static int
pci_uart_init(struct vmctx *ctx, struct devemu_inst *di, char *opts)
{
	struct uart_softc *sc;

	devemu_alloc_bar(di, 0, PCIBAR_IO, UART_IO_BAR_SIZE);
	devemu_lintr_request(di);

	/* initialize config space */
	devemu_set_cfgdata16(di, PCIR_DEVICE, COM_DEV);
	devemu_set_cfgdata16(di, PCIR_VENDOR, COM_VENDOR);
	devemu_set_cfgdata8(di, PCIR_CLASS, PCIC_SIMPLECOMM);

	sc = uart_init(pci_uart_intr_assert, pci_uart_intr_deassert, di);
	di->di_arg = sc;

	if (uart_set_backend(sc, opts) != 0) {
		EPRINTLN("Unable to initialize backend '%s' for "
		    "pci uart at %d:%d", opts, pi->pi_slot, pi->pi_func);
		return (-1);
	}

	return (0);
}

struct devemu_dev pci_de_com = {
	.de_emu =	"uart",
	.de_init =	pci_uart_init,
	.de_write =	pci_uart_write,
	.de_read =	pci_uart_read
};
DEVEMU_SET(pci_de_com);
