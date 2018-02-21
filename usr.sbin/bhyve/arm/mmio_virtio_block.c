/*-
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

#include <sys/param.h>
#include <sys/linker_set.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/disk.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <md5.h>

#include "bhyverun.h"
#include "block_if.h"
#include "mmio_emul.h"
#include "virtio.h"
#include "virtio_ids.h"
#include "virtio_mmio.h"

#define VTBLK_RINGSZ	64

#define VTBLK_S_OK	0
#define VTBLK_S_IOERR	1
#define	VTBLK_S_UNSUPP	2

#define	VTBLK_BLK_ID_BYTES	20

/* Capability bits */
#define	VTBLK_F_SEG_MAX		(1 << 2)	/* Maximum request segments */
#define	VTBLK_F_BLK_SIZE	(1 << 6)	/* cfg block size valid */
#define	VTBLK_F_FLUSH		(1 << 9)	/* Cache flush support */
#define	VTBLK_F_TOPOLOGY	(1 << 10)	/* Optimal I/O alignment */

/*
 * Host capabilities
 */
#define VTBLK_S_HOSTCAPS      \
  ( VTBLK_F_SEG_MAX  |						    \
    VTBLK_F_BLK_SIZE |						    \
    VTBLK_F_FLUSH    |						    \
    VTBLK_F_TOPOLOGY |						    \
    VIRTIO_RING_F_INDIRECT_DESC )	/* indirect descriptors */

/*
 * Config space "registers"
 */
struct vtblk_config {
	uint64_t	vbc_capacity;
	uint32_t	vbc_size_max;
	uint32_t	vbc_seg_max;
	struct {
		uint16_t cylinders;
		uint8_t heads;
		uint8_t sectors;
	} vbc_geometry;
	uint32_t	vbc_blk_size;
	struct {
		uint8_t physical_block_exp;
		uint8_t alignment_offset;
		uint16_t min_io_size;
		uint32_t opt_io_size;
	} vbc_topology;
	uint8_t		vbc_writeback;
} __packed;

/*
 * Fixed-size block header
 */
struct virtio_blk_hdr {
#define	VBH_OP_READ		0
#define	VBH_OP_WRITE		1
#define	VBH_OP_FLUSH		4
#define	VBH_OP_FLUSH_OUT	5
#define	VBH_OP_IDENT		8		
#define	VBH_FLAG_BARRIER	0x80000000	/* OR'ed into vbh_type */
	uint32_t       	vbh_type;
	uint32_t	vbh_ioprio;
	uint64_t	vbh_sector;
} __packed;

/*
 * Debug printf
 */
static int mmio_vtblk_debug;
#define DPRINTF(params) if (mmio_vtblk_debug) printf params
#define WPRINTF(params) printf params

struct mmio_vtblk_ioreq {
	struct blockif_req		io_req;
	struct mmio_vtblk_softc	       *io_sc;
	uint8_t			       *io_status;
	uint16_t			io_idx;
};

/*
 * Per-device softc
 */
struct mmio_vtblk_softc {
	struct virtio_softc vbsc_vs;
	pthread_mutex_t vsc_mtx;
	struct vqueue_info vbsc_vq;
	struct vtblk_config vbsc_cfg;
	struct blockif_ctxt *bc;
	char vbsc_ident[VTBLK_BLK_ID_BYTES];
	struct mmio_vtblk_ioreq vbsc_ios[VTBLK_RINGSZ];
};

static void mmio_vtblk_reset(void *);
static void mmio_vtblk_notify(void *, struct vqueue_info *);
static int mmio_vtblk_cfgread(void *, int, int, uint32_t *);
static int mmio_vtblk_cfgwrite(void *, int, int, uint32_t);

static struct virtio_consts vtblk_vi_consts = {
	"vtblk",		/* our name */
	1,			/* we support 1 virtqueue */
	sizeof(struct vtblk_config), /* config reg size */
	mmio_vtblk_reset,	/* reset */
	mmio_vtblk_notify,	/* device-wide qnotify */
	mmio_vtblk_cfgread,	/* read PCI config */
	mmio_vtblk_cfgwrite,	/* write PCI config */
	NULL,			/* apply negotiated features */
	VTBLK_S_HOSTCAPS,	/* our capabilities */
};

static void
mmio_vtblk_reset(void *vsc)
{
	struct mmio_vtblk_softc *sc = vsc;

	DPRINTF(("vtblk: device reset requested !\n"));
	vi_reset_dev(&sc->vbsc_vs);
}

