/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 iXsystems Inc.
 * All rights reserved.
 *
 * This software was developed by Jakub Klama <jceel@FreeBSD.org>
 * under sponsorship from iXsystems Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
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
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifndef WITHOUT_CAPSICUM
#include <sys/capsicum.h>
#endif
#include <sys/linker_set.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifndef WITHOUT_CAPSICUM
#include <capsicum_helpers.h>
#endif
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <libgen.h>
#include <sysexits.h>

#include "bhyverun.h"
#include "debug.h"
#include "devemu.h"
#include "devemu_virtio.h"
#include "virtio_console.h"
#include "mevent.h"
#include "sockstream.h"

#define	VTCON_RINGSZ	64
#define	VTCON_MAXPORTS	16
#define	VTCON_MAXQ	(VTCON_MAXPORTS * 2 + 2)

#define	VTCON_DEVICE_READY	0
#define	VTCON_DEVICE_ADD	1
#define	VTCON_DEVICE_REMOVE	2
#define	VTCON_PORT_READY	3
#define	VTCON_CONSOLE_PORT	4
#define	VTCON_CONSOLE_RESIZE	5
#define	VTCON_PORT_OPEN		6
#define	VTCON_PORT_NAME		7

#define	VTCON_S_HOSTCAPS		\
	(VIRTIO_CONSOLE_F_SIZE		| \
	 VIRTIO_CONSOLE_F_MULTIPORT	| \
	 VIRTIO_CONSOLE_F_EMERG_WRITE)

static int pci_vtcon_debug;
#define DPRINTF(params) if (pci_vtcon_debug) PRINTLN params
#define WPRINTF(params) PRINTLN params

struct devemu_vtcon_softc;
struct devemu_vtcon_port;
struct devemu_vtcon_config;
typedef void (devemu_vtcon_cb_t)(struct devemu_vtcon_port *, void *,
    struct iovec *, int);

struct devemu_vtcon_port {
	struct devemu_vtcon_softc * vsp_sc;
	int                         vsp_id;
	const char *                vsp_name;
	bool                        vsp_enabled;
	bool                        vsp_console;
	bool                        vsp_rx_ready;
	bool                        vsp_open;
	int                         vsp_rxq;
	int                         vsp_txq;
	void *                      vsp_arg;
	devemu_vtcon_cb_t *         vsp_cb;
};

struct devemu_vtcon_sock
{
	struct devemu_vtcon_port *  vss_port;
	const char *                vss_path;
	struct mevent *             vss_server_evp;
	struct mevent *             vss_conn_evp;
	int                         vss_server_fd;
	int                         vss_conn_fd;
	bool                        vss_open;
};

struct devemu_vtcon_softc {
	struct virtio_softc         vsc_vs;
	struct vqueue_info          vsc_queues[VTCON_MAXQ];
	pthread_mutex_t             vsc_mtx;
	uint64_t                    vsc_cfg;
	uint64_t                    vsc_features;
	char *                      vsc_rootdir;
	int                         vsc_kq;
	int                         vsc_nports;
	bool                        vsc_ready;
	struct devemu_vtcon_port    vsc_control_port;
 	struct devemu_vtcon_port    vsc_ports[VTCON_MAXPORTS];
	struct devemu_vtcon_config *vsc_config;
};

struct devemu_vtcon_config {
	uint16_t cols;
	uint16_t rows;
	uint32_t max_nr_ports;
	uint32_t emerg_wr;
} __attribute__((packed));

struct devemu_vtcon_control {
	uint32_t id;
	uint16_t event;
	uint16_t value;
} __attribute__((packed));

struct devemu_vtcon_console_resize {
	uint16_t cols;
	uint16_t rows;
} __attribute__((packed));

