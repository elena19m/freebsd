/* * Copyright (C) 2015 Mihai Carabas <mihai.carabas@gmail.com> * All rights reserved.
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

#include <sys/types.h>
#include <sys/select.h>

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <stdbool.h>

#include "mem.h"

#define	BVM_CONS_PORT		0x090000
#define	BVM_CONS_SIG		('b' << 8 | 'v')

static struct termios tio_orig, tio_new;

static void
ttyclose(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &tio_orig);
}

static void
ttyopen(void)
{
	tcgetattr(STDIN_FILENO, &tio_orig);

	cfmakeraw(&tio_new);
	tcsetattr(STDIN_FILENO, TCSANOW, &tio_new);

	atexit(ttyclose);
}

static bool
tty_char_available(void)
{
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
		return (true);
	} else {
		return (false);
	}
}

static int
ttyread(void)
{
	char rb;

	if (tty_char_available()) {
		read(STDIN_FILENO, &rb, 1);
		return (rb & 0xff);
	} else {
		return (-1);
	}
}

static void
ttywrite(unsigned char wb)
{
	(void) write(STDOUT_FILENO, &wb, 1);
}

static int
console_handler(struct vmctx *ctx, int vcpu, int dir, uint64_t addr, int size, uint64_t *val, void *arg1, long arg2)
{
	static int opened;

	if (size == 2 && dir == MEM_F_READ) {
		*val = BVM_CONS_SIG;
		return (0);
	}

	/*
	 * Guests might probe this port to look for old ISA devices
	 * using single-byte reads.  Return 0xff for those.
	 */
	if (size == 1 && dir == MEM_F_READ) {
		*val = 0xff;
		return (0);
	}

	if (size != 4)
		return (-1);

	if (!opened) {
		ttyopen();
		opened = 1;
	}

	if (dir == MEM_F_READ)
		*val = ttyread();
	else
		ttywrite(*val);
	return (0);
}

struct mem_range consport ={
	"bvmcons",
	MEM_F_RW,
	console_handler,
	NULL,
	0,
	BVM_CONS_PORT,
	sizeof(int)
};

void
init_bvmcons(void)
{
	register_mem(&consport);
}
