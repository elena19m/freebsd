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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#ifndef WITHOUT_CAPSICUM
#include <sys/capsicum.h>
#include <capsicum_helpers.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <machine/vmm.h>
#ifndef WITHOUT_CAPSICUM
#include <machine/vmm_dev.h>
#endif
#include <vmmapi.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <sys/mman.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <unistd.h>

#include "migration.h"
#include "pci_emul.h"
#include "snapshot.h"

#define MB		(1024UL * 1024)
#define GB		(1024UL * MB)

int
receive_vm_migration(struct vmctx *ctx, char *migration_data)
{
	struct migrate_req req;
	char *hostname, *pos;
	int rc;

	memset(req.host, 0, MAX_HOSTNAME_LEN);
	hostname = strdup(migration_data);

	if ((pos = strchr(hostname, ',')) != NULL ) {
		*pos = '\0';
		strncpy(req.host, hostname, MAX_HOSTNAME_LEN);
		pos = pos + 1;

		rc = sscanf(pos, "%d", &(req.port));

		if (rc == 0) {
			fprintf(stderr, "Could not parse the port\r\n");
			free(hostname);
			return -1;
		}
	} else {
		strncpy(req.host, hostname, MAX_HOSTNAME_LEN);

		/* If only one variable could be read, it should be the host */
		req.port = DEFAULT_MIGRATION_PORT;
	}

	rc = vm_recv_migrate_req(ctx, req);

	free(hostname);
	return (rc);
}

/*
 * This represents the implementation of the warm migration feature for bhyve.
 */

static int
get_system_specs_for_migration(struct migration_system_specs *specs)
{
	int mib[2];
	size_t len_machine, len_model, len_pagesize;
	char interm[MAX_SPEC_LEN];
	int rc;
	int num;

	mib[0] = CTL_HW;

	mib[1] = HW_MACHINE;
	memset(interm, 0, MAX_SPEC_LEN);
	len_machine = sizeof(interm);
	rc = sysctl(mib, 2, interm, &len_machine, NULL, 0);
	if (rc != 0) {
		perror("Could not retrieve HW_MACHINE specs");
		return (rc);
	}
	strncpy(specs->hw_machine, interm, MAX_SPEC_LEN);

	memset(interm, 0, MAX_SPEC_LEN);
	mib[0] = CTL_HW;
	mib[1] = HW_MODEL;
	len_model = sizeof(interm);
	rc = sysctl(mib, 2, interm, &len_model, NULL, 0);
	if (rc != 0) {
		perror("Could not retrieve HW_MODEL specs");
		return (rc);
	}
	strncpy(specs->hw_model, interm, MAX_SPEC_LEN);

	mib[0] = CTL_HW;
	mib[1] = HW_PAGESIZE;
	len_pagesize = sizeof(num);
	rc = sysctl(mib, 2, &num, &len_pagesize, NULL, 0);
	if (rc != 0) {
		perror("Could not retrieve HW_PAGESIZE specs");
		return (rc);
	}
	specs->hw_pagesize = num;

	return (0);
}

static int
migration_send_data_remote(int socket, const void *msg, size_t len)
{
	size_t to_send, total_sent;
	ssize_t sent;

	to_send = len;
	total_sent = 0;

	while (to_send > 0) {
		sent  = send(socket, msg + total_sent, to_send, 0);
		if (sent < 0) {
			perror("Error while sending data");
			return (sent);
		}

		to_send -= sent;
		total_sent += sent;
	}

	return (0);
}

static int
migration_recv_data_from_remote(int socket, void *msg, size_t len)
{
	size_t to_recv, total_recv;
	ssize_t recvt;

	to_recv = len;
	total_recv = 0;

	while (to_recv > 0) {
		recvt = recv(socket, msg + total_recv, to_recv, 0);
		if (recvt == 0) {
			break;
		}
		if (recvt < 0) {
			perror("Error while receiving data");
			return (recvt);
		}

		to_recv -= recvt;
		total_recv += recvt;
	}

	return (0);
}

