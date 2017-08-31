/*
 * virtio entropy device emulation.
 * Randomness is sourced from /dev/random which does not block
 * once it has been seeded at bootup.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/linker_set.h>
#include <sys/uio.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bhyverun.h"
#include "mmio_emul.h"
#include "virtio.h"
#include "virtio_ids.h"
#include "virtio_mmio.h"

#define VTRND_RINGSZ	    64

static int mmio_vtrnd_debug;
#define DPRINTF(format, ...) \
	if (mmio_vtrnd_debug) printf("vtrnd: " format, ##__VA_ARGS__)
#define WPRINTF(format, ...) \
	printf("vtrnd: " format, ##__VA_ARGS__)

/* Per-device softc */
struct mmio_vtrnd_softc {
	struct virtio_softc	vrsc_vs;
	struct vqueue_info	vrsc_vq;
	pthread_mutex_t		vrsc_mtx;
	uint64_t		vrsc_cfg;
	int			vrsc_fd;
};

static void mmio_vtrnd_reset(void *);
static void mmio_vtrnd_notify(void *, struct vqueue_info *);

static struct virtio_consts vtrnd_vi_consts = {
	"vtrnd",		/* device name */
	1,			/* support for 1 virtqueue */
	0,			/* config space size */
	mmio_vtrnd_reset,	/* reset handler */
	mmio_vtrnd_notify,	/* device-wide qnotify handler */
	NULL,			/* no read virtio handler */
	NULL,			/* no write virtio handler */
	NULL,			/* apply negotiated features */
	0,			/* our capabilities */
};

static void
mmio_vtrnd_reset(void *vsc)
{
	struct mmio_vtrnd_softc *sc;

	sc = vsc;

	DPRINTF("device reset requested!\n");
	vi_reset_dev(&sc->vrsc_vs);
}

static void
mmio_vtrnd_notify(void *vsc, struct vqueue_info *vq)
{
	struct iovec iov;
	struct mmio_vtrnd_softc *sc;
	int len;
	uint16_t idx;

	sc = vsc;

	if (sc->vrsc_fd < 0) {
		vq_endchains(vq, 0);
		return;
	}

	while (vq_has_descs(vq)) {
		vq_getchain(vq, &idx, &iov, 1, NULL);

		len = read(sc->vrsc_fd, iov.iov_base, iov.iov_len);

		DPRINTF("vtrnd_notify(): %d\r\n", len);

		/* Catastrophe if unable to read from /dev/random */
		assert(len > 0);

		/* Release chain and handle more */
		vq_relchain(vq, idx, len);
	}

	/* Generate an interrupt if appropriate */
	vq_endchains(vq, 1);
}


static int
mmio_vtrnd_init(struct vmctx *ctx, struct mmio_devinst *mi, char *opts)
{
	struct mmio_vtrnd_softc *sc;
	int fd;
	int len;
	uint8_t v;

	/* Should always be able to open /dev/random */
	fd = open("/dev/random", O_RDONLY | O_NONBLOCK);

	assert(fd >= 0);

	/* Check that device is seeded and non-blocking */
	len = read(fd, &v, sizeof(v));
	if (len <= 0) {
		WPRINTF("/dev/random no read, read(): %d\n", len);
		return (1);
	}

	sc = calloc(1, sizeof(struct mmio_vtrnd_softc));

	vi_softc_linkup(&sc->vrsc_vs, &vtrnd_vi_consts, sc, mi, &sc->vrsc_vq);
	sc->vrsc_vs.vs_mtx = &sc->vrsc_mtx;
	sc->vrsc_vq.vq_qsize = VTRND_RINGSZ;

	/* Keep /dev/random open while emulating */
	sc->vrsc_fd = fd;

	/* Initialize config space */
	mmio_set_cfgreg(mi, VIRTIO_MMIO_MAGIC_VALUE, VIRTIO_MMIO_MAGIC_NUM);
	mmio_set_cfgreg(mi, VIRTIO_MMIO_VERSION, VIRTIO_MMIO_VERSION_NUM);
	mmio_set_cfgreg(mi, VIRTIO_MMIO_DEVICE_ID, VIRTIO_ID_ENTROPY);
	mmio_set_cfgreg(mi, VIRTIO_MMIO_VENDOR_ID, VIRTIO_VENDOR);

	if (vi_intr_init(&sc->vrsc_vs))
		return (1);

	vi_set_mmio_mem(&sc->vrsc_vs);

	return (0);
}

struct mmio_devemu mmio_de_vrnd = {
	.me_emu		= "virtio-rnd",
	.me_init	= mmio_vtrnd_init,
	.me_write	= vi_mmio_write,
	.me_read	= vi_mmio_read
};
MMIO_EMUL_SET(mmio_de_vrnd);
