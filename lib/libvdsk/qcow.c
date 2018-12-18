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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: user/marcel/libvdsk/libvdsk/qcow.c 286996 2015-08-21 15:20:01Z marcel $");

#include <sys/disk.h>
#include <sys/endian.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vdsk.h>
#include <pthread.h>

#include "vdsk_int.h"

/* Flag bits in cluster offsets */
#define	QCOW_CLSTR_COMPRESSED	(1ULL << 62)
#define	QCOW_CLSTR_COPIED	(1ULL << 63)

// From OpenBSD
#define	QCOW2_COMPRESSED	0x4000000000000000ull
#define QCOW2_INPLACE		0x8000000000000000ull

#define	QCOW_MAGIC		0x514649fb
#define	QCOW_VERSION_2		2
#define	QCOW_VERSION_3		3
#define	QCOW_DIRTY		(1 << 0)
#define	QCOW_CORRUPT		(1 << 1)

static int vdsk_debug = 1;
static off_t xlate(struct vdsk *vdsk, off_t off, int *inplace);
static int qc2_openpath(struct vdsk *vdsk, char *path, int flags);
static off_t next_offset(struct vdsk *vdsk, off_t phyof, off_t l2off);

static int
qcow_probe(struct vdsk *vdsk)
{

	DPRINTF(("===> qcow_probe\n"));

	struct qcheader hdr;
	int qcowversion = 0;

	if (pread(vdsk->fd, &hdr, sizeof hdr, 0) != sizeof hdr) {
		printf("can't read header\n");
		errno = EBADF;
		goto out;
	}

	/* Get the magic identifier from qcow2 disk */
	if (strncmp(hdr.magic, "QFI\xfb", 4) != 0) {
		printf("It is not a qcow2 compatible disk.\n");
		errno = EFTYPE;
		goto out;
	}

	/* We support only qcow2 version 2 and 3 */
	qcowversion = be32toh(hdr.version);
	if (qcowversion != 2 && qcowversion != 3) {
		printf("qcow2 version: %d not supported.\n", qcowversion);
		errno = ENXIO;
		goto out;
	}

	errno = 0;

out:
	return (errno);
}

static int
qcow_open(struct vdsk *vdsk)
{

	DPRINTF(("===> qcow_open\n"));

	struct qcheader header;
	struct stat st;
	size_t i;
	char basepath[MAXPATHLEN];
	uint64_t backingoff;
	uint32_t backingsz;

	if (pread(vdsk->fd, &header, sizeof(header), 0) != sizeof(header)) {
		printf("==> cannot read header\n");
		exit(EBADF);
	}

	vdsk->base = NULL;
	vdsk->clustersz = (1ull << be32toh(header.clustershift));
	vdsk->disksz = be64toh(header.disksz);
	vdsk->cryptmethod = be32toh(header.cryptmethod);
	vdsk->l1sz = be32toh(header.l1sz);
	vdsk->l1off = be64toh(header.l1off);
	vdsk->refsz = be32toh(header.refsz);
	vdsk->refoff = be64toh(header.refoff);
	vdsk->nsnap = be32toh(header.snapcount);
	vdsk->snapoff = be64toh(header.snapsz);

	/* V2 is all 0 */
	vdsk->incompatfeatures = be64toh(header.incompatfeatures);
	vdsk->autoclearfeatures = be64toh(header.autoclearfeatures);
	vdsk->refssz = be32toh(header.refsz);
	vdsk->headersz = be32toh(header.headersz);

	/* XXX: Need to check more about these bits */
	if (vdsk->incompatfeatures & ~(QCOW_DIRTY|QCOW_CORRUPT)) {
		printf("==> unsupported features\n");
		exit(-1);
	}

	vdsk->l1 = calloc(vdsk->l1sz, sizeof (*vdsk->l1));
	if (!vdsk->l1) {
		printf("Cannot calloc L1\n");
		free(vdsk->l1);
		exit(-1);
	}
	if (pread(vdsk->fd, (char *)vdsk->l1, 8 * vdsk->l1sz, vdsk->l1off) != 8 * vdsk->l1sz) {
		printf("===> Unable to read qcow2 L1 table\n");
		free(vdsk->l1);
		exit(-1);
	}
	for (i = 0; i < vdsk->l1sz; i++) {
		vdsk->l1[i] = be64toh(vdsk->l1[i]);
	}

	backingoff = be64toh(header.backingoff);
	backingsz = be32toh(header.backingsz);
	if (backingsz != 0) {
		if (backingsz >= sizeof(basepath) - 1) {
			printf("==> Snapshot path is too long\n");
			exit(-1);
		}
		if (pread(vdsk->fd, basepath, backingsz, backingoff) != backingsz) {
			printf("==> could not read snapshot base name\n");
			exit(-1);
		}
		basepath[backingsz] = 0;

		/* XXX: Need to check base image NFD
		 * OpenBSD line 256
		 */

		vdsk->base = calloc(1, sizeof(struct vdsk));
		if (!vdsk->base) {
			printf("There is no vdsk->base\n");
			free(vdsk->base);
			exit(-1);
		}
		if (qc2_openpath(vdsk->base, basepath, O_RDONLY) == -1) {
			printf("==> cannot open vdsk->base disk\n");
			free(vdsk->base);
			exit(-1);
		}	
		if (vdsk->base->clustersz != vdsk->clustersz) {
			printf("===> all disks must share clustersize\n");
			free(vdsk->base);
			exit(-1);
		}
	}

	if (fstat(vdsk->fd, &st) == -1) {
		printf("Unable to stat disk\n");
		exit(-1);
	}
	vdsk->end = st.st_size;

	printf("qcow2 disk version %d size %lu end %lu\n",
			vdsk->header.version, vdsk->disksz, vdsk->end);
	printf("+++> filename: %s\n", vdsk->filename);

	return (0);
}