static int
migration_send_specs(int socket)
{
	struct migration_system_specs local_specs;
	struct migration_message_type mesg;
	size_t response;
	int rc;

	rc = get_system_specs_for_migration(&local_specs);
	if (rc != 0) {
		fprintf(stderr, "%s: Could not retrieve local specs\r\n",
			__func__);
		return (rc);
	}

	/* Send message type to server: specs & len */
	mesg.type = MESSAGE_TYPE_SPECS;
	mesg.len = sizeof(local_specs);
	rc = migration_send_data_remote(socket, &mesg, sizeof(mesg));
	if (rc < 0) {
		fprintf(stderr, "%s: Could not send message type\r\n", __func__);
		return (-1);
	}

	/* Send specs to server */
	rc = migration_send_data_remote(socket, &local_specs, sizeof(local_specs));
	if (rc < 0) {
		fprintf(stderr, "%s: Could not send system specs\r\n", __func__);
		return (-1);
	}

	/* Recv OK/NOT_OK from server */
	rc = migration_recv_data_from_remote(socket, &response, sizeof(response));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not receive response from server\r\n",
			__func__);
		return (-1);
	}

	/*  Return OK/NOT_OK */
	if (response == MIGRATION_SPECS_NOT_OK) {
		fprintf(stderr,
			"%s: System specification mismatch\r\n",
			__func__);
		return (-1);
	}

	fprintf(stdout, "%s: System specification accepted\r\n", __func__);

	return (0);
}

static int
migration_recv_and_check_specs(int socket)
{
	struct migration_system_specs local_specs;
	struct migration_system_specs remote_specs;
	struct migration_message_type msg;
	size_t response;
	int rc;

	/* TODO1: Get specs size from remote (from client) */
	rc = migration_recv_data_from_remote(socket, &msg, sizeof(msg));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not receive message type for specs from remote\r\n",
			__func__);
		return (rc);
	}

	if (msg.type != MESSAGE_TYPE_SPECS) {
		fprintf(stderr,
			"%s: Wrong message type received from remote\r\n",
			__func__);
		return (-1);
	}

	/* Get specs from remote (from client) */
	rc = migration_recv_data_from_remote(socket, &remote_specs, msg.len);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not receive specs from remote\r\n",
			__func__);
		return (rc);
	}

	rc = get_system_specs_for_migration(&local_specs);

	if (rc != 0) {
		fprintf(stderr, "%s: Could not get local specs\r\n", __func__);
		return (rc);
	}

	/* Check specs */
	response = MIGRATION_SPECS_OK;
	if ((strncmp(local_specs.hw_model, remote_specs.hw_model, MAX_SPEC_LEN) != 0)
		|| (strncmp(local_specs.hw_machine, remote_specs.hw_machine, MAX_SPEC_LEN) != 0)
		|| (local_specs.hw_pagesize  != remote_specs.hw_pagesize)
	   ) {
		fprintf(stderr, "%s: System specification mismatch\r\n", __func__);

#ifdef BHYVE_DEBUG
		fprintf(stderr,
			"%s: Local specs vs Remote Specs: \r\n"
			"\tmachine: %s vs %s\r\n"
			"\tmodel: %s vs %s\r\n"
			"\tpagesize: %zu vs %zu\r\n",
			__func__,
			local_specs.hw_machine,
			remote_specs.hw_machine,
			local_specs.hw_model,
			remote_specs.hw_model,
			local_specs.hw_pagesize,
			remote_specs.hw_pagesize
			);
#endif
		response = MIGRATION_SPECS_NOT_OK;
	}

	/* Send OK/NOT_OK to client */
	rc = migration_send_data_remote(socket, &response, sizeof(response));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send response to remote\r\n",
			__func__);
		return (-1);
	}

	if (response == MIGRATION_SPECS_NOT_OK)
		return (-1);

	return (0);
}