static void devemu_vtcon_reset(void *);
static void devemu_vtcon_notify_rx(void *, struct vqueue_info *);
static void devemu_vtcon_notify_tx(void *, struct vqueue_info *);
static int devemu_vtcon_cfgread(void *, int, int, uint32_t *);
static int devemu_vtcon_cfgwrite(void *, int, int, uint32_t);
static void devemu_vtcon_neg_features(void *, uint64_t);
static void devemu_vtcon_sock_accept(int, enum ev_type,  void *);
static void devemu_vtcon_sock_rx(int, enum ev_type, void *);
static void devemu_vtcon_sock_tx(struct devemu_vtcon_port *, void *,
    struct iovec *, int);
static void devemu_vtcon_control_send(struct devemu_vtcon_softc *,
    struct devemu_vtcon_control *, const void *, size_t);
static void devemu_vtcon_announce_port(struct devemu_vtcon_port *);
static void devemu_vtcon_open_port(struct devemu_vtcon_port *, bool);

static struct virtio_consts vtcon_vi_consts = {
	"vtcon",			/* our name */
	VTCON_MAXQ,			/* we support VTCON_MAXQ virtqueues */
	sizeof(struct devemu_vtcon_config), /* config reg size */
	devemu_vtcon_reset,		/* reset */
	NULL,				/* device-wide qnotify */
	devemu_vtcon_cfgread,		/* read virtio config */
	devemu_vtcon_cfgwrite,		/* write virtio config */
	devemu_vtcon_neg_features,	/* apply negotiated features */
	VTCON_S_HOSTCAPS,		/* our capabilities */
};


static void
devemu_vtcon_reset(void *vsc)
{
	struct devemu_vtcon_softc *sc;

	sc = vsc;

	DPRINTF(("vtcon: device reset requested!"));
	vi_reset_dev(&sc->vsc_vs);
}

static void
devemu_vtcon_neg_features(void *vsc, uint64_t negotiated_features)
{
	struct devemu_vtcon_softc *sc = vsc;

	sc->vsc_features = negotiated_features;
}

static int
devemu_vtcon_cfgread(void *vsc, int offset, int size, uint32_t *retval)
{
	struct devemu_vtcon_softc *sc = vsc;
	void *ptr;

	ptr = (uint8_t *)sc->vsc_config + offset;
	memcpy(retval, ptr, size);
	return (0);
}

static int
devemu_vtcon_cfgwrite(void *vsc, int offset, int size, uint32_t val)
{

	return (0);
}

static inline struct devemu_vtcon_port *
devemu_vtcon_vq_to_port(struct devemu_vtcon_softc *sc, struct vqueue_info *vq)
{
	uint16_t num = vq->vq_num;

	if (num == 0 || num == 1)
		return (&sc->vsc_ports[0]);

	if (num == 2 || num == 3)
		return (&sc->vsc_control_port);

	return (&sc->vsc_ports[(num / 2) - 1]);
}

static inline struct vqueue_info *
devemu_vtcon_port_to_vq(struct devemu_vtcon_port *port, bool tx_queue)
{
	int qnum;

	qnum = tx_queue ? port->vsp_txq : port->vsp_rxq;
	return (&port->vsp_sc->vsc_queues[qnum]);
}

static struct devemu_vtcon_port *
devemu_vtcon_port_add(struct devemu_vtcon_softc *sc, const char *name,
    devemu_vtcon_cb_t *cb, void *arg)
{
	struct devemu_vtcon_port *port;

	if (sc->vsc_nports == VTCON_MAXPORTS) {
		errno = EBUSY;
		return (NULL);
	}

	port = &sc->vsc_ports[sc->vsc_nports++];
	port->vsp_id = sc->vsc_nports - 1;
	port->vsp_sc = sc;
	port->vsp_name = name;
	port->vsp_cb = cb;
	port->vsp_arg = arg;

	if (port->vsp_id == 0) {
		/* port0 */
		port->vsp_txq = 0;
		port->vsp_rxq = 1;
	} else {
		port->vsp_txq = sc->vsc_nports * 2;
		port->vsp_rxq = port->vsp_txq + 1;
	}

	port->vsp_enabled = true;
	return (port);
}