static int
qc2_openpath(struct vdsk *vdsk, char *path, int flags)
{
	int fd;

	fd = open(path, flags);
	if (fd < 0) {
		printf("==> qc2_openpath could not open\n");
		return (-1);
	}

	return qcow_open(vdsk->base);
}

static int
qcow_close(struct vdsk *vdsk __unused)
{

	return (ENOSYS);
}

int count = 0;
static int
qcow_read(struct vdsk *vdsk, struct blockif_req *br, uint8_t *buf)
{
	off_t end = 0;
	int err = errno;

	/* raw_read */
	/*
	ssize_t clen, len, off, boff, voff;
	int i;
	*/
	/* raw_read */
	struct vdsk *disk, *d;
	off_t phys_off, cluster_off;
	ssize_t sz;
	ssize_t len;
	off_t off = 0;

	int count = 0;

	end = vdsk->disksz;
	disk = vdsk;
	int brk = 0;
	while (off != vdsk->disksz) {
		for (d = disk; d; d = d->base)
			if ((phys_off = xlate(vdsk, off, NULL)) > 0) {
				printf("xlate breaks\n");
				brk = 1;
				/*
				printf("br->br_iov->iov_len: %lu\n", br->br_iov->iov_len);
				printf("br->br_iovcnt: %d\n", br->br_iovcnt);
				printf("br->br_resid: %zd\n", br->br_resid);
				printf("----\n");
				printf("capacity: %lu\n", vdsk->capacity);
				printf("sectorsize: %d\n", vdsk->sectorsize);
				printf("clustersz: %u\n", vdsk->clustersz);
				printf("disksz: %lu\n", vdsk->disksz);
				printf("cryptmethod: %u\n", vdsk->cryptmethod);
				printf("l1sz: %u\n", vdsk->l1sz);
				printf("l1off: %lu\n", vdsk->l1off);
				printf("l2sz: %u\n", vdsk->l2sz);
				printf("l2off: %lu\n", vdsk->l2off);
				printf("refoff: %lu\n", vdsk->refoff);
				printf("refsz: %u\n", vdsk->refsz);
				printf("nsnap: %u\n", vdsk->nsnap);
				printf("snapoff: %lu\n", vdsk->snapoff);
				printf("=================================\n");
				*/
				break;
			} else 
				brk = 0;
		cluster_off = off % vdsk->clustersz;
		sz = vdsk->clustersz - cluster_off;
		br->br_iov->iov_len = vdsk->clustersz;
		printf("cluster_off: %lu sz: %lu\n", cluster_off, sz);
		if (brk > 0) {
			//off = phys_off + vdsk->l1off;
			printf("===> phys_off: %lu, off: %lu\n", phys_off, off);
			if (!d)
				bzero(buf, sz);
			else {
				//len = pread(d->fd, buf, sz, phys_off);
				len = preadv(d->fd, br->br_iov, br->br_iovcnt, phys_off);
			//pread(vdsk->fd, &buf, sizeof(buf), l2tab + l2off * 8);
				printf("len: %lu \n", len);
			       if (len != sz) {
					printf("++++ CANT READ +++\n");
					return (-1);
				}
			}
		}
		off += sz;
		//buf += sz;
		br->br_resid -= len;
		br->br_offset = phys_off;
		printf("off: %lu, l1off: %lu l2off: %lu\n", off, vdsk->l1off, vdsk->l2off);

		count++;
		if (count > 30)
			exit(-1);
		//return(err);
	}
	return (err);
}