static int
get_migration_host_and_type(const char *hostname, unsigned char *ipv4_addr,
				unsigned char *ipv6_addr, int *type)
{
	struct addrinfo hints, *res;
	void *addr;
	int rc;

	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;

	rc = getaddrinfo(hostname, NULL, &hints, &res);

	if (rc != 0) {
		fprintf(stderr, "%s: Could not get address info\r\n", __func__);
		return (-1);
	}

	*type = res->ai_family;
	switch(res->ai_family) {
		case AF_INET:
			addr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
			inet_ntop(res->ai_family, addr, ipv4_addr, MAX_IP_LEN);
			printf("hostname %s\r\n", ipv4_addr);
			break;
		case AF_INET6:
			addr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
			inet_ntop(res->ai_family, addr, ipv6_addr, MAX_IP_LEN);
			printf("hostname %s\r\n", ipv6_addr);
			break;
		default:
			fprintf(stderr, "%s: Unknown ai_family.\r\n", __func__);
			return (-1);
	}

	return (0);
}

static int
migrate_check_memsize(size_t local_lowmem_size, size_t local_highmem_size,
		      size_t remote_lowmem_size, size_t remote_highmem_size)
{
	int ret = MIGRATION_SPECS_OK;

	if (local_lowmem_size != remote_lowmem_size){
		ret = MIGRATION_SPECS_NOT_OK;
		fprintf(stderr,
			"%s: Local and remote lowmem size mismatch\r\n",
			__func__);
	}

	if (local_highmem_size != remote_highmem_size){
		ret = MIGRATION_SPECS_NOT_OK;
		fprintf(stderr,
			"%s: Local and remote highmem size mismatch\r\n",
			__func__);
	}

	return (ret);
}

static int
migrate_recv_memory(struct vmctx *ctx, int socket)
{
	size_t local_lowmem_size = 0, local_highmem_size = 0;
	size_t remote_lowmem_size = 0, remote_highmem_size = 0;
	char *baseaddr;
	int memsize_ok;
	int rc = 0;

	rc = vm_get_guestmem_from_ctx(ctx,
			&baseaddr, &local_lowmem_size,
			&local_highmem_size);
	if (rc != 0) {
		fprintf(stderr,
			"%s: Could not get guest lowmem size and highmem size\r\n",
			__func__);
		return (rc);
	}

	/* Receive the size of the lowmem segment */
	rc = migration_recv_data_from_remote(socket,
			&remote_lowmem_size,
			sizeof(size_t));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv lowmem size\r\n",
			__func__);
		return (rc);
	}

	/* Recieve the size of the highmem segment */
	rc = migration_recv_data_from_remote(socket,
			&remote_highmem_size,
			sizeof(size_t));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv highmem size\r\n",
			__func__);
		return (rc);
	}

	/* Check if local {low,high}mem is equal with remote {low,high}mem */
	memsize_ok = migrate_check_memsize(local_lowmem_size, local_highmem_size,
					   remote_lowmem_size, remote_highmem_size);

	rc = migration_send_data_remote(socket,
			&memsize_ok, sizeof(memsize_ok));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send migration_ok to remote\r\n",
			__func__);
		return (rc);
	}

	if (memsize_ok != MIGRATION_SPECS_OK) {
		fprintf(stderr,
			"%s: Memory size mismatch with remote host\r\n",
			__func__);
		return (-1);
	}

	/* Receive the lowmem segment */
	rc = migration_recv_data_from_remote(socket, baseaddr, local_lowmem_size);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv chunk lowmem.\r\n",
			__func__);
		return (-1);
	}

	/* Receive the highmem segment */
	if (local_highmem_size > 0 ){
		rc = migration_recv_data_from_remote(socket,
				baseaddr + 4 * GB,
				local_highmem_size);
		if (rc < 0) {
			fprintf(stderr,
				"%s: Could not recv highmem\r\n",
				__func__);
			return (-1);
		}
	}

	return (0);
}