static int
devemu_vtcon_sock_add(struct devemu_vtcon_softc *sc, const char *name,
    const char *path)
{
	struct devemu_vtcon_sock *sock;
	struct sockaddr_un sun;
	char *pathcopy;
	int s = -1, fd = -1, error = 0;
#ifndef WITHOUT_CAPSICUM
	cap_rights_t rights;
#endif

	sock = calloc(1, sizeof(struct devemu_vtcon_sock));
	if (sock == NULL) {
		error = -1;
		goto out;
	}

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		error = -1;
		goto out;
	}

	pathcopy = strdup(path);
	if (pathcopy == NULL) {
		error = -1;
		goto out;
	}

	fd = open(dirname(pathcopy), O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		free(pathcopy);
		error = -1;
		goto out;
	}

	sun.sun_family = AF_UNIX;
	sun.sun_len = sizeof(struct sockaddr_un);
	strcpy(pathcopy, path);
	strlcpy(sun.sun_path, basename(pathcopy), sizeof(sun.sun_path));
	free(pathcopy);

	if (bindat(fd, s, (struct sockaddr *)&sun, sun.sun_len) < 0) {
		error = -1;
		goto out;
	}

	if (fcntl(s, F_SETFL, O_NONBLOCK) < 0) {
		error = -1;
		goto out;
	}

	if (listen(s, 1) < 0) {
		error = -1;
		goto out;
	}

#ifndef WITHOUT_CAPSICUM
	cap_rights_init(&rights, CAP_ACCEPT, CAP_EVENT, CAP_READ, CAP_WRITE);
	if (caph_rights_limit(s, &rights) == -1)
		errx(EX_OSERR, "Unable to apply rights for sandbox");
#endif

	sock->vss_port = devemu_vtcon_port_add(sc, name, devemu_vtcon_sock_tx, sock);
	if (sock->vss_port == NULL) {
		error = -1;
		goto out;
	}

	sock->vss_open = false;
	sock->vss_conn_fd = -1;
	sock->vss_server_fd = s;
	sock->vss_server_evp = mevent_add(s, EVF_READ, devemu_vtcon_sock_accept,
	    sock);

	if (sock->vss_server_evp == NULL) {
		error = -1;
		goto out;
	}

out:
	if (fd != -1)
		close(fd);

	if (error != 0) {
		if (s != -1)
			close(s);
		free(sock);
	}

	return (error);
}

static void
devemu_vtcon_sock_accept(int fd __unused, enum ev_type t __unused, void *arg)
{
	struct devemu_vtcon_sock *sock = (struct devemu_vtcon_sock *)arg;
	int s;

	s = accept(sock->vss_server_fd, NULL, NULL);
	if (s < 0)
		return;

	if (sock->vss_open) {
		close(s);
		return;
	}

	sock->vss_open = true;
	sock->vss_conn_fd = s;
	sock->vss_conn_evp = mevent_add(s, EVF_READ, devemu_vtcon_sock_rx, sock);

	devemu_vtcon_open_port(sock->vss_port, true);
}

static void
devemu_vtcon_sock_rx(int fd __unused, enum ev_type t __unused, void *arg)
{
	struct devemu_vtcon_port *port;
	struct devemu_vtcon_sock *sock = (struct devemu_vtcon_sock *)arg;
	struct vqueue_info *vq;
	struct iovec iov;
	static char dummybuf[2048];
	int len, n;
	uint16_t idx;

	port = sock->vss_port;
	vq = devemu_vtcon_port_to_vq(port, true);

	if (!sock->vss_open || !port->vsp_rx_ready) {
		len = read(sock->vss_conn_fd, dummybuf, sizeof(dummybuf));
		if (len == 0)
			goto close;

		return;
	}

	if (!vq_has_descs(vq)) {
		len = read(sock->vss_conn_fd, dummybuf, sizeof(dummybuf));
		vq_endchains(vq, 1);
		if (len == 0)
			goto close;

		return;
	}

	do {
		n = vq_getchain(vq, &idx, &iov, 1, NULL);
		len = readv(sock->vss_conn_fd, &iov, n);

		if (len == 0 || (len < 0 && errno == EWOULDBLOCK)) {
			vq_retchains(vq, 1);
			vq_endchains(vq, 0);
			if (len == 0)
				goto close;

			return;
		}

		vq_relchain(vq, idx, len);
	} while (vq_has_descs(vq));

	vq_endchains(vq, 1);

close:
	mevent_delete_close(sock->vss_conn_evp);
	sock->vss_conn_fd = -1;
	sock->vss_open = false;
}

