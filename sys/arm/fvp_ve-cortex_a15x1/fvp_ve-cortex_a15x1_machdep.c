/*-
 * Copyright (c) 2012 Oleksandr Tymoshenko.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "opt_ddb.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: head/sys/arm/versatile/versatile_machdep.c 274668 2014-11-18 17:06:56Z imp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/devmap.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/smp.h>

#include <vm/vm.h>

#include <machine/bus.h>
#include <machine/platform.h> 
#include <machine/platformvar.h>
#include <machine/cpu.h>
#include <machine/cpu-v6.h>
#include <machine/smp.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <vm/pmap.h>

#include "fvp_ve-cortex_a15x1_semihosting.h"

#include "platform_if.h"

#define FVP_RESET_PORT 0x1c090100

#ifdef EARLY_PRINTF
static void
eputc(int c)
{
	char str[2];
	str[0] = c;
	str[1] = 0;

	__semi_call(SYS_WRITE0, str);
}
early_putc_t * early_putc = eputc;
#endif

static int
fvp_devmap_init(platform_t plat)
{
	/* UART0 (PL011) */
	devmap_add_entry(0x1c090000, 0x4000);
	return (0);
}

static void
fvp_cpu_reset(platform_t plat)
{
	int reset_port = (int) pmap_mapdev(FVP_RESET_PORT, 0x1000);
	*((char *) reset_port) = 'r';
	printf("Reset failed!\n");
	while (1);
}

static platform_method_t fvp_methods[] = {
	PLATFORMMETHOD(platform_devmap_init,	fvp_devmap_init),
	PLATFORMMETHOD(platform_cpu_reset,	fvp_cpu_reset),
	PLATFORMMETHOD_END,
};
FDT_PLATFORM_DEF(fvp, "fvp", 0, "arm,fvp_ve,cortex_a15x1", 1);