static int
migrate_send_memory(struct vmctx *ctx, int socket)
{
	size_t lowmem_size, highmem_size;
	char *mmap_vm_lowmem = MAP_FAILED;
	char *mmap_vm_highmem = MAP_FAILED;
	char *baseaddr;
	int memsize_ok;
	int rc = 0;

	rc = vm_get_guestmem_from_ctx(ctx, &baseaddr,
			&lowmem_size, &highmem_size);
	if (rc != 0) {
		fprintf(stderr,
			"%s: Could not get guest lowmem size and highmem size\r\n",
			__func__);
		return (rc);
	}

	/* Send the size of the lowmem segment */
	rc = migration_send_data_remote(socket, &lowmem_size, sizeof(size_t));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send lowmem size\r\n",
			__func__);
		return (rc);
	}

	/* Send the size of the highmem segment */
	rc = migration_send_data_remote(socket, &highmem_size, sizeof(size_t));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send highmem size\r\n",
			__func__);
		return (rc);
	}

	/* Wait for answer - params ok (if memory size matches) */
	rc = migration_recv_data_from_remote(socket, &memsize_ok, sizeof(memsize_ok));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not receive response from remote\r\n",
			__func__);
		return (rc);
	}

	if (memsize_ok != MIGRATION_SPECS_OK) {
		fprintf(stderr,
			"%s: Memory size mismatch with remote host\r\n",
			__func__);
		return (-1);
	}

	mmap_vm_lowmem = baseaddr;
	mmap_vm_highmem = baseaddr + 4 * GB;

	/* Send the lowmem segment */
	rc = migration_send_data_remote(socket, mmap_vm_lowmem, lowmem_size);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send lowmem\r\n",
			__func__);
		return (-1);
	}

	/* Send the highmem segment */
	if (highmem_size > 0 ){
		rc = migration_send_data_remote(socket, mmap_vm_highmem, highmem_size);
		if (rc < 0) {
			fprintf(stderr,
				"%s: Could not send highmem\r\n",
				__func__);
			return (-1);
		}
	}

	return (0);
}

static inline int
migrate_send_kern_struct(struct vmctx *ctx, int socket,
			 char *buffer,
			 enum snapshot_req struct_req)
{
	int rc;
	size_t data_size;
	struct migration_message_type msg;
	struct vm_snapshot_meta *meta;

	memset(&msg, 0, sizeof(msg));
	msg.type = MESSAGE_TYPE_KERN;

	meta = &(struct vm_snapshot_meta) {
		.ctx = ctx,

		.dev_req = struct_req,

		.buffer.buf_start = buffer,
		.buffer.buf_size = SNAPSHOT_BUFFER_SIZE,

		.op = VM_SNAPSHOT_SAVE,
	};

	memset(meta->buffer.buf_start, 0, meta->buffer.buf_size);
	meta->buffer.buf = meta->buffer.buf_start;
	meta->buffer.buf_rem = meta->buffer.buf_size;

	rc = vm_snapshot_req(meta);

	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not get struct with req %d\r\n",
			__func__,
			struct_req);
		return (-1);
	}

	data_size = vm_get_snapshot_size(meta);
	msg.len = data_size;
	msg.req_type = struct_req;

	rc = migration_send_data_remote(socket, &msg, sizeof(msg));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send struct msg for req %d\r\n",
			__func__,
			struct_req);
		return (-1);
	}

	rc = migration_send_data_remote(socket, buffer, data_size);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send struct with req %d\r\n",
			__func__,
			struct_req);
		return (-1);
	}

	return (0);
}

static inline int
migrate_recv_kern_struct(struct vmctx *ctx, int socket, char *buffer)
{
	int rc;
	struct migration_message_type msg;
	struct vm_snapshot_meta *meta;

	memset(&msg, 0, sizeof(struct migration_message_type));
	rc = migration_recv_data_from_remote(socket, &msg, sizeof(msg));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv struct mesg\r\n",
			__func__);
		return (-1);
	}
	memset(buffer, 0, SNAPSHOT_BUFFER_SIZE);
	rc = migration_recv_data_from_remote(socket, buffer, msg.len);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv struct for req %d\r\n",
			__func__,
			msg.req_type);
		return (-1);
	}

	meta = &(struct vm_snapshot_meta) {
		.ctx = ctx,

		.dev_req = msg.req_type,

		.buffer.buf_start = buffer,
		.buffer.buf_size = msg.len,

		.op = VM_SNAPSHOT_RESTORE,
	};

	meta->buffer.buf = meta->buffer.buf_start;
	meta->buffer.buf_rem = meta->buffer.buf_size;

	rc = vm_snapshot_req(meta);
	if (rc != 0 ) {
		fprintf(stderr,
			"%s: Failed to restore struct %d\r\n",
			__func__,
			msg.req_type);
		return (-1);
	}

	return (0);
}