static void
mmio_vtblk_done(struct blockif_req *br, int err)
{
	struct mmio_vtblk_ioreq *io = br->br_param;
	struct mmio_vtblk_softc *sc = io->io_sc;

	/* convert errno into a virtio block error return */
	if (err == EOPNOTSUPP || err == ENOSYS)
		*io->io_status = VTBLK_S_UNSUPP;
	else if (err != 0)
		*io->io_status = VTBLK_S_IOERR;
	else
		*io->io_status = VTBLK_S_OK;

	/*
	 * Return the descriptor back to the host.
	 * We wrote 1 byte (our status) to host.
	 */
	pthread_mutex_lock(&sc->vsc_mtx);
	vq_relchain(&sc->vbsc_vq, io->io_idx, 1);
	vq_endchains(&sc->vbsc_vq, 0);
	pthread_mutex_unlock(&sc->vsc_mtx);
}

static void
mmio_vtblk_proc(struct mmio_vtblk_softc *sc, struct vqueue_info *vq)
{
	struct virtio_blk_hdr *vbh;
	struct mmio_vtblk_ioreq *io;
	int i, n;
	int err;
	ssize_t iolen;
	int writeop, type;
	struct iovec iov[BLOCKIF_IOV_MAX + 2];
	uint16_t idx, flags[BLOCKIF_IOV_MAX + 2];

	n = vq_getchain(vq, &idx, iov, BLOCKIF_IOV_MAX + 2, flags);

	/*
	 * The first descriptor will be the read-only fixed header,
	 * and the last is for status (hence +2 above and below).
	 * The remaining iov's are the actual data I/O vectors.
	 *
	 * XXX - note - this fails on crash dump, which does a
	 * VIRTIO_BLK_T_FLUSH with a zero transfer length
	 */
	assert(n >= 2 && n <= BLOCKIF_IOV_MAX + 2);

	io = &sc->vbsc_ios[idx];
	assert((flags[0] & VRING_DESC_F_WRITE) == 0);
	assert(iov[0].iov_len == sizeof(struct virtio_blk_hdr));
	vbh = iov[0].iov_base;
	memcpy(&io->io_req.br_iov, &iov[1], sizeof(struct iovec) * (n - 2));
	io->io_req.br_iovcnt = n - 2;
	io->io_req.br_offset = vbh->vbh_sector * DEV_BSIZE;
	io->io_status = iov[--n].iov_base;
	assert(iov[n].iov_len == 1);
	assert(flags[n] & VRING_DESC_F_WRITE);

	/*
	 * XXX
	 * The guest should not be setting the BARRIER flag because
	 * we don't advertise the capability.
	 */
	type = vbh->vbh_type & ~VBH_FLAG_BARRIER;
	writeop = (type == VBH_OP_WRITE);

	iolen = 0;
	for (i = 1; i < n; i++) {
		/*
		 * - write op implies read-only descriptor,
		 * - read/ident op implies write-only descriptor,
		 * therefore test the inverse of the descriptor bit
		 * to the op.
		 */
		assert(((flags[i] & VRING_DESC_F_WRITE) == 0) == writeop);
		iolen += iov[i].iov_len;
	}
	io->io_req.br_resid = iolen;

	DPRINTF(("virtio-block: %s op, %zd bytes, %d segs, offset %lld\n\r", 
		 writeop ? "write" : "read/ident", iolen, i - 1,
		 io->io_req.br_offset));

	switch (type) {
	case VBH_OP_READ:
		err = blockif_read(sc->bc, &io->io_req);
		break;
	case VBH_OP_WRITE:
		err = blockif_write(sc->bc, &io->io_req);
		break;
	case VBH_OP_FLUSH:
	case VBH_OP_FLUSH_OUT:
		err = blockif_flush(sc->bc, &io->io_req);
		break;
	case VBH_OP_IDENT:
		/* Assume a single buffer */
		/* S/n equal to buffer is not zero-terminated. */
		memset(iov[1].iov_base, 0, iov[1].iov_len);
		strncpy(iov[1].iov_base, sc->vbsc_ident,
		    MIN(iov[1].iov_len, sizeof(sc->vbsc_ident)));
		mmio_vtblk_done(&io->io_req, 0);
		return;
	default:
		mmio_vtblk_done(&io->io_req, EOPNOTSUPP);
		return;
	}
	assert(err == 0);
}

static void
mmio_vtblk_notify(void *vsc, struct vqueue_info *vq)
{
	struct mmio_vtblk_softc *sc = vsc;

	while (vq_has_descs(vq))
		mmio_vtblk_proc(sc, vq);
}

