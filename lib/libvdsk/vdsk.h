/*-
 * Copyright (c) 2014 Marcel Moolenaar
 * Copyright (c) 2018 Marcelo Araujo <araujo@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: user/marcel/libvdsk/libvdsk/vdsk.h 286996 2015-08-21 15:20:01Z marcel $
 */

#ifndef __VDSK_H__
#define	__VDSK_H__

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "block_if.h"

typedef void *vdskctx;

vdskctx	vdsk_open(const char *, int, size_t);
int	vdsk_close(vdskctx);

off_t	vdsk_capacity(vdskctx);
int	vdsk_sectorsize(vdskctx);

int	vdsk_read(vdskctx, struct blockif_req *, uint8_t *);
int	vdsk_write(vdskctx, struct blockif_req *, uint8_t *);
int	vdsk_trim(vdskctx, unsigned long, off_t arg[2]);
int	vdsk_flush(vdskctx, unsigned long);

#endif /* __VDSK_H__ */