static int
migrate_kern_data(struct vmctx *ctx, int socket, enum migration_transfer_req req)
{
	int i, rc, error = 0;
	int ndevs;
	char *buffer;
	const struct vm_snapshot_kern_info *snapshot_kern_structs;

	snapshot_kern_structs = get_snapshot_kern_structs(&ndevs);

	buffer = malloc(SNAPSHOT_BUFFER_SIZE * sizeof(char));
	if (buffer == NULL) {
		fprintf(stderr,
			"%s: Could not allocate memory\r\n",
			__func__);
		return (-1);
	}

	for (i = 0; i < ndevs; i++) {
		if (req == MIGRATION_RECV_REQ) {
			rc = migrate_recv_kern_struct(ctx, socket, buffer);
			if (rc < 0) {
				fprintf(stderr,
					"%s: Could not restore struct %s\n",
					__func__,
					snapshot_kern_structs[i].struct_name);
				error = -1;
				break;
			}
		} else if (req == MIGRATION_SEND_REQ) {
			rc = migrate_send_kern_struct(ctx, socket, buffer,
					snapshot_kern_structs[i].req);
			if (rc < 0 ) {
				fprintf(stderr,
					"%s: Could not send %s\r\n",
					__func__,
					snapshot_kern_structs[i].struct_name);
				error = -1;
				break;
			}
		} else {
			fprintf(stderr,
				"%s: Unknown transfer request\r\n",
				__func__);
			error = -1;
			break;
		}
	}

	free(buffer);

	return (error);
}

static inline const struct vm_snapshot_dev_info *
find_entry_for_dev(const char *name)
{
	int i;
	int ndevs;
	const struct vm_snapshot_dev_info *snapshot_devs;

	snapshot_devs = get_snapshot_devs(&ndevs);

	for (i = 0; i < ndevs; i++) {
		if (strncmp(name, snapshot_devs[i].dev_name, MAX_DEV_NAME_LEN) == 0) {
			return (&snapshot_devs[i]);
		}
	}

	return NULL;
}

static inline int
migrate_send_dev(struct vmctx *ctx, int socket, const char *dev,
		     char *buffer, size_t len)
{
	int rc;
	size_t data_size;
	struct migration_message_type msg;
	struct vm_snapshot_meta *meta;
	const struct vm_snapshot_dev_info *dev_info;

	data_size = 0;
	memset(buffer, 0, len);
	dev_info = find_entry_for_dev(dev);
	if (dev_info == NULL) {
	    fprintf(stderr, "%s: Could not find the device %s "
		    "or migration not implemented yet for it."
		    "Please check if you have the same OS version installed.\r\n",
		    __func__, dev);
	    return (0);
	}


	meta = &(struct vm_snapshot_meta) {
		.ctx = ctx,

		.dev_name = dev,

		.buffer.buf_start = buffer,
		.buffer.buf_size = SNAPSHOT_BUFFER_SIZE,

		.op = VM_SNAPSHOT_SAVE,
	};

	memset(meta->buffer.buf_start, 0, meta->buffer.buf_size);
	meta->buffer.buf = meta->buffer.buf_start;
	meta->buffer.buf_rem = meta->buffer.buf_size;

	rc = (*dev_info->snapshot_cb)(meta);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not get info about %s dev\r\n",
			__func__,
			dev);
		return (-1);
	}

	data_size = vm_get_snapshot_size(meta);

	memset(&msg, 0, sizeof(msg));
	msg.type = MESSAGE_TYPE_DEV;
	msg.len = data_size;
	strncpy(msg.name, dev, MAX_DEV_NAME_LEN);

	rc = migration_send_data_remote(socket, &msg, sizeof(msg));
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send msg for %s dev\r\n",
			__func__,
			dev);
		return (-1);
	}

	if (data_size == 0) {
		fprintf(stderr, "%s: Did not send %s dev. Assuming unused. "
			"Continuing...\r\n", __func__, dev);
		return (0);
	}

	rc = migration_send_data_remote(socket, buffer, data_size);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send %s dev\r\n",
			__func__,
			dev);
		return (-1);
	}

	return (0);
}