static int
mmio_vtblk_init(struct vmctx *ctx, struct mmio_devinst *mi, char *opts)
{
	char bident[sizeof("XX:X:X")];
	struct blockif_ctxt *bctxt;
	MD5_CTX mdctx;
	u_char digest[16];
	struct mmio_vtblk_softc *sc;
	off_t size;
	int i, sectsz, sts, sto;

	if (opts == NULL) {
		printf("virtio-block: backing device required\n");
		return (1);
	}

	/*
	 * The supplied backing file has to exist
	 */
	/* TODO: find some better identifier */
	snprintf(bident, sizeof(bident), "%.*s", sizeof(bident) - 1,
		 mi->mi_name);
	bctxt = blockif_open(opts, bident);
	if (bctxt == NULL) {       	
		perror("Could not open backing file");
		return (1);
	}

	size = blockif_size(bctxt);
	sectsz = blockif_sectsz(bctxt);
	blockif_psectsz(bctxt, &sts, &sto);

	sc = calloc(1, sizeof(struct mmio_vtblk_softc));
	sc->bc = bctxt;
	for (i = 0; i < VTBLK_RINGSZ; i++) {
		struct mmio_vtblk_ioreq *io = &sc->vbsc_ios[i];
		io->io_req.br_callback = mmio_vtblk_done;
		io->io_req.br_param = io;
		io->io_sc = sc;
		io->io_idx = i;
	}

	pthread_mutex_init(&sc->vsc_mtx, NULL);

	/* init virtio softc and virtqueues */
	vi_softc_linkup(&sc->vbsc_vs, &vtblk_vi_consts, sc, mi, &sc->vbsc_vq);
	sc->vbsc_vs.vs_mtx = &sc->vsc_mtx;

	sc->vbsc_vq.vq_qsize = VTBLK_RINGSZ;
	/* sc->vbsc_vq.vq_notify = we have no per-queue notify */

	/*
	 * Create an identifier for the backing file. Use parts of the
	 * md5 sum of the filename
	 */
	MD5Init(&mdctx);
	MD5Update(&mdctx, opts, strlen(opts));
	MD5Final(digest, &mdctx);	
	sprintf(sc->vbsc_ident, "BHYVE-%02X%02X-%02X%02X-%02X%02X",
	    digest[0], digest[1], digest[2], digest[3], digest[4], digest[5]);

	/* setup virtio block config space */
	sc->vbsc_cfg.vbc_capacity = size / DEV_BSIZE; /* 512-byte units */
	sc->vbsc_cfg.vbc_size_max = 0;	/* not negotiated */
	sc->vbsc_cfg.vbc_seg_max = BLOCKIF_IOV_MAX;
	sc->vbsc_cfg.vbc_geometry.cylinders = 0;	/* no geometry */
	sc->vbsc_cfg.vbc_geometry.heads = 0;
	sc->vbsc_cfg.vbc_geometry.sectors = 0;
	sc->vbsc_cfg.vbc_blk_size = sectsz;
	sc->vbsc_cfg.vbc_topology.physical_block_exp =
	    (sts > sectsz) ? (ffsll(sts / sectsz) - 1) : 0;
	sc->vbsc_cfg.vbc_topology.alignment_offset =
	    (sto != 0) ? ((sts - sto) / sectsz) : 0;
	sc->vbsc_cfg.vbc_topology.min_io_size = 0;
	sc->vbsc_cfg.vbc_topology.opt_io_size = 0;
	sc->vbsc_cfg.vbc_writeback = 0;

	/*
	 * Should we move some of this into virtio.c?  Could
	 * have the device, class, and subdev_0 as fields in
	 * the virtio constants structure.
	 */
	mmio_set_cfgreg(mi, VIRTIO_MMIO_MAGIC_VALUE, VIRTIO_MMIO_MAGIC_NUM);
	mmio_set_cfgreg(mi, VIRTIO_MMIO_VERSION, VIRTIO_MMIO_VERSION_NUM);
	mmio_set_cfgreg(mi, VIRTIO_MMIO_DEVICE_ID, VIRTIO_ID_BLOCK);
	mmio_set_cfgreg(mi, VIRTIO_MMIO_VENDOR_ID, VIRTIO_VENDOR);

	if (vi_intr_init(&sc->vbsc_vs)) {
		blockif_close(sc->bc);
		free(sc);
		return (1);
	}
	vi_set_mmio_mem(&sc->vbsc_vs);
	return (0);
}

static int
mmio_vtblk_cfgwrite(void *vsc, int offset, int size, uint32_t value)
{

	DPRINTF(("vtblk: write to readonly reg %d\n\r", offset));
	return (1);
}

static int
mmio_vtblk_cfgread(void *vsc, int offset, int size, uint32_t *retval)
{
	struct mmio_vtblk_softc *sc = vsc;
	void *ptr;

	/* our caller has already verified offset and size */
	ptr = (uint8_t *)&sc->vbsc_cfg + offset;
	memcpy(retval, ptr, size);
	return (0);
}

struct mmio_devemu mmio_de_vblk = {
	.me_emu =	"virtio-blk",
	.me_irq =	24,
	.me_init =	mmio_vtblk_init,
	.me_write =	vi_mmio_write,
	.me_read =	vi_mmio_read
};
MMIO_EMUL_SET(mmio_de_vblk);