static void
devemu_vtcon_sock_tx(struct devemu_vtcon_port *port, void *arg, struct iovec *iov,
    int niov)
{
	struct devemu_vtcon_sock *sock;
	int i, ret;

	sock = (struct devemu_vtcon_sock *)arg;

	if (sock->vss_conn_fd == -1)
		return;

	for (i = 0; i < niov; i++) {
		ret = stream_write(sock->vss_conn_fd, iov[i].iov_base,
		    iov[i].iov_len);
		if (ret <= 0)
			break;
	}

	if (ret <= 0) {
		mevent_delete_close(sock->vss_conn_evp);
		sock->vss_conn_fd = -1;
		sock->vss_open = false;
	}
}

static void
devemu_vtcon_control_tx(struct devemu_vtcon_port *port, void *arg, struct iovec *iov,
    int niov)
{
	struct devemu_vtcon_softc *sc;
	struct devemu_vtcon_port *tmp;
	struct devemu_vtcon_control resp, *ctrl;
	int i;

	assert(niov == 1);

	sc = port->vsp_sc;
	ctrl = (struct devemu_vtcon_control *)iov->iov_base;

	switch (ctrl->event) {
	case VTCON_DEVICE_READY:
		sc->vsc_ready = true;
		/* set port ready events for registered ports */
		for (i = 0; i < VTCON_MAXPORTS; i++) {
			tmp = &sc->vsc_ports[i];
			if (tmp->vsp_enabled)
				devemu_vtcon_announce_port(tmp);

			if (tmp->vsp_open)
				devemu_vtcon_open_port(tmp, true);
		}
		break;

	case VTCON_PORT_READY:
		if (ctrl->id >= sc->vsc_nports) {
			WPRINTF(("VTCON_PORT_READY event for unknown port %d",
			    ctrl->id));
			return;
		}

		tmp = &sc->vsc_ports[ctrl->id];
		if (tmp->vsp_console) {
			resp.event = VTCON_CONSOLE_PORT;
			resp.id = ctrl->id;
			resp.value = 1;
			devemu_vtcon_control_send(sc, &resp, NULL, 0);
		}
		break;
	}
}

static void
devemu_vtcon_announce_port(struct devemu_vtcon_port *port)
{
	struct devemu_vtcon_control event;

	event.id = port->vsp_id;
	event.event = VTCON_DEVICE_ADD;
	event.value = 1;
	devemu_vtcon_control_send(port->vsp_sc, &event, NULL, 0);

	event.event = VTCON_PORT_NAME;
	devemu_vtcon_control_send(port->vsp_sc, &event, port->vsp_name,
	    strlen(port->vsp_name));
}

static void
devemu_vtcon_open_port(struct devemu_vtcon_port *port, bool open)
{
	struct devemu_vtcon_control event;

	if (!port->vsp_sc->vsc_ready) {
		port->vsp_open = true;
		return;
	}

	event.id = port->vsp_id;
	event.event = VTCON_PORT_OPEN;
	event.value = (int)open;
	devemu_vtcon_control_send(port->vsp_sc, &event, NULL, 0);
}

static void
devemu_vtcon_control_send(struct devemu_vtcon_softc *sc,
    struct devemu_vtcon_control *ctrl, const void *payload, size_t len)
{
	struct vqueue_info *vq;
	struct iovec iov;
	uint16_t idx;
	int n;

	vq = devemu_vtcon_port_to_vq(&sc->vsc_control_port, true);

	if (!vq_has_descs(vq))
		return;

	n = vq_getchain(vq, &idx, &iov, 1, NULL);