static int
migrate_recv_dev(struct vmctx *ctx, int socket, char *buffer, size_t len)
{
	int rc;
	size_t data_size;
	struct migration_message_type msg;
	struct vm_snapshot_meta *meta;
	const struct vm_snapshot_dev_info *dev_info;

	memset(&msg, 0, sizeof(msg));

	rc = migration_recv_data_from_remote(socket, &msg, sizeof(msg));
	if (rc < 0) {
		fprintf(stderr, "%s: Could not recv msg for device.\r\n", __func__);
		return (-1);
	}

	data_size = msg.len;

	if(data_size == 0) {
		fprintf(stderr, "%s: Did not restore %s dev. Assuming unused. "
			"Continuing...\r\n", __func__, msg.name);
		return (0);
	}

	memset(buffer, 0 , len);
	rc = migration_recv_data_from_remote(socket, buffer, data_size);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv %s dev\r\n",
			__func__,
			msg.name);
		return (-1);
	}

	dev_info = find_entry_for_dev(msg.name);
	if (dev_info == NULL) {
	    fprintf(stderr, "%s: Could not find the device %s "
		    "or migration not implemented yet for it."
		    "Please check if you have the same OS version installed.\r\n",
		    __func__, msg.name);
	    return (0);
	}

	meta = &(struct vm_snapshot_meta) {
		.ctx = ctx,
		.dev_name = msg.name,

		.buffer.buf_start = buffer,
		.buffer.buf_size = data_size,

		.op = VM_SNAPSHOT_RESTORE,
	};

	meta->buffer.buf = meta->buffer.buf_start;
	meta->buffer.buf_rem = meta->buffer.buf_size;

	rc = (*dev_info->snapshot_cb)(meta);
	if (rc != 0) {
		fprintf(stderr,
			"%s: Could not restore %s dev\r\n",
			__func__,
			msg.name);
		return (-1);
	}

	return (0);
}


static int
migrate_devs(struct vmctx *ctx, int socket, enum migration_transfer_req req)
{
	int i, num_items;
	int rc, error = 0;
	char *buffer;
	const struct vm_snapshot_dev_info *snapshot_devs;

	buffer = malloc(SNAPSHOT_BUFFER_SIZE * sizeof(char));
	if (buffer == NULL) {
		fprintf(stderr,
			"%s: Could not allocate memory\r\n",
			__func__);
		error = -1;
		goto end;
	}

	if (req == MIGRATION_SEND_REQ) {
		/*
		 * Send to the destination the number of devices that will
		 * be migrated.
		 */
		snapshot_devs = get_snapshot_devs(&num_items);
		rc = migration_send_data_remote(socket, &num_items, sizeof(num_items));

		if (rc < 0) {
		    fprintf(stderr, "%s: Could not send num_items to destination\r\n", __func__);
		    return (-1);
		}

		for (i = 0; i < num_items; i++) {
			rc = migrate_send_dev(ctx, socket, snapshot_devs[i].dev_name,
						buffer, SNAPSHOT_BUFFER_SIZE);

			if (rc < 0) {
				fprintf(stderr,
					"%s: Could not send %s\r\n",
					__func__, snapshot_devs[i].dev_name);
				error = -1;
				goto end;
			}
	    }
	} else if (req == MIGRATION_RECV_REQ) {
		/* receive the number of devices that will be migrated */
		rc = migration_recv_data_from_remote(socket, &num_items, sizeof(num_items));

		if (rc < 0) {
		    fprintf(stderr, "%s: Could not recv num_items from source\r\n", __func__);
		    return (-1);
		}

		for (i = 0; i < num_items; i ++) {
			rc = migrate_recv_dev(ctx, socket, buffer, SNAPSHOT_BUFFER_SIZE);
			if (rc < 0) {
				fprintf(stderr,
				    "%s: Could not recv device\r\n",
				    __func__);
				error = -1;
				goto end;
			}
		}
	}

	error = 0;

end:
	if (buffer != NULL)
		free(buffer);

	return (error);
}