static off_t next_offset(struct vdsk *vdsk, off_t phyof, off_t l2off)
{
	off_t result;
	result = xlate(vdsk, phyof + vdsk->disksz, NULL);
	result = phyof + vdsk->l1off;

	//uint64_t buf;

	//pread(vdsk->fd, &buf, sizeof(buf), phyof);

	return (result);
}

static int
qcow_write(struct vdsk *vdsk __unused, struct blockif_req *br __unused, uint8_t *buf __unused)
{

	return (ENOSYS);
}

static int
qcow_trim(struct vdsk *vdsk __unused, unsigned long diocg __unused, off_t arg[2] __unused)
{

	return (ENOSYS);
}

static int
qcow_flush(struct vdsk *vdsk __unused, unsigned long diocg __unused)
{

	return (ENOSYS);
}

static off_t
xlate(struct vdsk *vdsk, off_t off, int *inplace)
{
	off_t l2sz, l1off, l2tab, l2off, cluster, clusteroff;
	uint64_t buf;

	printf("\n\n====== xlate ======\n\n");

	if (inplace)
		*inplace = 0;
	if (off < 0)
		goto err;

	l2sz = vdsk->clustersz / 8;
	l1off = (off / vdsk->clustersz) / l2sz;
	if (l1off >= vdsk->l1sz)
		goto err;

	l2tab = vdsk->l1[l1off];
	l2tab &= ~QCOW2_INPLACE;

	vdsk->l2off = l2tab;

	if (l2tab == 0) {
		printf("===> xlate l2tab return 0\n");
		return 0;
	}

	l2off = (off / vdsk->clustersz) % l2sz;
	pread(vdsk->fd, &buf, sizeof(buf), l2tab + l2off * 8);
	cluster = be64toh(buf);

	if (inplace)
		*inplace = !!(cluster & QCOW2_INPLACE);
	if (cluster & QCOW2_COMPRESSED) {
		printf("Compressed cluster unsupported\n");
		goto err;
	}
	clusteroff = 0;
	cluster &= ~QCOW2_INPLACE;
	if (cluster)
		clusteroff = off % vdsk->clustersz;


	printf("l2sz: %lu, l2tab: %lu, l2off: %lu, l1off: %lu, cluster: %lu, clusteroff: %lu, cluster+clusteroff: %lu, refoff: %lu, refsz: %u\n",
		       l2sz, l2tab, l2off, l1off, cluster, clusteroff, cluster + clusteroff,
		       vdsk->refoff, vdsk->refsz);	
	return cluster + clusteroff;

err:
	printf("===> xlate err\n");
	return (-1);

}

static struct vdsk_format qcow_format = {
	.name = "qcow",
	.description = "QEMU Copy-On-Write",
	.flags = VDSKFMT_CAN_WRITE | VDSKFMT_HAS_HEADER,
	.probe = qcow_probe,
	.open = qcow_open,
	.close = qcow_close,
	.read = qcow_read,
	.write = qcow_write,
	.trim = qcow_trim,
	.flush = qcow_flush,
};
FORMAT_DEFINE(qcow_format);

