/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017-2020 Elena Mihailescu
 * Copyright (c) 2017-2020 Darius Mihai
 * Copyright (c) 2017-2020 Mihai Carabas
 * All rights reserved.
 * The migration feature was developed under sponsorships
 * from Matthew Grooms.
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

#ifndef _BHYVE_MIGRATION_
#define _BHYVE_MIGRATION_

#include <ucl.h>
#include <machine/vmm_dev.h>
#include <vmmapi.h>

struct vmctx;

int receive_vm_migration(struct vmctx *ctx, char *migration_data);

/* Warm Migration */
#define MAX_DEV_NAME_LEN    64

enum migration_transfer_req {
	MIGRATION_SEND_REQ	= 0,
	MIGRATION_RECV_REQ	= 1
};

enum message_types {
    MESSAGE_TYPE_SPECS		= 1,
    MESSAGE_TYPE_METADATA	= 2,
    MESSAGE_TYPE_RAM		= 3,
    MESSAGE_TYPE_KERN		= 4,
    MESSAGE_TYPE_DEV		= 5,
    MESSAGE_TYPE_UNKNOWN	= 8,
};

struct __attribute__((packed)) migration_message_type {
    size_t len;
    unsigned int type;		/* enum message_type */
    unsigned int req_type;	/* enum snapshot_req */
    char name[MAX_DEV_NAME_LEN];
};

struct __attribute__((packed)) migration_system_specs {
	char hw_machine[MAX_SPEC_LEN];
	char hw_model[MAX_SPEC_LEN];
	size_t hw_pagesize;
};

int vm_send_migrate_req(struct vmctx *ctx, struct migrate_req req);
int vm_recv_migrate_req(struct vmctx *ctx, struct migrate_req req);

#endif