static inline int
migrate_connections(struct migrate_req req, int *socket_fd,
		    int *connection_socket_fd,
		    enum migration_transfer_req type)
{
	unsigned char ipv4_addr[MAX_IP_LEN];
	unsigned char ipv6_addr[MAX_IP_LEN];
	int addr_type;
	int error;
	int s, con_socket;
	struct sockaddr_in sa, client_sa;
	socklen_t client_len;
	int rc;

	rc = get_migration_host_and_type(req.host, ipv4_addr,
					 ipv6_addr, &addr_type);

	if (rc != 0) {
		fprintf(stderr, "%s: Invalid address or not IPv6.\r\n", __func__);
		fprintf(stderr, "%s: IP address used for migration: %s;\r\n"
				"Port used for migration: %d\r\n"
				"Exiting...\r\n",
				__func__,
				req.host,
				req.port);
		return (rc);
	}

	if (addr_type == AF_INET6) {
		fprintf(stderr, "%s: IPv6 is not supported yet for migration. "
				"Please try again using a IPv4 address.\r\n",
				__func__);

		fprintf(stderr, "%s: IP address used for migration: %s;\r\n"
				"Port used for migration: %d\r\n",
				__func__,
				ipv6_addr,
				req.port);
		return (-1);
	}

	s = socket(AF_INET, SOCK_STREAM, 0);

	if (s < 0) {
		perror("Could not create socket");
		return (-1);
	}

	bzero(&sa, sizeof(sa));

	switch (type) {
		case MIGRATION_SEND_REQ:
			fprintf(stdout, "%s: Starting connection to %s on %d port...\r\n",
				__func__, ipv4_addr, req.port);

			sa.sin_family = AF_INET;
			sa.sin_port = htons(req.port);

			rc = inet_pton(AF_INET, ipv4_addr, &sa.sin_addr);
			if (rc <= 0) {
				fprintf(stderr, "%s: Could not retrive the IPV4 address", __func__);
				return (-1);
			}

			rc = connect(s, (struct sockaddr *)&sa, sizeof(sa));

			if (rc < 0) {
				perror("Could not connect to the remote host");
				error = rc;
				goto done_close_s;
			}
			*socket_fd = s;
			break;
		case MIGRATION_RECV_REQ:
			fprintf(stdout, "%s: Waiting for connections from %s on %d port...\r\n",
					__func__, ipv4_addr, req.port);

			sa.sin_family = AF_INET;
			sa.sin_port = htons(req.port);
			sa.sin_addr.s_addr = htonl(INADDR_ANY);

			rc = bind(s, (struct sockaddr *)&sa, sizeof(sa));

			if (rc < 0) {
				perror("Could not bind");
				error = -1;
				goto done_close_s;
			}

			listen(s, 1);

			con_socket = accept(s, (struct sockaddr *)&client_sa, &client_len);

			if (con_socket < 0) {
				fprintf(stderr, "%s: Could not accept connection\r\n", __func__);
				error = -1;
				goto done_close_s;
			}
			*socket_fd = s;
			*connection_socket_fd = con_socket;
			break;
		default:
			fprintf(stderr, "%s: unknown operation request\r\n",
				__func__);
			error = -1;
			goto done;
	}

	error = 0;
	goto done;

done_close_s:
	close(s);
done:
	return (error);
}