	assert(n == 1);

	memcpy(iov.iov_base, ctrl, sizeof(struct devemu_vtcon_control));
	if (payload != NULL && len > 0)
		memcpy(iov.iov_base + sizeof(struct devemu_vtcon_control),
		     payload, len);

	vq_relchain(vq, idx, sizeof(struct devemu_vtcon_control) + len);
	vq_endchains(vq, 1);
}
    

static void
devemu_vtcon_notify_tx(void *vsc, struct vqueue_info *vq)
{
	struct devemu_vtcon_softc *sc;
	struct devemu_vtcon_port *port;
	struct iovec iov[1];
	uint16_t idx, n;
	uint16_t flags[8];

	sc = vsc;
	port = devemu_vtcon_vq_to_port(sc, vq);

	while (vq_has_descs(vq)) {
		n = vq_getchain(vq, &idx, iov, 1, flags);
		assert(n >= 1);
		if (port != NULL)
			port->vsp_cb(port, port->vsp_arg, iov, 1);

		/*
		 * Release this chain and handle more
		 */
		vq_relchain(vq, idx, 0);
	}
	vq_endchains(vq, 1);	/* Generate interrupt if appropriate. */
}

static void
devemu_vtcon_notify_rx(void *vsc, struct vqueue_info *vq)
{
	struct devemu_vtcon_softc *sc;
	struct devemu_vtcon_port *port;

	sc = vsc;
	port = devemu_vtcon_vq_to_port(sc, vq);

	if (!port->vsp_rx_ready) {
		port->vsp_rx_ready = 1;
		vq_kick_disable(vq);
	}
}

static int
devemu_vtcon_init(struct vmctx *ctx, struct devemu_inst *di, char *opts)
{
	struct devemu_vtcon_softc *sc;
	char *portname = NULL;
	char *portpath = NULL;
	char *opt;
	int i;	

	sc = calloc(1, sizeof(struct devemu_vtcon_softc));
	sc->vsc_config = calloc(1, sizeof(struct devemu_vtcon_config));
	sc->vsc_config->max_nr_ports = VTCON_MAXPORTS;
	sc->vsc_config->cols = 80;
	sc->vsc_config->rows = 25; 

	vi_softc_linkup(&sc->vsc_vs, &vtcon_vi_consts, sc, di, sc->vsc_queues);
	sc->vsc_vs.vs_mtx = &sc->vsc_mtx;

	for (i = 0; i < VTCON_MAXQ; i++) {
		sc->vsc_queues[i].vq_qsize = VTCON_RINGSZ;
		sc->vsc_queues[i].vq_notify = i % 2 == 0
		    ? devemu_vtcon_notify_rx
		    : devemu_vtcon_notify_tx;
	}

	/* initialize config space */
	vi_devemu_init(di, VIRTIO_TYPE_CONSOLE);

	if (vi_intr_init(&sc->vsc_vs, 1, fbsdrun_virtio_msix()))
		return (1);
	vi_set_io_res(&sc->vsc_vs, 0);

	/* create control port */
	sc->vsc_control_port.vsp_sc = sc;
	sc->vsc_control_port.vsp_txq = 2;
	sc->vsc_control_port.vsp_rxq = 3;
	sc->vsc_control_port.vsp_cb = devemu_vtcon_control_tx;
	sc->vsc_control_port.vsp_enabled = true;

	while ((opt = strsep(&opts, ",")) != NULL) {
		portname = strsep(&opt, "=");
		portpath = opt;

		/* create port */
		if (devemu_vtcon_sock_add(sc, portname, portpath) < 0) {
			fprintf(stderr, "cannot create port %s: %s\n",
			    portname, strerror(errno));
			return (1);
		}
	}

	return (0);
}

struct devemu_dev devemu_de_vcon = {
	.de_emu =	"virtio-console",
	.de_init =	devemu_vtcon_init,
	.de_write =	vi_devemu_write,
	.de_read =	vi_devemu_read
};
DEVEMU_SET(devemu_de_vcon);