int
vm_send_migrate_req(struct vmctx *ctx, struct migrate_req req)
{
	int s;
	int rc, error;
	size_t migration_completed;


	rc = migrate_connections(req, &s, NULL, MIGRATION_SEND_REQ);

	if (rc < 0) {
		fprintf(stderr, "%s: Could not create connection\r\n", __func__);
		return (-1);
	}

	/* Send thw system requirements */
	rc = migration_send_specs(s);

	if (rc < 0) {
		fprintf(stderr, "%s: Error while checking system requirements\r\n",
			__func__);
		error = rc;
		goto done;
	}

	vm_vcpu_pause(ctx);

	rc = vm_pause_user_devs(ctx);
	if (rc != 0) {
		fprintf(stderr, "Could not pause devices\r\n");
		error = rc;
		goto unlock_vm_and_exit;
	}

	rc = migrate_send_memory(ctx, s);
	if (rc != 0) {
		fprintf(stderr,
			"%s: Could not send memory to destination\r\n",
			__func__);
		error = rc;
		goto unlock_vm_and_exit;
	}

	/* Send the state of kernel related devices */
	rc =  migrate_kern_data(ctx, s, MIGRATION_SEND_REQ);
	if (rc != 0) {
		fprintf(stderr,
			"%s: Could not send kern data to destination\r\n",
			__func__);
		error = rc;
		goto unlock_vm_and_exit;
	}

	/* Send the state of the userspace/emulated devices */
	rc =  migrate_devs(ctx, s, MIGRATION_SEND_REQ);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not send pci devs to destination\r\n",
			__func__);
		error = rc;
		goto unlock_vm_and_exit;
	}

	/* Wait for migration completed */
	rc = migration_recv_data_from_remote(s, &migration_completed,
					sizeof(migration_completed));
	if ((rc < 0) || (migration_completed != MIGRATION_SPECS_OK)) {
		fprintf(stderr,
			"%s: Could not recv migration completed remote"
			" or received error\r\n",
			__func__);
		error = -1;
		goto unlock_vm_and_exit;
	}

	/* Poweroff the vm */
	vm_destroy(ctx);
	exit(0);

unlock_vm_and_exit:
	vm_vcpu_resume(ctx);

	rc = vm_resume_user_devs(ctx);
	if (rc != 0)
		fprintf(stderr, "Could not resume devices\r\n");
done:
	close(s);
	return (error);
}

int
vm_recv_migrate_req(struct vmctx *ctx, struct migrate_req req)
{
	int s, con_socket;
	int rc;
	size_t migration_completed;

	rc = migrate_connections(req, &s, &con_socket, MIGRATION_RECV_REQ);
	if (rc != 0) {
		fprintf(stderr, "%s: Could not create connections\r\n", __func__);
		return (-1);
	}

	rc = migration_recv_and_check_specs(con_socket);
	if (rc < 0) {
		fprintf(stderr, "%s: Error while checking specs\r\n", __func__);
		close(con_socket);
		close(s);
		return (rc);
	}

	rc = migrate_recv_memory(ctx, con_socket);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv lowmem and highmem\r\n",
			__func__);
		close(con_socket);
		close(s);
		return (-1);
	}

	rc = migrate_kern_data(ctx, con_socket, MIGRATION_RECV_REQ);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv kern data\r\n",
			__func__);
		close(con_socket);
		close(s);
		return (-1);
	}

	rc = migrate_devs(ctx, con_socket, MIGRATION_RECV_REQ);
	if (rc < 0) {
		fprintf(stderr,
			"%s: Could not recv pci devs\r\n",
			__func__);
		close(con_socket);
		close(s);
		return (-1);
	}

	fprintf(stdout, "%s: Migration completed\r\n", __func__);
	migration_completed = MIGRATION_SPECS_OK;
	rc = migration_send_data_remote(con_socket, &migration_completed,
					sizeof(migration_completed));
	if (rc < 0 ) {
		fprintf(stderr,
			"%s: Could not send migration completed remote\r\n",
			__func__);
		close(con_socket);
		close(s);
		return (-1);
	}

	close(con_socket);
	close(s);
	return (0);
}

