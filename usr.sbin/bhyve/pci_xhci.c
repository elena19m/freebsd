/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Leon Dang <ldang@nahannisys.com>
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
/*
   XHCI options:
    -s <n>,xhci,{devices}

   devices:
     tablet             USB tablet mouse
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/queue.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_freebsd.h>
#include <xhcireg.h>

#include <ctype.h>

#include "bhyverun.h"
#include "debug.h"
#include "dm_pass_string.h"
#include "pci_emul.h"
#include "pci_xhci.h"
#include "usb_emul.h"
#include "usb_pmapper.h"

static int xhci_debug = 1;

#define	DPRINTF(params) if (xhci_debug) PRINTLN params
#define	WPRINTF(params) PRINTLN params


#define	XHCI_NAME		"xhci"
#define	XHCI_MAX_DEVS		8	/* 4 USB3 + 4 USB2 devs */

#define	XHCI_MAX_SLOTS		64	/* min allowed by Windows drivers */

/*
 * XHCI data structures can be up to 64k, but limit paddr_guest2host mapping
 * to 4k to avoid going over the guest physical memory barrier.
 */
#define	XHCI_PADDR_SZ		4096	/* paddr_guest2host max size */

#define	XHCI_ERST_MAX		0	/* max 2^entries event ring seg tbl */

#define	XHCI_CAPLEN		(4*8)	/* offset of op register space */
#define	XHCI_HCCPRAMS2		0x1C	/* offset of HCCPARAMS2 register */
#define	XHCI_PORTREGS_START	0x400
#define	XHCI_DOORBELL_MAX	256

#define	XHCI_STREAMS_MAX	1	/* 4-15 in XHCI spec */

/* caplength and hci-version registers */
#define	XHCI_SET_CAPLEN(x)		((x) & 0xFF)
#define	XHCI_SET_HCIVERSION(x)		(((x) & 0xFFFF) << 16)
#define	XHCI_GET_HCIVERSION(x)		(((x) >> 16) & 0xFFFF)

/* hcsparams1 register */
#define	XHCI_SET_HCSP1_MAXSLOTS(x)	((x) & 0xFF)
#define	XHCI_SET_HCSP1_MAXINTR(x)	(((x) & 0x7FF) << 8)
#define	XHCI_SET_HCSP1_MAXPORTS(x)	(((x) & 0xFF) << 24)

/* hcsparams2 register */
#define	XHCI_SET_HCSP2_IST(x)		((x) & 0x0F)
#define	XHCI_SET_HCSP2_ERSTMAX(x)	(((x) & 0x0F) << 4)
#define	XHCI_SET_HCSP2_MAXSCRATCH_HI(x)	(((x) & 0x1F) << 21)
#define	XHCI_SET_HCSP2_MAXSCRATCH_LO(x)	(((x) & 0x1F) << 27)

/* hcsparams3 register */
#define	XHCI_SET_HCSP3_U1EXITLATENCY(x)	((x) & 0xFF)
#define	XHCI_SET_HCSP3_U2EXITLATENCY(x)	(((x) & 0xFFFF) << 16)

/* hccparams1 register */
#define	XHCI_SET_HCCP1_AC64(x)		((x) & 0x01)
#define	XHCI_SET_HCCP1_BNC(x)		(((x) & 0x01) << 1)
#define	XHCI_SET_HCCP1_CSZ(x)		(((x) & 0x01) << 2)
#define	XHCI_SET_HCCP1_PPC(x)		(((x) & 0x01) << 3)
#define	XHCI_SET_HCCP1_PIND(x)		(((x) & 0x01) << 4)
#define	XHCI_SET_HCCP1_LHRC(x)		(((x) & 0x01) << 5)
#define	XHCI_SET_HCCP1_LTC(x)		(((x) & 0x01) << 6)
#define	XHCI_SET_HCCP1_NSS(x)		(((x) & 0x01) << 7)
#define	XHCI_SET_HCCP1_PAE(x)		(((x) & 0x01) << 8)
#define	XHCI_SET_HCCP1_SPC(x)		(((x) & 0x01) << 9)
#define	XHCI_SET_HCCP1_SEC(x)		(((x) & 0x01) << 10)
#define	XHCI_SET_HCCP1_CFC(x)		(((x) & 0x01) << 11)
#define	XHCI_SET_HCCP1_MAXPSA(x)	(((x) & 0x0F) << 12)
#define	XHCI_SET_HCCP1_XECP(x)		(((x) & 0xFFFF) << 16)

/* hccparams2 register */
#define	XHCI_SET_HCCP2_U3C(x)		((x) & 0x01)
#define	XHCI_SET_HCCP2_CMC(x)		(((x) & 0x01) << 1)
#define	XHCI_SET_HCCP2_FSC(x)		(((x) & 0x01) << 2)
#define	XHCI_SET_HCCP2_CTC(x)		(((x) & 0x01) << 3)
#define	XHCI_SET_HCCP2_LEC(x)		(((x) & 0x01) << 4)
#define	XHCI_SET_HCCP2_CIC(x)		(((x) & 0x01) << 5)

/* other registers */
#define	XHCI_SET_DOORBELL(x)		((x) & ~0x03)
#define	XHCI_SET_RTSOFFSET(x)		((x) & ~0x0F)

/* register masks */
#define	XHCI_PS_PLS_MASK		(0xF << 5)	/* port link state */
#define	XHCI_PS_SPEED_MASK		(0xF << 10)	/* port speed */
#define	XHCI_PS_PIC_MASK		(0x3 << 14)	/* port indicator */

/* port register set */
#define	XHCI_PORTREGS_BASE		0x400		/* base offset */
#define	XHCI_PORTREGS_PORT0		0x3F0
#define	XHCI_PORTREGS_SETSZ		0x10		/* size of a set */

#define	MASK_64_HI(x)			((x) & ~0xFFFFFFFFULL)
#define	MASK_64_LO(x)			((x) & 0xFFFFFFFFULL)

#define	FIELD_REPLACE(a,b,m,s)		(((a) & ~((m) << (s))) | \
					(((b) & (m)) << (s)))
#define	FIELD_COPY(a,b,m,s)		(((a) & ~((m) << (s))) | \
					(((b) & ((m) << (s)))))

struct pci_xhci_trb_ring {
	uint64_t ringaddr;		/* current dequeue guest address */
	uint32_t ccs;			/* consumer cycle state */
};

struct xhci_ep_timer_data {
	uint32_t slot;
	uint8_t epnum;
	uint8_t dir;
	struct pci_xhci_dev_emu *dev;
};

/* device endpoint transfer/stream rings */
struct pci_xhci_dev_ep {
	union {
		struct xhci_trb		*_epu_tr;
		struct xhci_stream_ctx	*_epu_sctx;
	} _ep_trbsctx;
#define	ep_tr		_ep_trbsctx._epu_tr
#define	ep_sctx		_ep_trbsctx._epu_sctx

	union {
		struct pci_xhci_trb_ring _epu_trb;
		struct pci_xhci_trb_ring *_epu_sctx_trbs;
	} _ep_trb_rings;
#define	ep_ringaddr	_ep_trb_rings._epu_trb.ringaddr
#define	ep_ccs		_ep_trb_rings._epu_trb.ccs
#define	ep_sctx_trbs	_ep_trb_rings._epu_sctx_trbs

	struct usb_data_xfer *ep_xfer;	/* transfer chain */
	struct xhci_ep_timer_data timer_data;
	pthread_mutex_t mtx;
};

/* device context base address array: maps slot->device context */
struct xhci_dcbaa {
	uint64_t dcba[USB_MAX_DEVICES+1]; /* xhci_dev_ctx ptrs */
};

/* port status registers */
struct pci_xhci_portregs {
	uint32_t	portsc;		/* port status and control */
	uint32_t	portpmsc;	/* port pwr mgmt status & control */
	uint32_t	portli;		/* port link info */
	uint32_t	porthlpmc;	/* port hardware LPM control */
} __packed;
#define	XHCI_PS_SPEED_SET(x)	(((x) & 0xF) << 10)

/* xHC operational registers */
struct pci_xhci_opregs {
	uint32_t	usbcmd;		/* usb command */
	uint32_t	usbsts;		/* usb status */
	uint32_t	pgsz;		/* page size */
	uint32_t	dnctrl;		/* device notification control */
	uint64_t	crcr;		/* command ring control */
	uint64_t	dcbaap;		/* device ctx base addr array ptr */
	uint32_t	config;		/* configure */

	/* guest mapped addresses: */
	struct xhci_trb	*cr_p;		/* crcr dequeue */
	struct xhci_dcbaa *dcbaa_p;	/* dev ctx array ptr */
};

/* xHC runtime registers */
struct pci_xhci_rtsregs {
	uint32_t	mfindex;	/* microframe index */
	struct {			/* interrupter register set */
		uint32_t	iman;	/* interrupter management */
		uint32_t	imod;	/* interrupter moderation */
		uint32_t	erstsz;	/* event ring segment table size */
		uint32_t	rsvd;
		uint64_t	erstba;	/* event ring seg-tbl base addr */
		uint64_t	erdp;	/* event ring dequeue ptr */
	} intrreg __packed;

	/* guest mapped addresses */
	struct xhci_event_ring_seg *erstba_p;
	struct xhci_trb *erst_p;	/* event ring segment tbl */
	int		er_deq_seg;	/* event ring dequeue segment */
	int		er_enq_idx;	/* event ring enqueue index - xHCI */
	int		er_enq_seg;	/* event ring enqueue segment */
	uint32_t	er_events_cnt;	/* number of events in ER */
	uint32_t	event_pcs;	/* producer cycle state flag */
};

/* this is used to describe the VBus Drop state */
enum pci_xhci_vbdp_state {
	S3_VBDP_NONE = 0,
	S3_VBDP_START,
	S3_VBDP_END
};

struct pci_xhci_softc;

/*
 * USB device emulation container.
 * This is referenced from usb_hci->hci_sc; 1 pci_xhci_dev_emu for each
 * emulated device instance.
 */
struct pci_xhci_dev_emu {
	struct pci_xhci_softc	*xsc;

	/* XHCI contexts */
	struct xhci_dev_ctx	*dev_ctx;
	struct pci_xhci_dev_ep	eps[XHCI_MAX_ENDPOINTS];
	int			dev_slotstate;

	struct usb_devemu	*dev_ue;	/* USB emulated dev */
	void			*dev_sc;	/* device's softc */

	struct usb_hci		hci;
};

struct pci_xhci_native_port {
	struct usb_native_devinfo info;
	uint8_t vport;
	uint8_t state;
};

/* This is used to describe the VBus Drop state */
struct pci_xhci_vbdp_dev_state {
	struct	usb_devpath path;
	uint8_t	vport;
	uint8_t	state;
};

struct pci_xhci_softc {
	struct pci_devinst *xsc_pi;

	pthread_mutex_t	mtx;

	uint32_t	caplength;	/* caplen & hciversion */
	uint32_t	hcsparams1;	/* structural parameters 1 */
	uint32_t	hcsparams2;	/* structural parameters 2 */
	uint32_t	hcsparams3;	/* structural parameters 3 */
	uint32_t	hccparams1;	/* capability parameters 1 */
	uint32_t	dboff;		/* doorbell offset */
	uint32_t	rtsoff;		/* runtime register space offset */
	uint32_t	hccparams2;	/* capability parameters 2 */

	uint32_t	regsend;	/* end of configuration registers */

	struct pci_xhci_opregs  opregs;
	struct pci_xhci_rtsregs rtsregs;

	struct pci_xhci_portregs *portregs;
	struct pci_xhci_dev_emu  **devices; /* XHCI[port] = device */
	struct pci_xhci_dev_emu  **slots;   /* slots assigned from 1 */

	bool            slot_allocated[XHCI_MAX_SLOTS + 1];

	int		ndevices;

	int		usb2_port_start;
	int		usb3_port_start;

	pthread_t	vbdp_thread;
	sem_t		vbdp_sem;
	bool		vbdp_polling;
	int		vbdp_dev_num;
	struct pci_xhci_vbdp_dev_state vbdp_devs[XHCI_MAX_DEVICES];
	struct pci_xhci_native_port native_ports[XHCI_MAX_DEVICES];

	int		tablet; /* flag enabled when emulating usb tablet */
};


/* portregs and devices arrays are set up to start from idx=1 */
#define	XHCI_PORTREG_PTR(x,n)	&(x)->portregs[(n)]
#define	XHCI_DEVINST_PTR(x,n)	(x)->devices[(n)]
#define	XHCI_SLOTDEV_PTR(x,n)	(x)->slots[(n)]

#define	XHCI_HALTED(sc)		((sc)->opregs.usbsts & XHCI_STS_HCH)

#define	XHCI_GADDR(sc,a)	paddr_guest2host((sc)->xsc_pi->pi_vmctx, \
				    (a),                                 \
				    XHCI_PADDR_SZ - ((a) & (XHCI_PADDR_SZ-1)))

#define XHCI_EPTYPE_INVALID	0
#define XHCI_EPTYPE_ISOC_OUT	1
#define XHCI_EPTYPE_BULK_OUT	2
#define XHCI_EPTYPE_INT_OUT	3
#define XHCI_EPTYPE_CTRL	4
#define XHCI_EPTYPE_ISOC_IN	5
#define XHCI_EPTYPE_BULK_IN	6
#define XHCI_EPTYPE_INT_IN	7

/* port mapping status */
#define VPORT_FREE (0)
#define VPORT_ASSIGNED (1)
#define VPORT_CONNECTED (2)
#define VPORT_EMULATED (3)

static int xhci_in_use;

/* map USB errors to XHCI */
static const int xhci_usb_errors[USB_ERR_MAX] = {
	[USB_ERR_NORMAL_COMPLETION]	= XHCI_TRB_ERROR_SUCCESS,
	[USB_ERR_PENDING_REQUESTS]	= XHCI_TRB_ERROR_RESOURCE,
	[USB_ERR_NOT_STARTED]		= XHCI_TRB_ERROR_ENDP_NOT_ON,
	[USB_ERR_INVAL]			= XHCI_TRB_ERROR_INVALID,
	[USB_ERR_NOMEM]			= XHCI_TRB_ERROR_RESOURCE,
	[USB_ERR_CANCELLED]		= XHCI_TRB_ERROR_STOPPED,
	[USB_ERR_BAD_ADDRESS]		= XHCI_TRB_ERROR_PARAMETER,
	[USB_ERR_BAD_BUFSIZE]		= XHCI_TRB_ERROR_PARAMETER,
	[USB_ERR_BAD_FLAG]		= XHCI_TRB_ERROR_PARAMETER,
	[USB_ERR_NO_CALLBACK]		= XHCI_TRB_ERROR_STALL,
	[USB_ERR_IN_USE]		= XHCI_TRB_ERROR_RESOURCE,
	[USB_ERR_NO_ADDR]		= XHCI_TRB_ERROR_RESOURCE,
	[USB_ERR_NO_PIPE]               = XHCI_TRB_ERROR_RESOURCE,
	[USB_ERR_ZERO_NFRAMES]          = XHCI_TRB_ERROR_UNDEFINED,
	[USB_ERR_ZERO_MAXP]             = XHCI_TRB_ERROR_UNDEFINED,
	[USB_ERR_SET_ADDR_FAILED]       = XHCI_TRB_ERROR_RESOURCE,
	[USB_ERR_NO_POWER]              = XHCI_TRB_ERROR_ENDP_NOT_ON,
	[USB_ERR_TOO_DEEP]              = XHCI_TRB_ERROR_RESOURCE,
	[USB_ERR_IOERROR]               = XHCI_TRB_ERROR_TRB,
	[USB_ERR_NOT_CONFIGURED]        = XHCI_TRB_ERROR_ENDP_NOT_ON,
	[USB_ERR_TIMEOUT]               = XHCI_TRB_ERROR_CMD_ABORTED,
	[USB_ERR_SHORT_XFER]            = XHCI_TRB_ERROR_SHORT_PKT,
	[USB_ERR_STALLED]               = XHCI_TRB_ERROR_STALL,
	[USB_ERR_INTERRUPTED]           = XHCI_TRB_ERROR_CMD_ABORTED,
	[USB_ERR_DMA_LOAD_FAILED]       = XHCI_TRB_ERROR_DATA_BUF,
	[USB_ERR_BAD_CONTEXT]           = XHCI_TRB_ERROR_TRB,
	[USB_ERR_NO_ROOT_HUB]           = XHCI_TRB_ERROR_UNDEFINED,
	[USB_ERR_NO_INTR_THREAD]        = XHCI_TRB_ERROR_UNDEFINED,
	[USB_ERR_NOT_LOCKED]            = XHCI_TRB_ERROR_UNDEFINED,
};
#define	USB_TO_XHCI_ERR(e)	((e) < USB_ERR_MAX ? xhci_usb_errors[(e)] : \
				XHCI_TRB_ERROR_INVALID)

static int pci_xhci_insert_event(struct pci_xhci_softc *sc,
    struct xhci_trb *evtrb, int do_intr);
static void pci_xhci_dump_trb(struct xhci_trb *trb);
static void pci_xhci_assert_interrupt(struct pci_xhci_softc *sc);
static void pci_xhci_reset_slot(struct pci_xhci_softc *sc, int slot);
static void pci_xhci_reset_port(struct pci_xhci_softc *sc, int portn, int warm);
static void pci_xhci_update_ep_ring(struct pci_xhci_softc *sc,
    struct pci_xhci_dev_emu *dev, struct pci_xhci_dev_ep *devep,
    struct xhci_endp_ctx *ep_ctx, uint32_t streamid,
    uint64_t ringaddr, int ccs);
static void pci_xhci_init_port(struct pci_xhci_softc *sc, int portn);
static void pci_xhci_free_usb_xfer(struct usb_data_xfer *xfer);
static struct pci_xhci_dev_emu* pci_xhci_dev_create(struct pci_xhci_softc *xdev, void *dev_data);
static void pci_xhci_dev_destroy(struct pci_xhci_dev_emu *de);

static void
pci_xhci_set_evtrb(struct xhci_trb *evtrb, uint64_t port, uint32_t errcode,
    uint32_t evtype)
{
	evtrb->qwTrb0 = port << 24;
	evtrb->dwTrb2 = XHCI_TRB_2_ERROR_SET(errcode);
	evtrb->dwTrb3 = XHCI_TRB_3_TYPE_SET(evtype);
}

static bool
pci_xhci_is_vport_free(struct pci_xhci_softc *xdev, int portnum)
{
	int i;

	for (i = 0; i < XHCI_MAX_DEVICES; i++)
		if (xdev->native_ports[i].vport == portnum) {
			return true;
		}

	return false;
}

static int
pci_xhci_convert_speed(int lspeed)
{
	/* according to xhci spec, zero means undefined speed */
	int speed = 0;

	switch (lspeed) {
	case USB_SPEED_LOW:
		speed = 0x2;
		break;
	case USB_SPEED_FULL:
		speed = 0x1;
		break;
	case USB_SPEED_HIGH:
		speed = 0x3;
		break;
	case USB_SPEED_SUPER:
		speed = 0x4;
		break;
	default:
		DPRINTF(("unkown speed %08x\r\n", lspeed));
	}
	return speed;
}

static inline int
pci_xhci_is_valid_portnum(int n)
{
	return n > 0 && n <= XHCI_MAX_DEVS;
}

static int
pci_xhci_change_port(struct pci_xhci_softc *xdev, int port, int usb_speed,
		int conn, int need_intr)
{
	int speed, error;
	struct xhci_trb evtrb;
	struct pci_xhci_portregs *reg;

	reg = XHCI_PORTREG_PTR(xdev, port);
	if (conn == 0) {
		reg->portsc &= ~(XHCI_PS_CCS | XHCI_PS_PED);
		reg->portsc |= (XHCI_PS_CSC |
				XHCI_PS_PLS_SET(UPS_PORT_LS_RX_DET));
	} else {
		speed = pci_xhci_convert_speed(usb_speed);
		reg->portsc = XHCI_PS_CCS | XHCI_PS_PP | XHCI_PS_CSC;
		reg->portsc |= XHCI_PS_SPEED_SET(speed);
	}

	if (!need_intr)
		return 0;

	if (!(xdev->opregs.usbcmd & XHCI_CMD_INTE))
		need_intr = 0;

	if (!(xdev->opregs.usbcmd & XHCI_CMD_RS))
		return 0;

	/* make an event for the guest OS */
	pci_xhci_set_evtrb(&evtrb,
			port,
			XHCI_TRB_ERROR_SUCCESS,
			XHCI_TRB_EVENT_PORT_STS_CHANGE);

	/* put it in the event ring */
	error = pci_xhci_insert_event(xdev, &evtrb, 1);
	if (error != XHCI_TRB_ERROR_SUCCESS)
		DPRINTF(("fail to report port change\r\n"));

	DPRINTF(("%s: port %d:%08X\r\n", __func__, port, reg->portsc));
	return (error == XHCI_TRB_ERROR_SUCCESS) ? 0 : -1;
}

static int
pci_xhci_connect_port(struct pci_xhci_softc *xdev, int port, int usb_speed,
		int intr)
{
	return pci_xhci_change_port(xdev, port, usb_speed, 1, intr);
}

static int
pci_xhci_disconnect_port(struct pci_xhci_softc *xdev, int port, int intr)
{
	/* for disconnect, the speed is useless */
	return pci_xhci_change_port(xdev, port, 0, 0, intr);
}

static int
pci_xhci_get_native_port_index_by_path(struct pci_xhci_softc *xdev,
		struct usb_devpath *path)
{
	int i;

	for (i = 0; i < XHCI_MAX_DEVICES; i++) {
		if (usb_dev_path_cmp(&xdev->native_ports[i].info.path, path)) {
			return i;
		}
	}
	return -1;
}

static int
pci_xhci_get_native_port_index_by_vport(struct pci_xhci_softc *xdev,
	uint8_t vport)
{
	int i;

	for (i = 0; i < XHCI_MAX_DEVICES; i++)
		if (xdev->native_ports[i].vport == vport)
			return i;

	return -1;
}

static int
pci_xhci_set_native_port_assigned(struct pci_xhci_softc *xdev,
		struct usb_native_devinfo *info)
{
	int i;

	for (i = 0; i < XHCI_MAX_DEVICES; i++)
		if (xdev->native_ports[i].state == VPORT_FREE)
			break;

	if (i < XHCI_MAX_DEVICES) {
		xdev->native_ports[i].info = *info;
		xdev->native_ports[i].state = VPORT_ASSIGNED;
		return i;
	}

	return -1;
}

static int
pci_xhci_assign_hub_ports(struct pci_xhci_softc *xdev,
		struct usb_native_devinfo *info)
{
	int index;
	uint8_t i;
	struct usb_native_devinfo di;
	struct usb_devpath *path;

	if (!xdev || !info || info->type != USB_TYPE_EXTHUB)
		return -1;

	index = pci_xhci_get_native_port_index_by_path(xdev, &info->path);
	if (index < 0) {
		DPRINTF(("cannot find hub %d-%s\r\n", info->path.bus,
			 usb_dev_path(&info->path)));
		return -1;
	}

	xdev->native_ports[index].info = *info;
	DPRINTF(("Found an USB hub %d-%s with %d port(s).\r\n",
		info->path.bus, usb_dev_path(&info->path),
		info->maxchild));

	path = &di.path;
	for (i = 1; i <= info->maxchild; i++) {
		/* make a device path for hub ports */
		memcpy(path->path, info->path.path, info->path.depth);
		memcpy(path->path + info->path.depth, &i, sizeof(i));
		memset(path->path + info->path.depth + 1, 0,
				USB_MAX_TIERS - info->path.depth - 1);
		path->depth = info->path.depth + 1;
		path->bus = info->path.bus;

		/* set the device path as assigned */
		index = pci_xhci_set_native_port_assigned(xdev, &di);
		if (index < 0) {
			DPRINTF(("too many USB devices\r\n"));
			return -1;
		}
		DPRINTF(("Add %d-%s as assigned port\r\n",
			path->bus, usb_dev_path(path)));
	}
	return 0;
}

static void
pci_xhci_clr_native_port_assigned(struct pci_xhci_softc *xdev,
		struct usb_native_devinfo *info)
{
	int i;

	i = pci_xhci_get_native_port_index_by_path(xdev, &info->path);
	if (i >= 0) {
		xdev->native_ports[i].state = VPORT_FREE;
		xdev->native_ports[i].vport = 0;
		memset(&xdev->native_ports[i].info, 0, sizeof(*info));
	}
}

static int
pci_xhci_unassign_hub_ports(struct pci_xhci_softc *xdev,
		struct usb_native_devinfo *info)
{
	uint8_t i, index;
	struct usb_native_devinfo di, *oldinfo;
	struct usb_devpath *path;

	if (!xdev || !info || info->type != USB_TYPE_EXTHUB)
		return -1;

	index = pci_xhci_get_native_port_index_by_path(xdev, &info->path);
	if (index < 0) {
		DPRINTF(("cannot find USB hub %d-%s\r\n",
			info->path.bus, usb_dev_path(&info->path)));
		return -1;
	}

	oldinfo = &xdev->native_ports[index].info;
	DPRINTF(("Disconnect an USB hub %d-%s with %d port(s)\r\n",
			oldinfo->path.bus, usb_dev_path(&oldinfo->path),
			oldinfo->maxchild));

	path = &di.path;
	for (i = 1; i <= oldinfo->maxchild; i++) {

		/* make a device path for hub ports */
		memcpy(path->path, oldinfo->path.path, oldinfo->path.depth);
		memcpy(path->path + oldinfo->path.depth, &i, sizeof(i));
		memset(path->path + oldinfo->path.depth + 1, 0,
				USB_MAX_TIERS - oldinfo->path.depth - 1);
		path->depth = oldinfo->path.depth + 1;
		path->bus = oldinfo->path.bus;

		/* clear the device path as not assigned */
		pci_xhci_clr_native_port_assigned(xdev, &di);
		DPRINTF(("Del %d-%s as assigned port\r\n",
				path->bus, usb_dev_path(path)));
	}
	return 0;
}

static int
pci_xhci_get_free_vport(struct pci_xhci_softc *xdev,
		struct usb_native_devinfo *di)
{
	int ports, porte;
	int i, j, k;

	if (di->bcd < 0x300)
		ports = xdev->usb2_port_start;
	else
		ports = xdev->usb3_port_start;

	porte = ports + (XHCI_MAX_DEVS / 2);

	for (i = ports; i <= porte; i++) {
		for (j = 0; j < XHCI_MAX_DEVICES; j++) {
			if (xdev->native_ports[j].vport == i)
				break;

			k = xdev->vbdp_dev_num;
			if (k > 0 && xdev->vbdp_devs[j].state == S3_VBDP_START
					&& xdev->vbdp_devs[j].vport == i)
				break;
		}
		if (j >= XHCI_MAX_DEVICES)
			return i;
	}
	return -1;
}

static void *
xhci_vbdp_thread(void *data)
{
	int i, j;
	int speed;
	struct pci_xhci_softc *xdev;
	struct pci_xhci_native_port *p;

	xdev = data;
	while (xdev->vbdp_polling) {

		sem_wait(&xdev->vbdp_sem);
		for (i = 0; i < XHCI_MAX_DEVICES; ++i)
			if (xdev->vbdp_devs[i].state == S3_VBDP_END) {
				xdev->vbdp_devs[i].state = S3_VBDP_NONE;
				break;
			}

		if (i >= XHCI_MAX_DEVICES)
			continue;

		j = pci_xhci_get_native_port_index_by_path(xdev,
				&xdev->vbdp_devs[i].path);
		if (j < 0)
			continue;

		p = &xdev->native_ports[j];
		if (p->state != VPORT_CONNECTED)
			continue;

		speed = pci_xhci_convert_speed(p->info.speed);
		pci_xhci_connect_port(xdev, p->vport, speed, 1);
		DPRINTF(("change portsc for %d-%s\r\n", p->info.path.bus,
			usb_dev_path(&p->info.path)));
	}
	return NULL;
}

/* controller reset */
static void
pci_xhci_reset(struct pci_xhci_softc *sc)
{
	int i;

	sc->rtsregs.er_enq_idx = 0;
	sc->rtsregs.er_events_cnt = 0;
	sc->rtsregs.event_pcs = 1;

	for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
		pci_xhci_reset_slot(sc, i);
	}
}

static uint32_t
pci_xhci_usbcmd_write(struct pci_xhci_softc *sc, uint32_t cmd)
{
	int do_intr = 0;
	int i;
	int j;
	struct pci_xhci_native_port *p;

	if (cmd & XHCI_CMD_RS) {
		do_intr = (sc->opregs.usbcmd & XHCI_CMD_RS) == 0;

		sc->opregs.usbcmd |= XHCI_CMD_RS;
		sc->opregs.usbsts &= ~XHCI_STS_HCH;
		sc->opregs.usbsts |= XHCI_STS_PCD;

		/* Queue port change event on controller run from stop */
		if (do_intr)
			for (i = 1; i <= XHCI_MAX_DEVS; i++) {
				struct pci_xhci_dev_emu *dev;
				struct pci_xhci_portregs *port;
				struct xhci_trb		evtrb;

				if ((dev = XHCI_DEVINST_PTR(sc, i)) == NULL)
					continue;

				port = XHCI_PORTREG_PTR(sc, i);
				port->portsc |= XHCI_PS_CSC | XHCI_PS_CCS;
				port->portsc &= ~XHCI_PS_PLS_MASK;

				/*
				 * XHCI 4.19.3 USB2 RxDetect->Polling,
				 *             USB3 Polling->U0
				 */

				if (dev->dev_ue->ue_usbver == 2)
					port->portsc |=
					    XHCI_PS_PLS_SET(UPS_PORT_LS_POLL);
				else
					port->portsc |=
					    XHCI_PS_PLS_SET(UPS_PORT_LS_U0);

				pci_xhci_set_evtrb(&evtrb, i,
				    XHCI_TRB_ERROR_SUCCESS,
				    XHCI_TRB_EVENT_PORT_STS_CHANGE);

				if (pci_xhci_insert_event(sc, &evtrb, 0) !=
				    XHCI_TRB_ERROR_SUCCESS)
					break;
			}
	} else {
		sc->opregs.usbcmd &= ~XHCI_CMD_RS;
		sc->opregs.usbsts |= XHCI_STS_HCH;
		sc->opregs.usbsts &= ~XHCI_STS_PCD;
	}

	/* start execution of schedule; stop when set to 0 */
	cmd |= sc->opregs.usbcmd & XHCI_CMD_RS;

	if (cmd & XHCI_CMD_HCRST) {
		/* reset controller */
		pci_xhci_reset(sc);
		cmd &= ~XHCI_CMD_HCRST;
	}

	if (cmd & XHCI_CMD_CSS) {

		sc->vbdp_dev_num = 0;
		memset(sc->vbdp_devs, 0, sizeof(sc->vbdp_devs));

		for (i = 0; i < XHCI_MAX_DEVICES; i++) {
			p = &sc->native_ports[i];
			if (sc->native_ports[i].state == VPORT_EMULATED) {
				/* save the device state before suspending */
				j = sc->vbdp_dev_num;
				sc->vbdp_devs[i].path = p->info.path;
				sc->vbdp_devs[i].vport = p->vport;
				sc->vbdp_devs[i].state = S3_VBDP_START;
				sc->vbdp_dev_num++;

				/* clear PORTSC register */
				pci_xhci_init_port(sc, p->vport);

				/* clear other information for this device */
				p->vport = 0;
				p->state = VPORT_ASSIGNED;
				DPRINTF(("s3: save %d-%s state\r\n",
						p->info.path.bus,
						usb_dev_path(&p->info.path)));
			}
		}
	 }

	cmd &= ~(XHCI_CMD_CSS | XHCI_CMD_CRS);

	if (do_intr)
		pci_xhci_assert_interrupt(sc);

	return (cmd);
}

static void
pci_xhci_portregs_write(struct pci_xhci_softc *sc, uint64_t offset,
    uint64_t value)
{
	struct xhci_trb		evtrb;
	struct pci_xhci_portregs *p;
	int port;
	uint32_t oldpls, newpls;

	if (sc->portregs == NULL)
		return;

	port = (offset - XHCI_PORTREGS_PORT0) / XHCI_PORTREGS_SETSZ;
	offset = (offset - XHCI_PORTREGS_PORT0) % XHCI_PORTREGS_SETSZ;

	DPRINTF(("pci_xhci: portregs wr offset 0x%lx, port %u: 0x%lx",
	        offset, port, value));

	assert(port >= 0);

	if (port > XHCI_MAX_DEVS) {
		DPRINTF(("pci_xhci: portregs_write port %d > ndevices",
		    port));
		return;
	}

	if (XHCI_DEVINST_PTR(sc, port) == NULL) {
		DPRINTF(("pci_xhci: portregs_write to unattached port %d",
		     port));
	}

	p = XHCI_PORTREG_PTR(sc, port);
	switch (offset) {
	case 0:
		/* port reset or warm reset */
		if (value & (XHCI_PS_PR | XHCI_PS_WPR)) {
			pci_xhci_reset_port(sc, port, value & XHCI_PS_WPR);
			break;
		}

		if ((p->portsc & XHCI_PS_PP) == 0) {
			WPRINTF(("pci_xhci: portregs_write to unpowered "
			         "port %d", port));
			break;
		}

		/* Port status and control register  */
		oldpls = XHCI_PS_PLS_GET(p->portsc);
		newpls = XHCI_PS_PLS_GET(value);

		p->portsc &= XHCI_PS_PED | XHCI_PS_PLS_MASK |
		             XHCI_PS_SPEED_MASK | XHCI_PS_PIC_MASK;

		if ((sc->tablet && XHCI_DEVINST_PTR(sc, port)) ||
				(!sc->tablet && (pci_xhci_is_vport_free(sc, port) == true))) {
			p->portsc |= XHCI_PS_CCS;
		}

		p->portsc |= (value &
		              ~(XHCI_PS_OCA |
		                XHCI_PS_PR  |
			        XHCI_PS_PED |
			        XHCI_PS_PLS_MASK   |	/* link state */
			        XHCI_PS_SPEED_MASK |
			        XHCI_PS_PIC_MASK   |	/* port indicator */
			        XHCI_PS_LWS | XHCI_PS_DR | XHCI_PS_WPR));

		/* clear control bits */
		p->portsc &= ~(value &
		               (XHCI_PS_CSC |
		                XHCI_PS_PEC |
		                XHCI_PS_WRC |
		                XHCI_PS_OCC |
		                XHCI_PS_PRC |
		                XHCI_PS_PLC |
		                XHCI_PS_CEC |
		                XHCI_PS_CAS));

		/* port disable request; for USB3, don't care */
		if (value & XHCI_PS_PED)
			DPRINTF(("Disable port %d request", port));

		if (!(value & XHCI_PS_LWS))
			break;

		DPRINTF(("Port new PLS: %d", newpls));
		switch (newpls) {
		case 0: /* U0 */
		case 3: /* U3 */
			if (oldpls != newpls) {
				p->portsc &= ~XHCI_PS_PLS_MASK;
				p->portsc |= XHCI_PS_PLS_SET(newpls) |
				             XHCI_PS_PLC;

				if (oldpls != 0 && newpls == 0) {
					pci_xhci_set_evtrb(&evtrb, port,
					    XHCI_TRB_ERROR_SUCCESS,
					    XHCI_TRB_EVENT_PORT_STS_CHANGE);

					pci_xhci_insert_event(sc, &evtrb, 1);
				}
			}
			break;

		default:
			DPRINTF(("Unhandled change port %d PLS %u",
			         port, newpls));
			break;
		}
		break;
	case 4:
		/* Port power management status and control register  */
		p->portpmsc = value;
		break;
	case 8:
		/* Port link information register */
		DPRINTF(("pci_xhci attempted write to PORTLI, port %d",
		        port));
		break;
	case 12:
		/*
		 * Port hardware LPM control register.
		 * For USB3, this register is reserved.
		 */
		p->porthlpmc = value;
		break;
	}
}

struct xhci_dev_ctx *
pci_xhci_get_dev_ctx(struct pci_xhci_softc *sc, uint32_t slot)
{
	uint64_t devctx_addr;
	struct xhci_dev_ctx *devctx;

	assert(slot > 0 && slot <= sc->ndevices);
	assert(sc->opregs.dcbaa_p != NULL);

	if (!sc->tablet && !sc->slot_allocated[slot]) {
		DPRINTF(("invalid ctx: slot %d, alloc %d dcbaa %p",
			slot, sc->slot_allocated[slot],
			sc->opregs.dcbaa_p));
		return NULL;
	}

	devctx_addr = sc->opregs.dcbaa_p->dcba[slot];

	if (devctx_addr == 0) {
		DPRINTF(("get_dev_ctx devctx_addr == 0"));
		return (NULL);
	}

	DPRINTF(("pci_xhci: get dev ctx, slot %u devctx addr %016lx",
	        slot, devctx_addr));
	devctx = XHCI_GADDR(sc, devctx_addr & ~0x3FUL);

	return (devctx);
}

struct xhci_trb *
pci_xhci_trb_next(struct pci_xhci_softc *sc, struct xhci_trb *curtrb,
    uint64_t *guestaddr)
{
	struct xhci_trb *next;

	assert(curtrb != NULL);

	if (XHCI_TRB_3_TYPE_GET(curtrb->dwTrb3) == XHCI_TRB_TYPE_LINK) {
		if (guestaddr)
			*guestaddr = curtrb->qwTrb0 & ~0xFUL;

		next = XHCI_GADDR(sc, curtrb->qwTrb0 & ~0xFUL);
	} else {
		if (guestaddr)
			*guestaddr += sizeof(struct xhci_trb) & ~0xFUL;

		next = curtrb + 1;
	}

	return (next);
}

static void
pci_xhci_assert_interrupt(struct pci_xhci_softc *sc)
{
	sc->rtsregs.intrreg.erdp |= XHCI_ERDP_LO_BUSY;
	sc->rtsregs.intrreg.iman |= XHCI_IMAN_INTR_PEND;
	sc->opregs.usbsts |= XHCI_STS_EINT;

	/* only trigger interrupt if permitted */
	if ((sc->opregs.usbcmd & XHCI_CMD_INTE) &&
	    (sc->rtsregs.intrreg.iman & XHCI_IMAN_INTR_ENA)) {
		if (pci_msi_enabled(sc->xsc_pi))
			pci_generate_msi(sc->xsc_pi, 0);
		else
			pci_lintr_assert(sc->xsc_pi);
	}
}

static void
pci_xhci_deassert_interrupt(struct pci_xhci_softc *sc)
{
	if (!pci_msi_enabled(sc->xsc_pi))
		pci_lintr_assert(sc->xsc_pi);
}

static struct usb_data_xfer *
pci_xhci_alloc_usb_xfer(struct pci_xhci_dev_emu *dev, int epid)
{
	struct usb_data_xfer *xfer;
	struct xhci_dev_ctx *dev_ctx;
	struct xhci_endp_ctx *ep_ctx;
	int max_blk_cnt;
	uint8_t type;

	if (!dev) {
		return NULL;
	}

	dev_ctx = dev->dev_ctx;
	ep_ctx = &dev_ctx->ctx_ep[epid];
	type = XHCI_EPCTX_1_EPTYPE_GET(ep_ctx->dwEpCtx1);

	/* TODO:
	 * The following code is still not perfect, due to fixed values are
	 * not flexible and the overflow risk is still existed.
	 */
	switch (type) {
	case XHCI_EPTYPE_CTRL:
	case XHCI_EPTYPE_INT_IN:
	case XHCI_EPTYPE_INT_OUT:
		max_blk_cnt = 128;
		break;
	case XHCI_EPTYPE_BULK_IN:
	case XHCI_EPTYPE_BULK_OUT:
		max_blk_cnt = 1024;
		break;
	case XHCI_EPTYPE_ISOC_IN:
	case XHCI_EPTYPE_ISOC_OUT:
		max_blk_cnt = 2048;
		break;
	default:
		DPRINTF(("err: unexpected epid %d type %d", epid, type));
		return NULL;
	}

	xfer = calloc(1, sizeof(struct usb_data_xfer));
	if (!xfer) {
		return NULL;
	}

	xfer->reqs = calloc(max_blk_cnt, sizeof(struct usb_dev_req *));
	if (!xfer->reqs) {
		goto fail;
	}

	DPRINTF(("allocate %d blocks for epid %d type %d",
			max_blk_cnt, epid, type));

	xfer->max_blk_cnt = max_blk_cnt;
	xfer->dev = (void *)dev;
	xfer->epid = epid;
	return xfer;
fail:
	if (xfer->reqs)
		free(xfer->reqs);
	free(xfer);
	return NULL;

}

static void
pci_xhci_free_usb_xfer(struct usb_data_xfer *xfer)
{
	if (!xfer)
		return;

	free(xfer->data);
	free(xfer->reqs);
	free(xfer->ureq);
	free(xfer);
}

static void
pci_xhci_init_ep(struct pci_xhci_dev_emu *dev, int epid, int slot)
{
	struct xhci_dev_ctx    *dev_ctx;
	struct pci_xhci_dev_ep *devep;
	struct xhci_endp_ctx   *ep_ctx;
	uint32_t	pstreams;
	int		i;

	dev_ctx = dev->dev_ctx;
	ep_ctx = &dev_ctx->ctx_ep[epid];
	devep = &dev->eps[epid];
	pstreams = XHCI_EPCTX_0_MAXP_STREAMS_GET(ep_ctx->dwEpCtx0);
	if (pstreams > 0) {
		DPRINTF(("init_ep %d with pstreams %d", epid, pstreams));
		assert(devep->ep_sctx_trbs == NULL);

		devep->ep_sctx = XHCI_GADDR(dev->xsc, ep_ctx->qwEpCtx2 &
		                            XHCI_EPCTX_2_TR_DQ_PTR_MASK);
		devep->ep_sctx_trbs = calloc(pstreams,
		                      sizeof(struct pci_xhci_trb_ring));
		for (i = 0; i < pstreams; i++) {
			devep->ep_sctx_trbs[i].ringaddr =
			                         devep->ep_sctx[i].qwSctx0 &
			                         XHCI_SCTX_0_TR_DQ_PTR_MASK;
			devep->ep_sctx_trbs[i].ccs =
			     XHCI_SCTX_0_DCS_GET(devep->ep_sctx[i].qwSctx0);
		}
	} else {
		DPRINTF(("init_ep %d with no pstreams", epid));
		devep->ep_ringaddr = ep_ctx->qwEpCtx2 &
		                     XHCI_EPCTX_2_TR_DQ_PTR_MASK;
		devep->ep_ccs = XHCI_EPCTX_2_DCS_GET(ep_ctx->qwEpCtx2);
		devep->ep_tr = XHCI_GADDR(dev->xsc, devep->ep_ringaddr);
		DPRINTF(("init_ep tr DCS %x", devep->ep_ccs));
	}

	if (devep->ep_xfer == NULL) {
		devep->ep_xfer = pci_xhci_alloc_usb_xfer(dev, epid);
		if (!devep->ep_xfer) {
			DPRINTF(("[pci_xhci_init_ep] errout"));
			pci_xhci_free_usb_xfer(devep->ep_xfer);
			devep->ep_xfer = NULL;
			devep->timer_data.dev = NULL;
			devep->timer_data.slot = 0;
			devep->timer_data.epnum = 0;
		}
	}

	devep->timer_data.dev = dev;
	devep->timer_data.slot = slot;
	devep->timer_data.epnum = epid;
	devep->timer_data.dir = (epid & 0x1) ? TOKEN_IN : TOKEN_OUT;
}

static void
pci_xhci_disable_ep(struct pci_xhci_dev_emu *dev, int epid)
{
	struct xhci_dev_ctx    *dev_ctx;
	struct pci_xhci_dev_ep *devep;
	struct xhci_endp_ctx   *ep_ctx;

	DPRINTF(("pci_xhci disable_ep %d", epid));

	dev_ctx = dev->dev_ctx;
	ep_ctx = &dev_ctx->ctx_ep[epid];
	ep_ctx->dwEpCtx0 = (ep_ctx->dwEpCtx0 & ~0x7) | XHCI_ST_EPCTX_DISABLED;

	devep = &dev->eps[epid];
	if (XHCI_EPCTX_0_MAXP_STREAMS_GET(ep_ctx->dwEpCtx0) > 0 &&
	    devep->ep_sctx_trbs != NULL)
			free(devep->ep_sctx_trbs);

	if (devep->ep_xfer != NULL) {
		free(devep->ep_xfer);
		devep->ep_xfer = NULL;
	}

	memset(devep, 0, sizeof(struct pci_xhci_dev_ep));
}


/* reset device at slot and data structures related to it */
static void
pci_xhci_reset_slot(struct pci_xhci_softc *sc, int slot)
{
	struct pci_xhci_dev_emu *dev;

	dev = XHCI_SLOTDEV_PTR(sc, slot);

	if (!dev) {
		DPRINTF(("xhci reset unassigned slot (%d)?", slot));
	} else {
		dev->dev_slotstate = XHCI_ST_DISABLED;
	}

	/* TODO: reset ring buffer pointers */
}

static int
pci_xhci_insert_event(struct pci_xhci_softc *sc, struct xhci_trb *evtrb,
    int do_intr)
{
	struct pci_xhci_rtsregs *rts;
	uint64_t	erdp;
	int		erdp_idx;
	int		err;
	struct xhci_trb *evtrbptr;

	err = XHCI_TRB_ERROR_SUCCESS;

	rts = &sc->rtsregs;

	erdp = rts->intrreg.erdp & ~0xF;
	erdp_idx = (erdp - rts->erstba_p[rts->er_deq_seg].qwEvrsTablePtr) /
	           sizeof(struct xhci_trb);

	DPRINTF(("pci_xhci: insert event 0[%lx] 2[%x] 3[%x]",
	         evtrb->qwTrb0, evtrb->dwTrb2, evtrb->dwTrb3));
	DPRINTF(("\terdp idx %d/seg %d, enq idx %d/seg %d, pcs %u",
	         erdp_idx, rts->er_deq_seg, rts->er_enq_idx,
	         rts->er_enq_seg, rts->event_pcs));
	DPRINTF(("\t(erdp=0x%lx, erst=0x%lx, tblsz=%u, do_intr %d)",
		 erdp, rts->erstba_p->qwEvrsTablePtr,
	         rts->erstba_p->dwEvrsTableSize, do_intr));

	evtrbptr = &rts->erst_p[rts->er_enq_idx];

	/* TODO: multi-segment table */
	if (rts->er_events_cnt >= rts->erstba_p->dwEvrsTableSize) {
		DPRINTF(("pci_xhci[%d] cannot insert event; ring full",
		         __LINE__));
		err = XHCI_TRB_ERROR_EV_RING_FULL;
		goto done;
	}

	if (rts->er_events_cnt == rts->erstba_p->dwEvrsTableSize - 1) {
		struct xhci_trb	errev;

		if ((evtrbptr->dwTrb3 & 0x1) == (rts->event_pcs & 0x1)) {

			DPRINTF(("pci_xhci[%d] insert evt err: ring full",
			         __LINE__));

			errev.qwTrb0 = 0;
			errev.dwTrb2 = XHCI_TRB_2_ERROR_SET(
			                    XHCI_TRB_ERROR_EV_RING_FULL);
			errev.dwTrb3 = XHCI_TRB_3_TYPE_SET(
			                    XHCI_TRB_EVENT_HOST_CTRL) |
			               rts->event_pcs;
			rts->er_events_cnt++;
			memcpy(&rts->erst_p[rts->er_enq_idx], &errev,
			       sizeof(struct xhci_trb));
			rts->er_enq_idx = (rts->er_enq_idx + 1) %
			                  rts->erstba_p->dwEvrsTableSize;
			err = XHCI_TRB_ERROR_EV_RING_FULL;
			do_intr = 1;

			goto done;
		}
	} else {
		rts->er_events_cnt++;
	}

	evtrb->dwTrb3 &= ~XHCI_TRB_3_CYCLE_BIT;
	evtrb->dwTrb3 |= rts->event_pcs;

	memcpy(&rts->erst_p[rts->er_enq_idx], evtrb, sizeof(struct xhci_trb));
	rts->er_enq_idx = (rts->er_enq_idx + 1) %
	                  rts->erstba_p->dwEvrsTableSize;

	if (rts->er_enq_idx == 0)
		rts->event_pcs ^= 1;

done:
	if (do_intr)
		pci_xhci_assert_interrupt(sc);

	return (err);
}

static uint32_t
pci_xhci_cmd_enable_slot(struct pci_xhci_softc *sc, uint32_t *slot)
{
	struct pci_xhci_dev_emu *dev;
	uint32_t	cmderr;
	int		i;

	cmderr = XHCI_TRB_ERROR_NO_SLOTS;
	if (sc->portregs != NULL)
		for (i = 1; i <= XHCI_MAX_SLOTS; i++) {
			dev = XHCI_SLOTDEV_PTR(sc, i);
			if (dev && dev->dev_slotstate == XHCI_ST_DISABLED) {
				*slot = i;
				dev->dev_slotstate = XHCI_ST_ENABLED;
				cmderr = XHCI_TRB_ERROR_SUCCESS;
				dev->hci.hci_address = i;
				break;
			}
		}

	for (i = 1; i <= XHCI_MAX_SLOTS; i++)
		if (sc->slot_allocated[i] == false)
			break;

	if (i < XHCI_MAX_SLOTS) {
		sc->slot_allocated[i] = true;
		*slot = i;
		cmderr = XHCI_TRB_ERROR_SUCCESS;
	}

	DPRINTF(("pci_xhci enable slot (error=%d) slot %u",
		cmderr != XHCI_TRB_ERROR_SUCCESS, *slot));

	return (cmderr);
}

static uint32_t
pci_xhci_cmd_disable_slot(struct pci_xhci_softc *sc, uint32_t slot)
{
	struct pci_xhci_dev_emu *dev;
	uint32_t cmderr;

	DPRINTF(("pci_xhci disable slot %u", slot));

	cmderr = XHCI_TRB_ERROR_NO_SLOTS;
	if (sc->portregs == NULL)
		goto done;

	if (slot > sc->ndevices) {
		cmderr = XHCI_TRB_ERROR_SLOT_NOT_ON;
		goto done;
	}

	dev = XHCI_SLOTDEV_PTR(sc, slot);
	if (dev) {
		if (dev->dev_slotstate == XHCI_ST_DISABLED) {
			cmderr = XHCI_TRB_ERROR_SLOT_NOT_ON;
		} else {
			dev->dev_slotstate = XHCI_ST_DISABLED;
			cmderr = XHCI_TRB_ERROR_SUCCESS;
			/* TODO: reset events and endpoints */
		}
	}

done:
	return (cmderr);
}

static uint32_t
pci_xhci_cmd_reset_device(struct pci_xhci_softc *sc, uint32_t slot)
{
	struct pci_xhci_dev_emu *dev;
	struct xhci_dev_ctx     *dev_ctx;
	struct xhci_endp_ctx    *ep_ctx;
	uint32_t	cmderr;
	int		i;

	cmderr = XHCI_TRB_ERROR_NO_SLOTS;
	if (sc->portregs == NULL)
		goto done;

	DPRINTF(("pci_xhci reset device slot %u", slot));

	dev = XHCI_SLOTDEV_PTR(sc, slot);
	if (!dev || dev->dev_slotstate == XHCI_ST_DISABLED)
		cmderr = XHCI_TRB_ERROR_SLOT_NOT_ON;
	else {
		dev->dev_slotstate = XHCI_ST_DEFAULT;

		dev->hci.hci_address = 0;
		dev_ctx = pci_xhci_get_dev_ctx(sc, slot);

		/* slot state */
		dev_ctx->ctx_slot.dwSctx3 = FIELD_REPLACE(
		    dev_ctx->ctx_slot.dwSctx3, XHCI_ST_SLCTX_DEFAULT,
		    0x1F, 27);

		/* number of contexts */
		dev_ctx->ctx_slot.dwSctx0 = FIELD_REPLACE(
		    dev_ctx->ctx_slot.dwSctx0, 1, 0x1F, 27);

		/* reset all eps other than ep-0 */
		for (i = 2; i <= 31; i++) {
			ep_ctx = &dev_ctx->ctx_ep[i];
			ep_ctx->dwEpCtx0 = FIELD_REPLACE( ep_ctx->dwEpCtx0,
			    XHCI_ST_EPCTX_DISABLED, 0x7, 0);
		}

		cmderr = XHCI_TRB_ERROR_SUCCESS;
	}

	pci_xhci_reset_slot(sc, slot);

done:
	return (cmderr);
}

static uint32_t
pci_xhci_cmd_address_device(struct pci_xhci_softc *sc, uint32_t slot,
    struct xhci_trb *trb)
{
	struct pci_xhci_dev_emu	*dev;
	struct xhci_input_dev_ctx *input_ctx;
	struct xhci_slot_ctx	*islot_ctx;
	struct xhci_dev_ctx	*dev_ctx;
	struct xhci_endp_ctx	*ep0_ctx;
	struct usb_native_devinfo *di;
	uint32_t cmderr;
	uint8_t rh_port;

	input_ctx = XHCI_GADDR(sc, trb->qwTrb0 & ~0xFUL);
	islot_ctx = &input_ctx->ctx_slot;
	ep0_ctx = &input_ctx->ctx_ep[1];

	cmderr = XHCI_TRB_ERROR_SUCCESS;

	DPRINTF(("pci_xhci: address device, input ctl: D 0x%08x A 0x%08x,",
	        input_ctx->ctx_input.dwInCtx0, input_ctx->ctx_input.dwInCtx1));
	DPRINTF(("          slot %08x %08x %08x %08x",
	        islot_ctx->dwSctx0, islot_ctx->dwSctx1,
	        islot_ctx->dwSctx2, islot_ctx->dwSctx3));
	DPRINTF(("          ep0  %08x %08x %016lx %08x",
	        ep0_ctx->dwEpCtx0, ep0_ctx->dwEpCtx1, ep0_ctx->qwEpCtx2,
	        ep0_ctx->dwEpCtx4));

	/* when setting address: drop-ctx=0, add-ctx=slot+ep0 */
	if ((input_ctx->ctx_input.dwInCtx0 != 0) ||
	    (input_ctx->ctx_input.dwInCtx1 & 0x03) != 0x03) {
		DPRINTF(("pci_xhci: address device, input ctl invalid"));
		cmderr = XHCI_TRB_ERROR_TRB;
		goto done;
	}

	if (!sc->tablet) {
		/* If not emulating tablet, address the USB device */
		if (slot <= 0 || slot > XHCI_MAX_SLOTS ||
				sc->slot_allocated[slot] == false) {
			DPRINTF(("address device, invalid slot %d", slot));
			cmderr = XHCI_TRB_ERROR_SLOT_NOT_ON;
			goto done;
		}

		dev = sc->slots[slot];
		if (!dev) {
			int index;

			rh_port = XHCI_SCTX_1_RH_PORT_GET(islot_ctx->dwSctx1);
			index = pci_xhci_get_native_port_index_by_vport(sc, rh_port);
			if (index < 0) {
				cmderr = XHCI_TRB_ERROR_TRB;
				DPRINTF(("invalid root hub port %d", rh_port));
				goto done;
			}

			di = &sc->native_ports[index].info;
			DPRINTF(("create virtual device for %d-%s on virtual "
					"port %d", di->path.bus,
					usb_dev_path(&di->path), rh_port));

			dev = pci_xhci_dev_create(sc, di);
			if (!dev) {
				DPRINTF(( "fail to create device for %d-%s",
						di->path.bus,
						usb_dev_path(&di->path)));
				goto done;
			}

			sc->native_ports[index].state = VPORT_EMULATED;
			sc->devices[rh_port] = dev;
			sc->ndevices++;
			sc->slots[slot] = dev;
			dev->hci.hci_address = slot;
		}
	}

	/* assign address to slot */
	dev_ctx = pci_xhci_get_dev_ctx(sc, slot);
	if (!dev_ctx) {
		cmderr = XHCI_TRB_ERROR_CONTEXT_STATE;
		goto done;
	}

	DPRINTF(("pci_xhci: address device, dev ctx"));
	DPRINTF(("          slot %08x %08x %08x %08x",
	        dev_ctx->ctx_slot.dwSctx0, dev_ctx->ctx_slot.dwSctx1,
	        dev_ctx->ctx_slot.dwSctx2, dev_ctx->ctx_slot.dwSctx3));

	dev = XHCI_SLOTDEV_PTR(sc, slot);
	assert(dev != NULL);

	dev->hci.hci_address = slot;
	dev->dev_ctx = dev_ctx;

	if (dev->dev_ue->ue_reset == NULL ||
	    dev->dev_ue->ue_reset(dev->dev_sc) < 0) {
		cmderr = XHCI_TRB_ERROR_ENDP_NOT_ON;
		goto done;
	}

	memcpy(&dev_ctx->ctx_slot, islot_ctx, sizeof(struct xhci_slot_ctx));

	dev_ctx->ctx_slot.dwSctx3 =
	    XHCI_SCTX_3_SLOT_STATE_SET(XHCI_ST_SLCTX_ADDRESSED) |
	    XHCI_SCTX_3_DEV_ADDR_SET(slot);

	memcpy(&dev_ctx->ctx_ep[1], ep0_ctx, sizeof(struct xhci_endp_ctx));
	ep0_ctx = &dev_ctx->ctx_ep[1];
	ep0_ctx->dwEpCtx0 = (ep0_ctx->dwEpCtx0 & ~0x7) |
	    XHCI_EPCTX_0_EPSTATE_SET(XHCI_ST_EPCTX_RUNNING);

	pci_xhci_init_ep(dev, 1, slot);

	dev->dev_slotstate = XHCI_ST_ADDRESSED;

	DPRINTF(("pci_xhci: address device, output ctx"));
	DPRINTF(("          slot %08x %08x %08x %08x",
	        dev_ctx->ctx_slot.dwSctx0, dev_ctx->ctx_slot.dwSctx1,
	        dev_ctx->ctx_slot.dwSctx2, dev_ctx->ctx_slot.dwSctx3));
	DPRINTF(("          ep0  %08x %08x %016lx %08x",
	        ep0_ctx->dwEpCtx0, ep0_ctx->dwEpCtx1, ep0_ctx->qwEpCtx2,
	        ep0_ctx->dwEpCtx4));

done:
	return (cmderr);
}

static uint32_t
pci_xhci_cmd_config_ep(struct pci_xhci_softc *sc, uint32_t slot,
    struct xhci_trb *trb)
{
	struct xhci_input_dev_ctx *input_ctx;
	struct pci_xhci_dev_emu	*dev;
	struct xhci_dev_ctx	*dev_ctx;
	struct xhci_endp_ctx	*ep_ctx, *iep_ctx;
	uint32_t	cmderr;
	int		i;

	cmderr = XHCI_TRB_ERROR_SUCCESS;

	DPRINTF(("pci_xhci config_ep slot %u", slot));

	dev = XHCI_SLOTDEV_PTR(sc, slot);
	assert(dev != NULL);

	if ((trb->dwTrb3 & XHCI_TRB_3_DCEP_BIT) != 0) {
		DPRINTF(("pci_xhci config_ep - deconfigure ep slot %u",
		        slot));
		if (dev->dev_ue->ue_stop != NULL)
			dev->dev_ue->ue_stop(dev->dev_sc);

		dev->dev_slotstate = XHCI_ST_ADDRESSED;

		dev->hci.hci_address = 0;
		dev_ctx = pci_xhci_get_dev_ctx(sc, slot);

		/* number of contexts */
		dev_ctx->ctx_slot.dwSctx0 = FIELD_REPLACE(
		    dev_ctx->ctx_slot.dwSctx0, 1, 0x1F, 27);

		/* slot state */
		dev_ctx->ctx_slot.dwSctx3 = FIELD_REPLACE(
		    dev_ctx->ctx_slot.dwSctx3, XHCI_ST_SLCTX_ADDRESSED,
		    0x1F, 27);

		/* disable endpoints */
		for (i = 2; i < 32; i++)
			pci_xhci_disable_ep(dev, i);

		cmderr = XHCI_TRB_ERROR_SUCCESS;

		goto done;
	}

	if (dev->dev_slotstate < XHCI_ST_ADDRESSED) {
		DPRINTF(("pci_xhci: config_ep slotstate x%x != addressed",
		        dev->dev_slotstate));
		cmderr = XHCI_TRB_ERROR_SLOT_NOT_ON;
		goto done;
	}

	/* In addressed/configured state;
	 * for each drop endpoint ctx flag:
	 *   ep->state = DISABLED
	 * for each add endpoint ctx flag:
	 *   cp(ep-in, ep-out)
	 *   ep->state = RUNNING
	 * for each drop+add endpoint flag:
	 *   reset ep resources
	 *   cp(ep-in, ep-out)
	 *   ep->state = RUNNING
	 * if input->DisabledCtx[2-31] < 30: (at least 1 ep not disabled)
	 *   slot->state = configured
	 */

	input_ctx = XHCI_GADDR(sc, trb->qwTrb0 & ~0xFUL);
	dev_ctx = dev->dev_ctx;
	DPRINTF(("pci_xhci: config_ep inputctx: D:x%08x A:x%08x 7:x%08x",
		input_ctx->ctx_input.dwInCtx0, input_ctx->ctx_input.dwInCtx1,
	        input_ctx->ctx_input.dwInCtx7));

	for (i = 2; i <= 31; i++) {
		ep_ctx = &dev_ctx->ctx_ep[i];

		if (input_ctx->ctx_input.dwInCtx0 &
		    XHCI_INCTX_0_DROP_MASK(i)) {
			DPRINTF((" config ep - dropping ep %d", i));
			pci_xhci_disable_ep(dev, i);
		}

		if (input_ctx->ctx_input.dwInCtx1 &
		    XHCI_INCTX_1_ADD_MASK(i)) {
			iep_ctx = &input_ctx->ctx_ep[i];

			DPRINTF((" enable ep[%d]  %08x %08x %016lx %08x",
			   i, iep_ctx->dwEpCtx0, iep_ctx->dwEpCtx1,
			   iep_ctx->qwEpCtx2, iep_ctx->dwEpCtx4));

			memcpy(ep_ctx, iep_ctx, sizeof(struct xhci_endp_ctx));

			pci_xhci_init_ep(dev, i, slot);

			/* ep state */
			ep_ctx->dwEpCtx0 = FIELD_REPLACE(
			    ep_ctx->dwEpCtx0, XHCI_ST_EPCTX_RUNNING, 0x7, 0);
		}
	}

	/* slot state to configured */
	dev_ctx->ctx_slot.dwSctx3 = FIELD_REPLACE(
	    dev_ctx->ctx_slot.dwSctx3, XHCI_ST_SLCTX_CONFIGURED, 0x1F, 27);
	dev_ctx->ctx_slot.dwSctx0 = FIELD_COPY(
	    dev_ctx->ctx_slot.dwSctx0, input_ctx->ctx_slot.dwSctx0, 0x1F, 27);
	dev->dev_slotstate = XHCI_ST_CONFIGURED;

	DPRINTF(("EP configured; slot %u [0]=0x%08x [1]=0x%08x [2]=0x%08x "
	         "[3]=0x%08x",
	    slot, dev_ctx->ctx_slot.dwSctx0, dev_ctx->ctx_slot.dwSctx1,
	    dev_ctx->ctx_slot.dwSctx2, dev_ctx->ctx_slot.dwSctx3));

done:
	return (cmderr);
}

static uint32_t
pci_xhci_cmd_reset_ep(struct pci_xhci_softc *sc, uint32_t slot,
    struct xhci_trb *trb)
{
	struct pci_xhci_dev_emu	*dev;
	struct pci_xhci_dev_ep *devep;
	struct xhci_dev_ctx	*dev_ctx;
	struct xhci_endp_ctx	*ep_ctx;
	uint32_t	cmderr, epid;
	uint32_t	type;

	epid = XHCI_TRB_3_EP_GET(trb->dwTrb3);

	DPRINTF(("pci_xhci: reset ep %u: slot %u", epid, slot));

	cmderr = XHCI_TRB_ERROR_SUCCESS;

	type = XHCI_TRB_3_TYPE_GET(trb->dwTrb3);

	dev = XHCI_SLOTDEV_PTR(sc, slot);
	assert(dev != NULL);

	if (type == XHCI_TRB_TYPE_STOP_EP &&
	    (trb->dwTrb3 & XHCI_TRB_3_SUSP_EP_BIT) != 0) {
		/* XXX suspend endpoint for 10ms */
	}

	if (epid < 1 || epid > 31) {
		DPRINTF(("pci_xhci: reset ep: invalid epid %u", epid));
		cmderr = XHCI_TRB_ERROR_TRB;
		goto done;
	}

	devep = &dev->eps[epid];
	if (devep->ep_xfer != NULL)
		USB_DATA_XFER_RESET(devep->ep_xfer);

	dev_ctx = dev->dev_ctx;
	assert(dev_ctx != NULL);

	ep_ctx = &dev_ctx->ctx_ep[epid];

	ep_ctx->dwEpCtx0 = (ep_ctx->dwEpCtx0 & ~0x7) | XHCI_ST_EPCTX_STOPPED;

	if (XHCI_EPCTX_0_MAXP_STREAMS_GET(ep_ctx->dwEpCtx0) == 0)
		ep_ctx->qwEpCtx2 = devep->ep_ringaddr | devep->ep_ccs;

	DPRINTF(("pci_xhci: reset ep[%u] %08x %08x %016lx %08x",
	        epid, ep_ctx->dwEpCtx0, ep_ctx->dwEpCtx1, ep_ctx->qwEpCtx2,
	        ep_ctx->dwEpCtx4));

	if (type == XHCI_TRB_TYPE_RESET_EP &&
	    (dev->dev_ue->ue_reset == NULL ||
	    dev->dev_ue->ue_reset(dev->dev_sc) < 0)) {
		cmderr = XHCI_TRB_ERROR_ENDP_NOT_ON;
		goto done;
	}

done:
	return (cmderr);
}


static uint32_t
pci_xhci_find_stream(struct pci_xhci_softc *sc, struct xhci_endp_ctx *ep,
    uint32_t streamid, struct xhci_stream_ctx **osctx)
{
	struct xhci_stream_ctx *sctx;
	uint32_t	maxpstreams;

	maxpstreams = XHCI_EPCTX_0_MAXP_STREAMS_GET(ep->dwEpCtx0);
	if (maxpstreams == 0)
		return (XHCI_TRB_ERROR_TRB);

	if (maxpstreams > XHCI_STREAMS_MAX)
		return (XHCI_TRB_ERROR_INVALID_SID);

	if (XHCI_EPCTX_0_LSA_GET(ep->dwEpCtx0) == 0) {
		DPRINTF(("pci_xhci: find_stream; LSA bit not set"));
		return (XHCI_TRB_ERROR_INVALID_SID);
	}

	/* only support primary stream */
	if (streamid > maxpstreams)
		return (XHCI_TRB_ERROR_STREAM_TYPE);

	sctx = XHCI_GADDR(sc, ep->qwEpCtx2 & ~0xFUL) + streamid;
	if (!XHCI_SCTX_0_SCT_GET(sctx->qwSctx0))
		return (XHCI_TRB_ERROR_STREAM_TYPE);

	*osctx = sctx;

	return (XHCI_TRB_ERROR_SUCCESS);
}


static uint32_t
pci_xhci_cmd_set_tr(struct pci_xhci_softc *sc, uint32_t slot,
    struct xhci_trb *trb)
{
	struct pci_xhci_dev_emu	*dev;
	struct pci_xhci_dev_ep	*devep;
	struct xhci_dev_ctx	*dev_ctx;
	struct xhci_endp_ctx	*ep_ctx;
	uint32_t	cmderr, epid;
	uint32_t	streamid;

	cmderr = XHCI_TRB_ERROR_SUCCESS;

	dev = XHCI_SLOTDEV_PTR(sc, slot);
	assert(dev != NULL);

	DPRINTF(("pci_xhci set_tr: new-tr x%016lx, SCT %u DCS %u",
	         (trb->qwTrb0 & ~0xF),  (uint32_t)((trb->qwTrb0 >> 1) & 0x7),
	         (uint32_t)(trb->qwTrb0 & 0x1)));
	DPRINTF(("                 stream-id %u, slot %u, epid %u, C %u",
		 (trb->dwTrb2 >> 16) & 0xFFFF,
	         XHCI_TRB_3_SLOT_GET(trb->dwTrb3),
	         XHCI_TRB_3_EP_GET(trb->dwTrb3), trb->dwTrb3 & 0x1));

	epid = XHCI_TRB_3_EP_GET(trb->dwTrb3);
	if (epid < 1 || epid > 31) {
		DPRINTF(("pci_xhci: set_tr_deq: invalid epid %u", epid));
		cmderr = XHCI_TRB_ERROR_TRB;
		goto done;
	}

	dev_ctx = dev->dev_ctx;
	assert(dev_ctx != NULL);

	ep_ctx = &dev_ctx->ctx_ep[epid];
	devep = &dev->eps[epid];

	switch (XHCI_EPCTX_0_EPSTATE_GET(ep_ctx->dwEpCtx0)) {
	case XHCI_ST_EPCTX_STOPPED:
	case XHCI_ST_EPCTX_ERROR:
		break;
	default:
		DPRINTF(("pci_xhci cmd set_tr invalid state %x",
		        XHCI_EPCTX_0_EPSTATE_GET(ep_ctx->dwEpCtx0)));
		cmderr = XHCI_TRB_ERROR_CONTEXT_STATE;
		goto done;
	}

	streamid = XHCI_TRB_2_STREAM_GET(trb->dwTrb2);
	if (XHCI_EPCTX_0_MAXP_STREAMS_GET(ep_ctx->dwEpCtx0) > 0) {
		struct xhci_stream_ctx *sctx;

		sctx = NULL;
		cmderr = pci_xhci_find_stream(sc, ep_ctx, streamid, &sctx);
		if (sctx != NULL) {
			assert(devep->ep_sctx != NULL);
			
			devep->ep_sctx[streamid].qwSctx0 = trb->qwTrb0;
			devep->ep_sctx_trbs[streamid].ringaddr =
			    trb->qwTrb0 & ~0xF;
			devep->ep_sctx_trbs[streamid].ccs =
			    XHCI_EPCTX_2_DCS_GET(trb->qwTrb0);
		}
	} else {
		if (streamid != 0) {
			DPRINTF(("pci_xhci cmd set_tr streamid %x != 0",
			        streamid));
		}
		ep_ctx->qwEpCtx2 = trb->qwTrb0 & ~0xFUL;
		devep->ep_ringaddr = ep_ctx->qwEpCtx2 & ~0xFUL;
		devep->ep_ccs = trb->qwTrb0 & 0x1;
		devep->ep_tr = XHCI_GADDR(sc, devep->ep_ringaddr);

		DPRINTF(("pci_xhci set_tr first TRB:"));
		pci_xhci_dump_trb(devep->ep_tr);
	}
	ep_ctx->dwEpCtx0 = (ep_ctx->dwEpCtx0 & ~0x7) | XHCI_ST_EPCTX_STOPPED;

done:
	return (cmderr);
}

static uint32_t
pci_xhci_cmd_eval_ctx(struct pci_xhci_softc *sc, uint32_t slot,
    struct xhci_trb *trb)
{
	struct xhci_input_dev_ctx *input_ctx;
	struct xhci_slot_ctx      *islot_ctx;
	struct xhci_dev_ctx       *dev_ctx;
	struct xhci_endp_ctx      *ep0_ctx;
	uint32_t cmderr;

	input_ctx = XHCI_GADDR(sc, trb->qwTrb0 & ~0xFUL);
	islot_ctx = &input_ctx->ctx_slot;
	ep0_ctx = &input_ctx->ctx_ep[1];

	cmderr = XHCI_TRB_ERROR_SUCCESS;
	DPRINTF(("pci_xhci: eval ctx, input ctl: D 0x%08x A 0x%08x,",
	        input_ctx->ctx_input.dwInCtx0, input_ctx->ctx_input.dwInCtx1));
	DPRINTF(("          slot %08x %08x %08x %08x",
	        islot_ctx->dwSctx0, islot_ctx->dwSctx1,
	        islot_ctx->dwSctx2, islot_ctx->dwSctx3));
	DPRINTF(("          ep0  %08x %08x %016lx %08x",
	        ep0_ctx->dwEpCtx0, ep0_ctx->dwEpCtx1, ep0_ctx->qwEpCtx2,
	        ep0_ctx->dwEpCtx4));

	/* this command expects drop-ctx=0 & add-ctx=slot+ep0 */
	if ((input_ctx->ctx_input.dwInCtx0 != 0) ||
	    (input_ctx->ctx_input.dwInCtx1 & 0x03) == 0) {
		DPRINTF(("pci_xhci: eval ctx, input ctl invalid"));
		cmderr = XHCI_TRB_ERROR_TRB;
		goto done;
	}

	/* assign address to slot; in this emulation, slot_id = address */
	dev_ctx = pci_xhci_get_dev_ctx(sc, slot);

	DPRINTF(("pci_xhci: eval ctx, dev ctx"));
	DPRINTF(("          slot %08x %08x %08x %08x",
	        dev_ctx->ctx_slot.dwSctx0, dev_ctx->ctx_slot.dwSctx1,
	        dev_ctx->ctx_slot.dwSctx2, dev_ctx->ctx_slot.dwSctx3));

	if (input_ctx->ctx_input.dwInCtx1 & 0x01) {	/* slot ctx */
		/* set max exit latency */
		dev_ctx->ctx_slot.dwSctx1 = FIELD_COPY(
		    dev_ctx->ctx_slot.dwSctx1, input_ctx->ctx_slot.dwSctx1,
		    0xFFFF, 0);

		/* set interrupter target */
		dev_ctx->ctx_slot.dwSctx2 = FIELD_COPY(
		    dev_ctx->ctx_slot.dwSctx2, input_ctx->ctx_slot.dwSctx2,
		    0x3FF, 22);
	}
	if (input_ctx->ctx_input.dwInCtx1 & 0x02) {	/* control ctx */
		/* set max packet size */
		dev_ctx->ctx_ep[1].dwEpCtx1 = FIELD_COPY(
		    dev_ctx->ctx_ep[1].dwEpCtx1, ep0_ctx->dwEpCtx1,
		    0xFFFF, 16);

		ep0_ctx = &dev_ctx->ctx_ep[1];
	}

	DPRINTF(("pci_xhci: eval ctx, output ctx"));
	DPRINTF(("          slot %08x %08x %08x %08x",
	        dev_ctx->ctx_slot.dwSctx0, dev_ctx->ctx_slot.dwSctx1,
	        dev_ctx->ctx_slot.dwSctx2, dev_ctx->ctx_slot.dwSctx3));
	DPRINTF(("          ep0  %08x %08x %016lx %08x",
	        ep0_ctx->dwEpCtx0, ep0_ctx->dwEpCtx1, ep0_ctx->qwEpCtx2,
	        ep0_ctx->dwEpCtx4));

done:
	return (cmderr);
}

static int
pci_xhci_complete_commands(struct pci_xhci_softc *sc)
{
	struct xhci_trb	evtrb;
	struct xhci_trb	*trb;
	uint64_t	crcr;
	uint32_t	ccs;		/* cycle state (XHCI 4.9.2) */
	uint32_t	type;
	uint32_t	slot;
	uint32_t	cmderr;
	int		error;

	error = 0;
	sc->opregs.crcr |= XHCI_CRCR_LO_CRR;

	trb = sc->opregs.cr_p;
	ccs = sc->opregs.crcr & XHCI_CRCR_LO_RCS;
	crcr = sc->opregs.crcr & ~0xF;

	while (1) {
		sc->opregs.cr_p = trb;
	
		type = XHCI_TRB_3_TYPE_GET(trb->dwTrb3);

		if ((trb->dwTrb3 & XHCI_TRB_3_CYCLE_BIT) !=
		    (ccs & XHCI_TRB_3_CYCLE_BIT))
			break;

		DPRINTF(("pci_xhci: cmd type 0x%x, Trb0 x%016lx dwTrb2 x%08x"
		        " dwTrb3 x%08x, TRB_CYCLE %u/ccs %u",
		        type, trb->qwTrb0, trb->dwTrb2, trb->dwTrb3,
		        trb->dwTrb3 & XHCI_TRB_3_CYCLE_BIT, ccs));

		cmderr = XHCI_TRB_ERROR_SUCCESS;
		evtrb.dwTrb2 = 0;
		evtrb.dwTrb3 = (ccs & XHCI_TRB_3_CYCLE_BIT) |
		      XHCI_TRB_3_TYPE_SET(XHCI_TRB_EVENT_CMD_COMPLETE);
		slot = 0;

		switch (type) {
		case XHCI_TRB_TYPE_LINK:			/* 0x06 */
			if (trb->dwTrb3 & XHCI_TRB_3_TC_BIT)
				ccs ^= XHCI_CRCR_LO_RCS;
			break;

		case XHCI_TRB_TYPE_ENABLE_SLOT:			/* 0x09 */
			cmderr = pci_xhci_cmd_enable_slot(sc, &slot);
			break;

		case XHCI_TRB_TYPE_DISABLE_SLOT:		/* 0x0A */
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_disable_slot(sc, slot);
			break;

		case XHCI_TRB_TYPE_ADDRESS_DEVICE:		/* 0x0B */
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_address_device(sc, slot, trb);
			break;

		case XHCI_TRB_TYPE_CONFIGURE_EP:		/* 0x0C */
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_config_ep(sc, slot, trb);
			break;

		case XHCI_TRB_TYPE_EVALUATE_CTX:		/* 0x0D */
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_eval_ctx(sc, slot, trb);
			break;

		case XHCI_TRB_TYPE_RESET_EP:			/* 0x0E */
			DPRINTF(("Reset Endpoint on slot %d", slot));
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_reset_ep(sc, slot, trb);
			break;

		case XHCI_TRB_TYPE_STOP_EP:			/* 0x0F */
			DPRINTF(("Stop Endpoint on slot %d", slot));
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_reset_ep(sc, slot, trb);
			break;

		case XHCI_TRB_TYPE_SET_TR_DEQUEUE:		/* 0x10 */
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_set_tr(sc, slot, trb);
			break;

		case XHCI_TRB_TYPE_RESET_DEVICE:		/* 0x11 */
			slot = XHCI_TRB_3_SLOT_GET(trb->dwTrb3);
			cmderr = pci_xhci_cmd_reset_device(sc, slot);
			break;

		case XHCI_TRB_TYPE_FORCE_EVENT:			/* 0x12 */
			/* TODO: */
			break;

		case XHCI_TRB_TYPE_NEGOTIATE_BW:		/* 0x13 */
			break;

		case XHCI_TRB_TYPE_SET_LATENCY_TOL:		/* 0x14 */
			break;

		case XHCI_TRB_TYPE_GET_PORT_BW:			/* 0x15 */
			break;

		case XHCI_TRB_TYPE_FORCE_HEADER:		/* 0x16 */
			break;

		case XHCI_TRB_TYPE_NOOP_CMD:			/* 0x17 */
			break;

		default:
			DPRINTF(("pci_xhci: unsupported cmd %x", type));
			break;
		}

		if (type != XHCI_TRB_TYPE_LINK) {
			/* 
			 * insert command completion event and assert intr
			 */
			evtrb.qwTrb0 = crcr;
			evtrb.dwTrb2 |= XHCI_TRB_2_ERROR_SET(cmderr);
			evtrb.dwTrb3 |= XHCI_TRB_3_SLOT_SET(slot);
			DPRINTF(("pci_xhci: command 0x%x result: 0x%x",
			        type, cmderr));
			pci_xhci_insert_event(sc, &evtrb, 1);
		}

		trb = pci_xhci_trb_next(sc, trb, &crcr);
	}

	sc->opregs.crcr = crcr | (sc->opregs.crcr & XHCI_CRCR_LO_CA) | ccs;
	sc->opregs.crcr &= ~XHCI_CRCR_LO_CRR;
	return (error);
}

static void
pci_xhci_dump_trb(struct xhci_trb *trb)
{
	static const char *trbtypes[] = {
		"RESERVED",
		"NORMAL",
		"SETUP_STAGE",
		"DATA_STAGE",
		"STATUS_STAGE",
		"ISOCH",
		"LINK",
		"EVENT_DATA",
		"NOOP",
		"ENABLE_SLOT",
		"DISABLE_SLOT",
		"ADDRESS_DEVICE",
		"CONFIGURE_EP",
		"EVALUATE_CTX",
		"RESET_EP",
		"STOP_EP",
		"SET_TR_DEQUEUE",
		"RESET_DEVICE",
		"FORCE_EVENT",
		"NEGOTIATE_BW",
		"SET_LATENCY_TOL",
		"GET_PORT_BW",
		"FORCE_HEADER",
		"NOOP_CMD"
	};
	uint32_t type;

	type = XHCI_TRB_3_TYPE_GET(trb->dwTrb3);
	DPRINTF(("pci_xhci: trb[@%p] type x%02x %s 0:x%016lx 2:x%08x 3:x%08x",
	         trb, type,
	         type <= XHCI_TRB_TYPE_NOOP_CMD ? trbtypes[type] : "INVALID",
	         trb->qwTrb0, trb->dwTrb2, trb->dwTrb3));
}

static int
pci_xhci_xfer_complete(struct pci_xhci_softc *xdev, struct usb_data_xfer *xfer,
     uint32_t slot, uint32_t epid, int *do_intr)
{
	struct pci_xhci_dev_emu *dev;
	struct pci_xhci_dev_ep	*devep;
	struct xhci_dev_ctx	*dev_ctx;
	struct xhci_endp_ctx	*ep_ctx;
	struct xhci_trb		*trb;
	struct xhci_trb		evtrb;
	uint32_t trbflags;
	uint32_t edtla;
	int i, err;
	int rem_len = 0;

	devep = &dev->eps[epid];
	dev_ctx = pci_xhci_get_dev_ctx(xdev, slot);

	assert(dev_ctx != NULL);

	ep_ctx = &dev_ctx->ctx_ep[epid];
	err = XHCI_TRB_ERROR_SUCCESS;

	/* err is used as completion code and sent to guest driver */
	switch (xfer->status) {
	case USB_ERR_STALLED:
		ep_ctx->dwEpCtx0 = (ep_ctx->dwEpCtx0 & ~0x7) |
			XHCI_ST_EPCTX_HALTED;
		err = XHCI_TRB_ERROR_STALL;
		break;
	case USB_ERR_SHORT_XFER:
		err = XHCI_TRB_ERROR_SHORT_PKT;
		break;
	case USB_ERR_TIMEOUT:
	case USB_ERR_IOERROR:
		err = XHCI_TRB_ERROR_XACT;
		break;
	case USB_ERR_BAD_BUFSIZE:
		err = XHCI_TRB_ERROR_BABBLE;
		break;
	case USB_ERR_NORMAL_COMPLETION:
		break;
	default:
		DPRINTF(( "unknown error %d", xfer->status));
	}

	*do_intr = 0;
	edtla = 0;

	/* go through list of TRBs and insert event(s) */
	for (i = (uint64_t)xfer->head; xfer->ndata > 0; ) {
		evtrb.qwTrb0 = xfer->data[i].trb_addr;
		trb = XHCI_GADDR(xdev, evtrb.qwTrb0);
		trbflags = trb->dwTrb3;

		DPRINTF(("xfer[%d] done?%u:%d trb %x %016lx %x "
			"(err %d) IOC?%d, type %d",
			i, xfer->data[i].stat, xfer->data[i].blen,
			XHCI_TRB_3_TYPE_GET(trbflags), evtrb.qwTrb0, trbflags,
			err, trb->dwTrb3 & XHCI_TRB_3_IOC_BIT ? 1 : 0,
			xfer->data[i].type));

		if (xfer->data[i].stat < USB_BLOCK_HANDLED) {
			xfer->head = (int)i;
			break;
		}

		xfer->data[i].stat = USB_BLOCK_FREE;
		xfer->ndata--;
		xfer->head = index_inc(xfer->head, xfer->max_blk_cnt);
		edtla += xfer->data[i].bdone;

		trb->dwTrb3 = (trb->dwTrb3 & ~0x1) | (xfer->data[i].ccs);

		if (xfer->data[i].type == USB_DATA_PART) {
			rem_len += xfer->data[i].blen;
			i = index_inc(i, xfer->max_blk_cnt);

			/* This 'continue' will delay the IOC behavior which
			 * could decrease the number of virtual interrupts.
			 * This could GREATLY improve the performance especially
			 * under ISOCH scenario.
			 */
			continue;
		} else
			rem_len += xfer->data[i].blen;

		if (err == XHCI_TRB_ERROR_SUCCESS && rem_len > 0)
			err = XHCI_TRB_ERROR_SHORT_PKT;

		/* Only interrupt if IOC or short packet */
		if (!(trb->dwTrb3 & XHCI_TRB_3_IOC_BIT) &&
		    !((err == XHCI_TRB_ERROR_SHORT_PKT) &&
		      (trb->dwTrb3 & XHCI_TRB_3_ISP_BIT))) {

			i = index_inc(i, xfer->max_blk_cnt);
			continue;
		}

		evtrb.dwTrb2 = XHCI_TRB_2_ERROR_SET(err) |
		               XHCI_TRB_2_REM_SET(rem_len);

		evtrb.dwTrb3 = XHCI_TRB_3_TYPE_SET(XHCI_TRB_EVENT_TRANSFER) |
		    XHCI_TRB_3_SLOT_SET(slot) | XHCI_TRB_3_EP_SET(epid);

		if (XHCI_TRB_3_TYPE_GET(trbflags) == XHCI_TRB_TYPE_EVENT_DATA) {
			DPRINTF(("pci_xhci EVENT_DATA edtla %u", edtla));
			evtrb.qwTrb0 = trb->qwTrb0;
			evtrb.dwTrb2 = (edtla & 0xFFFFF) | 
			         XHCI_TRB_2_ERROR_SET(err);
			evtrb.dwTrb3 |= XHCI_TRB_3_ED_BIT;
			edtla = 0;
		}

		*do_intr = 1;

		err = pci_xhci_insert_event(xdev, &evtrb, 0);
		if (err != XHCI_TRB_ERROR_SUCCESS) {
			break;
		}

		i = index_inc(i, xfer->max_blk_cnt);
		rem_len = 0;
	}

	return (err);
}

static void
pci_xhci_update_ep_ring(struct pci_xhci_softc *sc, struct pci_xhci_dev_emu *dev,
    struct pci_xhci_dev_ep *devep, struct xhci_endp_ctx *ep_ctx,
    uint32_t streamid, uint64_t ringaddr, int ccs)
{

	if (XHCI_EPCTX_0_MAXP_STREAMS_GET(ep_ctx->dwEpCtx0) != 0) {
		devep->ep_sctx[streamid].qwSctx0 = (ringaddr & ~0xFUL) |
		                                   (ccs & 0x1);

		devep->ep_sctx_trbs[streamid].ringaddr = ringaddr & ~0xFUL;
		devep->ep_sctx_trbs[streamid].ccs = ccs & 0x1;
		ep_ctx->qwEpCtx2 = (ep_ctx->qwEpCtx2 & ~0x1) | (ccs & 0x1);

		DPRINTF(("xhci update ep-ring stream %d, addr %lx",
		    streamid, devep->ep_sctx[streamid].qwSctx0));
	} else {
		devep->ep_ringaddr = ringaddr & ~0xFUL;
		devep->ep_ccs = ccs & 0x1;
		devep->ep_tr = XHCI_GADDR(sc, ringaddr & ~0xFUL);
		ep_ctx->qwEpCtx2 = (ringaddr & ~0xFUL) | (ccs & 0x1);

		DPRINTF(("xhci update ep-ring, addr %lx",
		    (devep->ep_ringaddr | devep->ep_ccs)));
	}
}

/*
 * Outstanding transfer still in progress (device NAK'd earlier) so retry
 * the transfer again to see if it succeeds.
 */
static int
pci_xhci_try_usb_xfer(struct pci_xhci_softc *sc,
    struct pci_xhci_dev_emu *dev, struct pci_xhci_dev_ep *devep,
    struct xhci_endp_ctx *ep_ctx, uint32_t slot, uint32_t epid)
{
	struct usb_data_xfer *xfer;
	int		err;
	int		do_intr;

	ep_ctx->dwEpCtx0 = FIELD_REPLACE(
		    ep_ctx->dwEpCtx0, XHCI_ST_EPCTX_RUNNING, 0x7, 0);

	err = 0;
	do_intr = 0;

	xfer = devep->ep_xfer;
	USB_DATA_XFER_LOCK(xfer);

	/* outstanding requests queued up */
	if (dev->dev_ue->ue_data != NULL) {
		err = dev->dev_ue->ue_data(dev->dev_sc, xfer,
		            epid & 0x1 ? USB_XFER_IN : USB_XFER_OUT, epid/2);
		if (err == USB_ERR_CANCELLED) {
			if (USB_DATA_GET_ERRCODE(&xfer->data[xfer->head]) ==
			    USB_NAK)
				err = XHCI_TRB_ERROR_SUCCESS;
		} else {
			err = pci_xhci_xfer_complete(sc, xfer, slot, epid,
			                             &do_intr);
			if (err == XHCI_TRB_ERROR_SUCCESS && do_intr) {
				pci_xhci_assert_interrupt(sc);
			}


			/* XXX should not do it if error? */
			USB_DATA_XFER_RESET(xfer);
		}
	}

	USB_DATA_XFER_UNLOCK(xfer);


	return (err);
}


static int
pci_xhci_handle_transfer(struct pci_xhci_softc *sc,
    struct pci_xhci_dev_emu *dev, struct pci_xhci_dev_ep *devep,
    struct xhci_endp_ctx *ep_ctx, struct xhci_trb *trb, uint32_t slot,
    uint32_t epid, uint64_t addr, uint32_t ccs, uint32_t streamid)
{
	struct xhci_trb *setup_trb;
	struct usb_data_xfer *xfer;
	struct usb_data_xfer_block *xfer_block;
	struct usb_data_xfer_block *prev_block;
	uint64_t	val;
	uint32_t	trbflags;
	int		do_intr, err;
	int		do_retry;

	ep_ctx->dwEpCtx0 = FIELD_REPLACE(ep_ctx->dwEpCtx0,
	                                 XHCI_ST_EPCTX_RUNNING, 0x7, 0);

	xfer = devep->ep_xfer;
	USB_DATA_XFER_LOCK(xfer);

	DPRINTF(("pci_xhci handle_transfer slot %u", slot));

retry:
	err = 0;
	do_retry = 0;
	do_intr = 0;
	setup_trb = NULL;
	prev_block = NULL;

	while (1) {
		pci_xhci_dump_trb(trb);

		trbflags = trb->dwTrb3;

		if (XHCI_TRB_3_TYPE_GET(trbflags) != XHCI_TRB_TYPE_LINK &&
		    (trbflags & XHCI_TRB_3_CYCLE_BIT) !=
		    (ccs & XHCI_TRB_3_CYCLE_BIT)) {
			DPRINTF(("Cycle-bit changed trbflags %x, ccs %x",
			    trbflags & XHCI_TRB_3_CYCLE_BIT, ccs));
			break;
		}

		xfer_block = NULL;

		switch (XHCI_TRB_3_TYPE_GET(trbflags)) {
		case XHCI_TRB_TYPE_LINK:
			if (trb->dwTrb3 & XHCI_TRB_3_TC_BIT)
				ccs ^= 0x1;

			xfer_block = usb_block_append(xfer, NULL, 0, ccs, addr, streamid);
			if (!xfer_block) {
				err = XHCI_TRB_ERROR_STALL;
				goto errout;
			}

			xfer_block->stat = USB_BLOCK_FREE;
			break;

		case XHCI_TRB_TYPE_SETUP_STAGE:
			if ((trbflags & XHCI_TRB_3_IDT_BIT) == 0 ||
			    XHCI_TRB_2_BYTES_GET(trb->dwTrb2) != 8) {
				DPRINTF(("pci_xhci: invalid setup trb"));
				err = XHCI_TRB_ERROR_TRB;
				goto errout;
			}
			setup_trb = trb;
			val = trb->qwTrb0;
			if (!xfer->ureq)
				xfer->ureq = malloc(
				           sizeof(struct usb_device_request));
			if (!xfer->ureq) {
				err = XHCI_TRB_ERROR_STALL;
				goto errout;
			}

			memcpy(xfer->ureq, &val,
			       sizeof(struct usb_device_request));

			xfer_block = usb_block_append(xfer, NULL, 0, ccs, addr, streamid);

			if (!xfer_block) {
				free(xfer->ureq);
				xfer->ureq = NULL;
				err = XHCI_TRB_ERROR_STALL;
				goto errout;
			}
			xfer_block->stat = USB_BLOCK_HANDLED;
			break;

		case XHCI_TRB_TYPE_NORMAL:
			if (setup_trb != NULL) {
				fprintf(stderr, "trb not supposed to be in ctl scope\r\n");
				err = XHCI_TRB_ERROR_TRB;
				goto errout;
			}
		case XHCI_TRB_TYPE_ISOCH:
			if (setup_trb != NULL) {
				DPRINTF(("pci_xhci: trb not supposed to be in "
				         "ctl scope"));
				err = XHCI_TRB_ERROR_TRB;
				goto errout;
			}
			/* fall through */

		case XHCI_TRB_TYPE_DATA_STAGE:
			xfer_block = usb_block_append(xfer,
					(void *)(trbflags & XHCI_TRB_3_IDT_BIT ?
					&trb->qwTrb0 :
					XHCI_GADDR(sc, trb->qwTrb0)),
					trb->dwTrb2 & 0x1FFFF, ccs,
					addr, streamid);

			if (!xfer_block) {
				err = XHCI_TRB_ERROR_STALL;
				goto errout;
			}

			if (trb->dwTrb3 & XHCI_TRB_3_CHAIN_BIT)
				xfer_block->type = USB_DATA_PART;
			else
				xfer_block->type = USB_DATA_FULL;

			prev_block = xfer_block;
			break;

		case XHCI_TRB_TYPE_STATUS_STAGE:
			xfer_block = usb_block_append(xfer, NULL, 0, ccs, addr, streamid);
			break;

		case XHCI_TRB_TYPE_NOOP:
			if (!xfer_block) {
				err = XHCI_TRB_ERROR_STALL;
				goto errout;
			}
			xfer_block->stat = USB_BLOCK_HANDLED;
			break;

		case XHCI_TRB_TYPE_EVENT_DATA:
			if (!xfer_block) {
				err = XHCI_TRB_ERROR_TRB;
				goto errout;
			}
			if ((epid > 1) && (trbflags & XHCI_TRB_3_IOC_BIT)) {
				xfer_block->stat = USB_BLOCK_HANDLED;
			}
			if (prev_block && prev_block->type == USB_DATA_PART) {
				prev_block->type = USB_DATA_FULL;
				prev_block = NULL;
			}
			break;

		default:
			DPRINTF(("pci_xhci: handle xfer unexpected trb type "
			         "0x%x",
			         XHCI_TRB_3_TYPE_GET(trbflags)));
			err = XHCI_TRB_ERROR_TRB;
			goto errout;
		}

		trb = pci_xhci_trb_next(sc, trb, &addr);

		DPRINTF(("pci_xhci: next trb: 0x%lx", (uint64_t)trb));

		if (xfer_block) {
			pci_xhci_update_ep_ring(sc, dev, devep, ep_ctx,
					streamid, addr, ccs);
		}

		if (trbflags & XHCI_TRB_3_BEI_BIT)
			continue;

		/* win10 needs it */
		if (XHCI_TRB_3_TYPE_GET(trbflags) == XHCI_TRB_TYPE_EVENT_DATA)
			continue;

		if (!setup_trb && !(trbflags & XHCI_TRB_3_CHAIN_BIT) &&
		    XHCI_TRB_3_TYPE_GET(trbflags) != XHCI_TRB_TYPE_LINK) {
			break;
		}

		/* handle current batch that requires interrupt on complete */
		if (trbflags & XHCI_TRB_3_IOC_BIT) {
			DPRINTF(("pci_xhci: trb IOC bit set"));
			if (epid == 1)
				do_retry = 1;
			break;
		}
	}

	DPRINTF(("pci_xhci[%d]: xfer->ndata %u", __LINE__, xfer->ndata));

	if (xfer->ndata <= 0)
		goto errout;

	if (epid == 1) {
		err = USB_ERR_NOT_STARTED;
		if (dev->dev_ue->ue_request != NULL)
			err = dev->dev_ue->ue_request(dev->dev_sc, xfer);
		setup_trb = NULL;
	} else {
		/* handle data transfer */
		pci_xhci_try_usb_xfer(sc, dev, devep, ep_ctx, slot, epid);
		err = XHCI_TRB_ERROR_SUCCESS;
		goto errout;
	}

	err = USB_TO_XHCI_ERR(err);
	if ((err == XHCI_TRB_ERROR_SUCCESS) ||
	    (err == XHCI_TRB_ERROR_SHORT_PKT)) {
		err = pci_xhci_xfer_complete(sc, xfer, slot, epid, &do_intr);
		if (err != XHCI_TRB_ERROR_SUCCESS)
			do_retry = 0;
	}

errout:
	if (err == XHCI_TRB_ERROR_EV_RING_FULL)
		DPRINTF(("pci_xhci[%d]: event ring full", __LINE__));

	if (!do_retry)
		USB_DATA_XFER_UNLOCK(xfer);

	if (do_intr)
		pci_xhci_assert_interrupt(sc);

	if (do_retry) {
		USB_DATA_XFER_RESET(xfer);
		DPRINTF(("pci_xhci[%d]: retry:continuing with next TRBs",
		         __LINE__));
		goto retry;
	}

	if (epid == 1)
		USB_DATA_XFER_RESET(xfer);

	return (err);
}

static void
pci_xhci_device_doorbell(struct pci_xhci_softc *sc, uint32_t slot,
    uint32_t epid, uint32_t streamid)
{
	struct pci_xhci_dev_emu *dev;
	struct pci_xhci_dev_ep	*devep;
	struct xhci_dev_ctx	*dev_ctx;
	struct xhci_endp_ctx	*ep_ctx;
	struct pci_xhci_trb_ring *sctx_tr;
	struct xhci_trb	*trb;
	uint64_t	ringaddr;
	uint32_t	ccs;

	DPRINTF(("pci_xhci doorbell slot %u epid %u stream %u",
	    slot, epid, streamid));

	if ((sc->tablet && (slot == 0 || slot > sc->ndevices)) ||
		(!sc->tablet && (slot == 0 || slot > sc->ndevices ||
			!sc->slot_allocated[slot]))) {
		DPRINTF(("pci_xhci: invalid doorbell slot %u", slot));
		return;
	}

	if (epid == 0 || epid >= XHCI_MAX_ENDPOINTS) {
		DPRINTF(("pci_xhci: invalid endpoint %u", epid));
		return;
	}

	dev = XHCI_SLOTDEV_PTR(sc, slot);
	devep = &dev->eps[epid];
	dev_ctx = pci_xhci_get_dev_ctx(sc, slot);
	if (!dev_ctx) {
		return;
	}
	ep_ctx = &dev_ctx->ctx_ep[epid];

	sctx_tr = NULL;

	DPRINTF(("pci_xhci: device doorbell ep[%u] %08x %08x %016lx %08x",
	        epid, ep_ctx->dwEpCtx0, ep_ctx->dwEpCtx1, ep_ctx->qwEpCtx2,
	        ep_ctx->dwEpCtx4));

	if (ep_ctx->qwEpCtx2 == 0)
		return;


	/*
	 * In USB emulation with port mapping, the following transfer should
	 * NOT be called, or else the interrupt transfer will result
	 * of invalid and infinite loop. It is used by usb_mouse.c only.
	 */
	/* handle pending transfers */
	if (dev->dev_ue && dev->dev_ue->ue_devtype == USB_DEV_STATIC && devep->ep_xfer->ndata > 0) {
		pci_xhci_try_usb_xfer(sc, dev, devep, ep_ctx, slot, epid);
		return;
	}

	/* get next trb work item */
	if (XHCI_EPCTX_0_MAXP_STREAMS_GET(ep_ctx->dwEpCtx0) != 0) {
		/*
		 * Stream IDs of 0, 65535 (any stream), and 65534
		 * (prime) are invalid.
		 */
		sctx_tr = &devep->ep_sctx_trbs[streamid];
		ringaddr = sctx_tr->ringaddr;
		ccs = sctx_tr->ccs;
		trb = XHCI_GADDR(sc, sctx_tr->ringaddr & ~0xFUL);
		DPRINTF(("doorbell, stream %u, ccs %lx, trb ccs %x",
		        streamid, ep_ctx->qwEpCtx2 & XHCI_TRB_3_CYCLE_BIT,
		        trb->dwTrb3 & XHCI_TRB_3_CYCLE_BIT));
	} else {
		if (streamid != 0) {
			DPRINTF(("pci_xhci: invalid stream %u", streamid));
			return;
		}
		ringaddr = devep->ep_ringaddr;
		ccs = devep->ep_ccs;
		trb = devep->ep_tr;
		DPRINTF(("doorbell, ccs %lx, trb ccs %x",
		        ep_ctx->qwEpCtx2 & XHCI_TRB_3_CYCLE_BIT,
		        trb->dwTrb3 & XHCI_TRB_3_CYCLE_BIT));
	}

	if (XHCI_TRB_3_TYPE_GET(trb->dwTrb3) == 0) {
		DPRINTF(("pci_xhci: ring %lx trb[%lx] EP %u is RESERVED?",
		        ep_ctx->qwEpCtx2, devep->ep_ringaddr, epid));
		return;
	}

	pci_xhci_handle_transfer(sc, dev, devep, ep_ctx, trb, slot, epid,
	                         ringaddr, ccs, streamid);
}

static void
pci_xhci_dbregs_write(struct pci_xhci_softc *sc, uint64_t offset,
    uint64_t value)
{

	offset = (offset - sc->dboff) / sizeof(uint32_t);

	DPRINTF(("pci_xhci: doorbell write offset 0x%lx: 0x%lx",
	        offset, value));

	if (XHCI_HALTED(sc)) {
		DPRINTF(("pci_xhci: controller halted"));
		return;
	}

	if (offset == 0)
		pci_xhci_complete_commands(sc);
	else if (sc->portregs != NULL)
		pci_xhci_device_doorbell(sc, offset,
		   XHCI_DB_TARGET_GET(value), XHCI_DB_SID_GET(value));
}

static void
pci_xhci_rtsregs_write(struct pci_xhci_softc *sc, uint64_t offset,
    uint64_t value)
{
	struct pci_xhci_rtsregs *rts;

	offset -= sc->rtsoff;

	if (offset == 0) {
		DPRINTF(("pci_xhci attempted write to MFINDEX"));
		return;
	}

	DPRINTF(("pci_xhci: runtime regs write offset 0x%lx: 0x%lx",
	        offset, value));

	offset -= 0x20;		/* start of intrreg */

	rts = &sc->rtsregs;

	switch (offset) {
	case 0x00:
		if (value & XHCI_IMAN_INTR_PEND)
			rts->intrreg.iman &= ~XHCI_IMAN_INTR_PEND;
		rts->intrreg.iman = (value & XHCI_IMAN_INTR_ENA) |
		                    (rts->intrreg.iman & XHCI_IMAN_INTR_PEND);

		if (!(value & XHCI_IMAN_INTR_ENA))
			pci_xhci_deassert_interrupt(sc);

		break;

	case 0x04:
		rts->intrreg.imod = value;
		break;

	case 0x08:
		rts->intrreg.erstsz = value & 0xFFFF;
		break;

	case 0x10:
		/* ERSTBA low bits */
		rts->intrreg.erstba = MASK_64_HI(sc->rtsregs.intrreg.erstba) |
		                      (value & ~0x3F);
		break;

	case 0x14:
		/* ERSTBA high bits */
		rts->intrreg.erstba = (value << 32) |
		    MASK_64_LO(sc->rtsregs.intrreg.erstba);

		rts->erstba_p = XHCI_GADDR(sc,
		                        sc->rtsregs.intrreg.erstba & ~0x3FUL);

		rts->erst_p = XHCI_GADDR(sc,
		              sc->rtsregs.erstba_p->qwEvrsTablePtr & ~0x3FUL);

		rts->er_enq_idx = 0;
		rts->er_events_cnt = 0;

		DPRINTF(("pci_xhci: wr erstba erst (%p) ptr 0x%lx, sz %u",
		        rts->erstba_p,
		        rts->erstba_p->qwEvrsTablePtr,
		        rts->erstba_p->dwEvrsTableSize));
		break;

	case 0x18:
		/* ERDP low bits */
		rts->intrreg.erdp =
		    MASK_64_HI(sc->rtsregs.intrreg.erdp) |
		    (rts->intrreg.erdp & XHCI_ERDP_LO_BUSY) |
		    (value & ~0xF);
		if (value & XHCI_ERDP_LO_BUSY) {
			rts->intrreg.erdp &= ~XHCI_ERDP_LO_BUSY;
			rts->intrreg.iman &= ~XHCI_IMAN_INTR_PEND;
		}

		rts->er_deq_seg = XHCI_ERDP_LO_SINDEX(value);

		break;

	case 0x1C:
		/* ERDP high bits */
		rts->intrreg.erdp = (value << 32) |
		    MASK_64_LO(sc->rtsregs.intrreg.erdp);

		if (rts->er_events_cnt > 0) {
			uint64_t erdp;
			uint32_t erdp_i;

			erdp = rts->intrreg.erdp & ~0xF;
			erdp_i = (erdp - rts->erstba_p->qwEvrsTablePtr) /
			           sizeof(struct xhci_trb);

			if (erdp_i <= rts->er_enq_idx)
				rts->er_events_cnt = rts->er_enq_idx - erdp_i;
			else
				rts->er_events_cnt =
				          rts->erstba_p->dwEvrsTableSize -
				          (erdp_i - rts->er_enq_idx);

			DPRINTF(("pci_xhci: erdp 0x%lx, events cnt %u",
			        erdp, rts->er_events_cnt));
		}

		break;

	default:
		DPRINTF(("pci_xhci attempted write to RTS offset 0x%lx",
		        offset));
		break;
	}
}

static uint64_t
pci_xhci_portregs_read(struct pci_xhci_softc *sc, uint64_t offset)
{
	int port;
	uint32_t *p;

	if (sc->portregs == NULL)
		return (0);

	port = (offset - 0x3F0) / 0x10;

	if (port > XHCI_MAX_DEVS) {
		DPRINTF(("pci_xhci: portregs_read port %d >= XHCI_MAX_DEVS",
		    port));

		/* return default value for unused port */
		return (XHCI_PS_SPEED_SET(3));
	}

	offset = (offset - 0x3F0) % 0x10;

	p = &sc->portregs[port].portsc;
	p += offset / sizeof(uint32_t);

	DPRINTF(("pci_xhci: portregs read offset 0x%lx port %u -> 0x%x",
	        offset, port, *p));

	return (*p);
}

static void
pci_xhci_hostop_write(struct pci_xhci_softc *sc, uint64_t offset,
    uint64_t value)
{
	offset -= XHCI_CAPLEN;

	if (offset < 0x400)
		DPRINTF(("pci_xhci: hostop write offset 0x%lx: 0x%lx",
		         offset, value));

	switch (offset) {
	case XHCI_USBCMD:
		sc->opregs.usbcmd = pci_xhci_usbcmd_write(sc, value & 0x3F0F);
		break;

	case XHCI_USBSTS:
		/* clear bits on write */
		sc->opregs.usbsts &= ~(value &
		      (XHCI_STS_HSE|XHCI_STS_EINT|XHCI_STS_PCD|XHCI_STS_SSS|
		       XHCI_STS_RSS|XHCI_STS_SRE|XHCI_STS_CNR));
		break;

	case XHCI_PAGESIZE:
		/* read only */
		break;

	case XHCI_DNCTRL:
		sc->opregs.dnctrl = value & 0xFFFF;
		break;

	case XHCI_CRCR_LO:
		if (sc->opregs.crcr & XHCI_CRCR_LO_CRR) {
			sc->opregs.crcr &= ~(XHCI_CRCR_LO_CS|XHCI_CRCR_LO_CA);
			sc->opregs.crcr |= value &
			                   (XHCI_CRCR_LO_CS|XHCI_CRCR_LO_CA);
		} else {
			sc->opregs.crcr = MASK_64_HI(sc->opregs.crcr) |
			           (value & (0xFFFFFFC0 | XHCI_CRCR_LO_RCS));
		}
		break;

	case XHCI_CRCR_HI:
		if (!(sc->opregs.crcr & XHCI_CRCR_LO_CRR)) {
			sc->opregs.crcr = MASK_64_LO(sc->opregs.crcr) |
			                  (value << 32);

			sc->opregs.cr_p = XHCI_GADDR(sc,
			                  sc->opregs.crcr & ~0xF);
		}

		if (sc->opregs.crcr & XHCI_CRCR_LO_CS) {
			/* Stop operation of Command Ring */
		}

		if (sc->opregs.crcr & XHCI_CRCR_LO_CA) {
			/* Abort command */
		}

		break;

	case XHCI_DCBAAP_LO:
		sc->opregs.dcbaap = MASK_64_HI(sc->opregs.dcbaap) |
		                    (value & 0xFFFFFFC0);
		break;

	case XHCI_DCBAAP_HI:
		sc->opregs.dcbaap =  MASK_64_LO(sc->opregs.dcbaap) |
		                     (value << 32);
		sc->opregs.dcbaa_p = XHCI_GADDR(sc, sc->opregs.dcbaap & ~0x3FUL);

		DPRINTF(("pci_xhci: opregs dcbaap = 0x%lx (vaddr 0x%lx)",
		    sc->opregs.dcbaap, (uint64_t)sc->opregs.dcbaa_p));
		break;

	case XHCI_CONFIG:
		sc->opregs.config = value & 0x03FF;
		break;

	default:
		if (offset >= 0x400)
			pci_xhci_portregs_write(sc, offset, value);

		break;
	}
}


static void
pci_xhci_write(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
                int baridx, uint64_t offset, int size, uint64_t value)
{
	struct pci_xhci_softc *sc;

	sc = pi->pi_arg;

	assert(baridx == 0);


	pthread_mutex_lock(&sc->mtx);
	if (offset < XHCI_CAPLEN)	/* read only registers */
		WPRINTF(("pci_xhci: write RO-CAPs offset %ld", offset));
	else if (offset < sc->dboff)
		pci_xhci_hostop_write(sc, offset, value);
	else if (offset < sc->rtsoff)
		pci_xhci_dbregs_write(sc, offset, value);
	else if (offset < sc->regsend)
		pci_xhci_rtsregs_write(sc, offset, value);
	else
		WPRINTF(("pci_xhci: write invalid offset %ld", offset));

	pthread_mutex_unlock(&sc->mtx);
}

static uint64_t
pci_xhci_hostcap_read(struct pci_xhci_softc *sc, uint64_t offset)
{
	uint64_t	value;

	switch (offset) {
	case XHCI_CAPLENGTH:	/* 0x00 */
		value = sc->caplength;
		break;

	case XHCI_HCSPARAMS1:	/* 0x04 */
		value = sc->hcsparams1;
		break;

	case XHCI_HCSPARAMS2:	/* 0x08 */
		value = sc->hcsparams2;
		break;

	case XHCI_HCSPARAMS3:	/* 0x0C */
		value = sc->hcsparams3;
		break;

	case XHCI_HCSPARAMS0:	/* 0x10 */
		value = sc->hccparams1;
		break;

	case XHCI_DBOFF:	/* 0x14 */
		value = sc->dboff;
		break;

	case XHCI_RTSOFF:	/* 0x18 */
		value = sc->rtsoff;
		break;

	case XHCI_HCCPRAMS2:	/* 0x1C */
		value = sc->hccparams2;
		break;

	default:
		value = 0;
		break;
	}

	DPRINTF(("pci_xhci: hostcap read offset 0x%lx -> 0x%lx",
	        offset, value));

	return (value);
}

static uint64_t
pci_xhci_hostop_read(struct pci_xhci_softc *sc, uint64_t offset)
{
	uint64_t value;

	offset = (offset - XHCI_CAPLEN);

	switch (offset) {
	case XHCI_USBCMD:	/* 0x00 */
		value = sc->opregs.usbcmd;
		break;

	case XHCI_USBSTS:	/* 0x04 */
		value = sc->opregs.usbsts;
		break;

	case XHCI_PAGESIZE:	/* 0x08 */
		value = sc->opregs.pgsz;
		break;

	case XHCI_DNCTRL:	/* 0x14 */
		value = sc->opregs.dnctrl;
		break;

	case XHCI_CRCR_LO:	/* 0x18 */
		value = sc->opregs.crcr & XHCI_CRCR_LO_CRR;
		break;

	case XHCI_CRCR_HI:	/* 0x1C */
		value = 0;
		break;

	case XHCI_DCBAAP_LO:	/* 0x30 */
		value = sc->opregs.dcbaap & 0xFFFFFFFF;
		break;

	case XHCI_DCBAAP_HI:	/* 0x34 */
		value = (sc->opregs.dcbaap >> 32) & 0xFFFFFFFF;
		break;

	case XHCI_CONFIG:	/* 0x38 */
		value = sc->opregs.config;
		break;

	default:
		if (offset >= 0x400)
			value = pci_xhci_portregs_read(sc, offset);
		else
			value = 0;

		break;
	}

	if (offset < 0x400)
		DPRINTF(("pci_xhci: hostop read offset 0x%lx -> 0x%lx",
		        offset, value));

	return (value);
}

static uint64_t
pci_xhci_dbregs_read(struct pci_xhci_softc *sc, uint64_t offset)
{

	/* read doorbell always returns 0 */
	return (0);
}

static uint64_t
pci_xhci_rtsregs_read(struct pci_xhci_softc *sc, uint64_t offset)
{
	uint32_t	value;

	offset -= sc->rtsoff;
	value = 0;

	if (offset == XHCI_MFINDEX) {
		value = sc->rtsregs.mfindex;
	} else if (offset >= 0x20) {
		int item;
		uint32_t *p;

		offset -= 0x20;
		item = offset % 32;

		assert(offset < sizeof(sc->rtsregs.intrreg));

		p = &sc->rtsregs.intrreg.iman;
		p += item / sizeof(uint32_t);
		value = *p;
	}

	DPRINTF(("pci_xhci: rtsregs read offset 0x%lx -> 0x%x",
	        offset, value));

	return (value);
}

static uint64_t
pci_xhci_xecp_read(struct pci_xhci_softc *sc, uint64_t offset)
{
	uint32_t	value;

	offset -= sc->regsend;
	value = 0;

	switch (offset) {
	case 0:
		/* rev major | rev minor | next-cap | cap-id */
		value = (0x02 << 24) | (4 << 8) | XHCI_ID_PROTOCOLS;
		break;
	case 4:
		/* name string = "USB" */
		value = 0x20425355;
		break;
	case 8:
		/* psic | proto-defined | compat # | compat offset */
		value = ((XHCI_MAX_DEVS/2) << 8) | sc->usb2_port_start;
		break;
	case 12:
		break;
	case 16:
		/* rev major | rev minor | next-cap | cap-id */
		value = (0x03 << 24) | XHCI_ID_PROTOCOLS;
		break;
	case 20:
		/* name string = "USB" */
		value = 0x20425355;
		break;
	case 24:
		/* psic | proto-defined | compat # | compat offset */
		value = ((XHCI_MAX_DEVS/2) << 8) | sc->usb3_port_start;
		break;
	case 28:
		break;
	default:
		DPRINTF(("pci_xhci: xecp invalid offset 0x%lx", offset));
		break;
	}

	DPRINTF(("pci_xhci: xecp read offset 0x%lx -> 0x%x",
	        offset, value));

	return (value);
}


static uint64_t
pci_xhci_read(struct vmctx *ctx, int vcpu, struct pci_devinst *pi, int baridx,
    uint64_t offset, int size)
{
	struct pci_xhci_softc *sc;
	uint32_t	value;

	sc = pi->pi_arg;

	assert(baridx == 0);

	pthread_mutex_lock(&sc->mtx);
	if (offset < XHCI_CAPLEN)
		value = pci_xhci_hostcap_read(sc, offset);
	else if (offset < sc->dboff)
		value = pci_xhci_hostop_read(sc, offset);
	else if (offset < sc->rtsoff)
		value = pci_xhci_dbregs_read(sc, offset);
	else if (offset < sc->regsend)
		value = pci_xhci_rtsregs_read(sc, offset);
	else if (offset < (sc->regsend + 4*32))
		value = pci_xhci_xecp_read(sc, offset);
	else {
		value = 0;
		WPRINTF(("pci_xhci: read invalid offset %ld", offset));
	}

	pthread_mutex_unlock(&sc->mtx);

	switch (size) {
	case 1:
		value &= 0xFF;
		break;
	case 2:
		value &= 0xFFFF;
		break;
	case 4:
		value &= 0xFFFFFFFF;
		break;
	}

	return (value);
}

static void
pci_xhci_reset_port(struct pci_xhci_softc *sc, int portn, int warm)
{
	struct pci_xhci_portregs *port;
	struct pci_xhci_dev_emu	*dev;
	struct xhci_trb		evtrb;
	struct usb_native_devinfo *di;
	int	error;
	int	speed;
	int	index;

	assert(portn <= XHCI_MAX_DEVS);

	DPRINTF(("xhci reset port %d", portn));

	port = XHCI_PORTREG_PTR(sc, portn);
	dev = XHCI_DEVINST_PTR(sc, portn);
	if (dev) {
		port->portsc &= ~(XHCI_PS_PLS_MASK | XHCI_PS_PR | XHCI_PS_PRC);
		port->portsc |= XHCI_PS_PED |
		    XHCI_PS_SPEED_SET(dev->dev_ue->ue_usbspeed);

		if (warm && dev->dev_ue->ue_usbver == 3) {
			port->portsc |= XHCI_PS_WRC;
		}

		if ((port->portsc & XHCI_PS_PRC) == 0) {
			port->portsc |= XHCI_PS_PRC;

			pci_xhci_set_evtrb(&evtrb, portn,
			     XHCI_TRB_ERROR_SUCCESS,
			     XHCI_TRB_EVENT_PORT_STS_CHANGE);
			error = pci_xhci_insert_event(sc, &evtrb, 1);
			if (error != XHCI_TRB_ERROR_SUCCESS)
				DPRINTF(("xhci reset port insert event "
				         "failed"));
		}
	} else {
		index = pci_xhci_get_native_port_index_by_vport(sc, portn);
		if (index < 0) {
			DPRINTF(("fail to reset port %d", portn));
			return;
		}
		di = &sc->native_ports[index].info;

		speed = pci_xhci_convert_speed(di->speed);
		port->portsc &= ~(XHCI_PS_PLS_MASK | XHCI_PS_PR | XHCI_PS_PRC);
		port->portsc |= XHCI_PS_PED | XHCI_PS_SPEED_SET(speed);

		if (warm && di->bcd >= 0x300)
			port->portsc |= XHCI_PS_WRC;

		if ((port->portsc & XHCI_PS_PRC) == 0) {
			port->portsc |= XHCI_PS_PRC;

			pci_xhci_set_evtrb(&evtrb, portn,
				XHCI_TRB_ERROR_SUCCESS,
				XHCI_TRB_EVENT_PORT_STS_CHANGE);
			error = pci_xhci_insert_event(sc, &evtrb, 1);
			if (error != XHCI_TRB_ERROR_SUCCESS)
				DPRINTF(("reset port insert event failed"));
		}
	}
}

static void
pci_xhci_init_port(struct pci_xhci_softc *sc, int portn)
{
	struct pci_xhci_portregs *port;
	struct pci_xhci_dev_emu	*dev;

	port = XHCI_PORTREG_PTR(sc, portn);
	dev = XHCI_DEVINST_PTR(sc, portn);
	if (dev) {
		port->portsc = XHCI_PS_CCS |		/* connected */
		               XHCI_PS_PP;		/* port power */
		
		if (dev->dev_ue->ue_usbver == 2) {
			port->portsc |= XHCI_PS_PLS_SET(UPS_PORT_LS_POLL) |
		               XHCI_PS_SPEED_SET(dev->dev_ue->ue_usbspeed);
		} else {
			port->portsc |= XHCI_PS_PLS_SET(UPS_PORT_LS_U0) |
		               XHCI_PS_PED |		/* enabled */
		               XHCI_PS_SPEED_SET(dev->dev_ue->ue_usbspeed);
		}
		
		DPRINTF(("Init port %d 0x%x", portn, port->portsc));
	} else {
		port->portsc = XHCI_PS_PLS_SET(UPS_PORT_LS_RX_DET) | XHCI_PS_PP;
		DPRINTF(("Init empty port %d 0x%x", portn, port->portsc));
	}
}

static int
pci_xhci_dev_intr(struct usb_hci *hci, int epctx)
{
	struct pci_xhci_dev_emu *dev;
	struct xhci_dev_ctx	*dev_ctx;
	struct xhci_trb		evtrb;
	struct pci_xhci_softc	*sc;
	struct pci_xhci_portregs *p;
	struct xhci_endp_ctx	*ep_ctx;
	int	error = 0;
	int	dir_in;
	int	epid;

	dir_in = epctx & 0x80;
	epid = epctx & ~0x80;

	/* HW endpoint contexts are 0-15; convert to epid based on dir */
	epid = (epid * 2) + (dir_in ? 1 : 0);

	assert(epid >= 1 && epid <= 31);

	dev = hci->hci_sc;
	sc = dev->xsc;

	/* check if device is ready; OS has to initialise it */
	if (sc->rtsregs.erstba_p == NULL ||
	    (sc->opregs.usbcmd & XHCI_CMD_RS) == 0 ||
	    dev->dev_ctx == NULL)
		return (0);

	p = XHCI_PORTREG_PTR(sc, hci->hci_port);

	/* raise event if link U3 (suspended) state */
	if (XHCI_PS_PLS_GET(p->portsc) == 3) {
		p->portsc &= ~XHCI_PS_PLS_MASK;
		p->portsc |= XHCI_PS_PLS_SET(UPS_PORT_LS_RESUME);
		if ((p->portsc & XHCI_PS_PLC) != 0)
			return (0);

		p->portsc |= XHCI_PS_PLC;

		pci_xhci_set_evtrb(&evtrb, hci->hci_port,
		      XHCI_TRB_ERROR_SUCCESS, XHCI_TRB_EVENT_PORT_STS_CHANGE);
		error = pci_xhci_insert_event(sc, &evtrb, 0);
		if (error != XHCI_TRB_ERROR_SUCCESS)
			goto done;
	}

	dev_ctx = dev->dev_ctx;
	ep_ctx = &dev_ctx->ctx_ep[epid];
	if ((ep_ctx->dwEpCtx0 & 0x7) == XHCI_ST_EPCTX_DISABLED) {
		DPRINTF(("xhci device interrupt on disabled endpoint %d",
		         epid));
		return (0);
	}

	DPRINTF(("xhci device interrupt on endpoint %d", epid));

	pci_xhci_device_doorbell(sc, hci->hci_port, epid, 0);

done:
	return (error);
}

static int
pci_xhci_dev_event(struct usb_hci *hci, enum hci_usbev evid, void *param)
{

	DPRINTF(("xhci device event port %d", hci->hci_port));
	return (0);
}

static int
pci_xhci_native_usb_dev_conn_cb(void *hci_data, void *dev_data)
{
	struct pci_xhci_softc *xdev;
	struct usb_native_devinfo *di;
	int vport = -1;
	int index;
	int rc;
	int i;
	int s3_conn = 0;

	xdev = hci_data;
	di = dev_data;

	/* print physical information about new device */
	DPRINTF(("%04x:%04x %d-%s connecting.\r\n", di->vid, di->pid,
			di->path.bus, usb_dev_path(&di->path)));

	index = pci_xhci_get_native_port_index_by_path(xdev, &di->path);
	if (index < 0) {
		UPRINTF(LINF, "%04x:%04x %d-%s doesn't belong to this"
				" vm, bye.\r\n", di->vid, di->pid,
				di->path.bus, usb_dev_path(&di->path));
		return 0;
	}

	if (di->type == USB_TYPE_EXTHUB) {
		rc = pci_xhci_assign_hub_ports(xdev, di);
		if (rc < 0)
			UPRINTF(LFTL, "fail to assign ports of hub %d-%s\r\n",
					di->path.bus, usb_dev_path(&di->path));
		return 0;
	}

	DPRINTF(("%04x:%04x %d-%s belong to this vm.\r\n", di->vid,
			di->pid, di->path.bus, usb_dev_path(&di->path)));

	for (i = 0; xdev->vbdp_dev_num && i < XHCI_MAX_DEVICES; ++i) {
		if (xdev->vbdp_devs[i].state != S3_VBDP_START)
			continue;

		if (!usb_dev_path_cmp(&di->path, &xdev->vbdp_devs[i].path))
			continue;

		s3_conn = 1;
		vport = xdev->vbdp_devs[i].vport;
		DPRINTF(("Skip and cache connect event for %d-%s\r\n",
				di->path.bus, usb_dev_path(&di->path)));
		break;
	}

	if (vport <= 0)
		vport = pci_xhci_get_free_vport(xdev, di);

	if (vport <= 0) {
		DPRINTF(("no free virtual port for native device %d-%s"
				"\r\n", di->path.bus,
				usb_dev_path(&di->path)));
		goto errout;
	}

	xdev->native_ports[index].vport = vport;
	xdev->native_ports[index].info = *di;
	xdev->native_ports[index].state = VPORT_CONNECTED;

	DPRINTF(("%04X:%04X %d-%s is attached to virtual port %d.\r\n",
			di->vid, di->pid, di->path.bus,
			usb_dev_path(&di->path), vport));

	/* we will report connecting event in xhci_vbdp_thread for
	 * device that hasn't complete the S3 process
	 */
	if (s3_conn)
		return 0;

	/* Trigger port change event for the arriving device */
	if (pci_xhci_connect_port(xdev, vport, di->speed, 1))
		DPRINTF(("fail to report port event\n"));

	return 0;
errout:
	return -1;
}

static int
pci_xhci_native_usb_dev_disconn_cb(void *hci_data, void *dev_data)
{
	struct pci_xhci_softc *xdev;
	struct pci_xhci_dev_emu *edev;
	struct usb_native_devinfo *di;
	uint8_t vport, slot;
	uint16_t state;
	int need_intr = 1;
	int index;
	int rc;
	int i;

	xdev = hci_data;
	di = dev_data;
	if (!pci_xhci_is_valid_portnum(ROOTHUB_PORT(di->path))) {
		DPRINTF(("invalid physical port %d\r\n",
				ROOTHUB_PORT(di->path)));
		return -1;
	}

	index = pci_xhci_get_native_port_index_by_path(xdev, &di->path);
	if (index < 0) {
		DPRINTF(("fail to find physical port %d\r\n",
				ROOTHUB_PORT(di->path)));
		return -1;
	}

	if (di->type == USB_TYPE_EXTHUB) {
		rc = pci_xhci_unassign_hub_ports(xdev, di);
		if (rc < 0)
			DPRINTF(("fail to unassign the ports of hub"
					" %d-%s\r\n", di->path.bus,
					usb_dev_path(&di->path)));
		return 0;
	}

	state = xdev->native_ports[index].state;
	vport = xdev->native_ports[index].vport;

	if (state == VPORT_CONNECTED && vport > 0) {
		/*
		 * When this place is reached, it means the physical
		 * USB device is disconnected before the emulation
		 * procedure is started. The related states should be
		 * cleared for future connecting.
		 */
		DPRINTF(("disconnect VPORT_CONNECTED device: "
				"%d-%s vport %d\r\n", di->path.bus,
				usb_dev_path(&di->path), vport));
		pci_xhci_disconnect_port(xdev, vport, 0);
		xdev->native_ports[index].state = VPORT_ASSIGNED;
		return 0;
	}

	edev = xdev->devices[vport];
	for (slot = 1; slot <= XHCI_MAX_SLOTS; ++slot)
		if (xdev->slots[slot] == edev)
			break;

	for (i = 0; xdev->vbdp_dev_num && i < XHCI_MAX_DEVICES; ++i) {
		if (xdev->vbdp_devs[i].state != S3_VBDP_START)
			continue;

		if (!usb_dev_path_cmp(&xdev->vbdp_devs[i].path, &di->path))
			continue;

		/*
		 * we do nothing here for device that is in the middle of
		 * S3 resuming process.
		 */
		DPRINTF(("disconnect device %d-%s on vport %d with "
				"state %d and return.\r\n", di->path.bus,
				usb_dev_path(&di->path), vport, state));
		return 0;
	}

	if (!(state == VPORT_EMULATED || state == VPORT_CONNECTED))
		UPRINTF(LFTL, "error: unexpected state %d\r\n", state);

	xdev->native_ports[index].state = VPORT_ASSIGNED;
	xdev->native_ports[index].vport = 0;

	DPRINTF(("disconnect device %d-%s on vport %d with state %d\r\n",
			di->path.bus, usb_dev_path(&di->path), vport, state));
	if (pci_xhci_disconnect_port(xdev, vport, need_intr)) {
		UPRINTF(LFTL, "fail to report event\r\n");
		return -1;
	}

	/*
	 * At this point, the resources allocated for virtual device
	 * should not be released, it should be released in the
	 * pci_xhci_cmd_disable_slot function.
	 */
	return 0;
}

/*
 * return value:
 * = 0: succeed without interrupt
 * > 0: succeed with interrupt
 * < 0: failure
 */
static int
pci_xhci_usb_dev_notify_cb(void *hci_data, void *udev_data)
{
	int slot, epid, intr, rc;
	struct usb_data_xfer *xfer;
	struct pci_xhci_dev_emu *edev;
	struct pci_xhci_softc *xdev;

	xfer = udev_data;
	if (!xfer)
		return -1;

	epid = xfer->epid;
	edev = xfer->dev;
	if (!edev)
		return -1;

	xdev = edev->xsc;
	if (!xdev)
		return -1;

	slot = edev->hci.hci_address;
	rc = pci_xhci_xfer_complete(xdev, xfer, slot, epid, &intr);

	if (rc)
		return -1;
	else if (intr)
		return 1;
	else
		return 0;
}

static int
pci_xhci_usb_dev_intr_cb(void *hci_data, void *udev_data)
{
	struct pci_xhci_dev_emu *edev;

	edev = hci_data;
	if (edev && edev->xsc)
		pci_xhci_assert_interrupt(edev->xsc);

	return 0;
}

static int
pci_xhci_usb_dev_lock_ep_cb(void *hci_data, void *udev_data)
{
	struct pci_xhci_dev_emu *edev;
	struct pci_xhci_dev_ep	*ep;
	int			epid;

	edev = hci_data;
	epid = *((int *)udev_data);

	if (edev && edev->xsc && epid > 0 && epid < 32) {
		ep = &edev->eps[epid];
		pthread_mutex_lock(&ep->mtx);
	}

	return 0;
}

static int
pci_xhci_usb_dev_unlock_ep_cb(void *hci_data, void *udev_data)
{
	struct pci_xhci_dev_emu	*edev;
	struct pci_xhci_dev_ep	*ep;
	int			epid;

	edev = hci_data;
	epid = *((int *)udev_data);

	if (edev && edev->xsc && epid > 0 && epid < 32) {
		ep = &edev->eps[epid];
		pthread_mutex_unlock(&ep->mtx);
	}

	return 0;
}

static struct pci_xhci_dev_emu*
pci_xhci_dev_create(struct pci_xhci_softc *xdev, void *dev_data)
{
	struct usb_devemu *ue = NULL;
	struct pci_xhci_dev_emu *de = NULL;
	void *ud = NULL;
	int rc;

	ue = calloc(1, sizeof(struct usb_devemu));
	if (!ue)
		return NULL;

	/*
	 * TODO: at present, the following functions are
	 * enough. But for the purpose to be compatible with
	 * usb_mouse.c, the high level design including the
	 * function interface should be changed and refined
	 * in future.
	 */
	ue->ue_init	= usb_dev_init;
	ue->ue_request	= usb_dev_request;
	ue->ue_data	= usb_dev_data;
	ue->ue_info	= usb_dev_info;
	ue->ue_reset	= usb_dev_reset;
	ue->ue_remove	= NULL;
	ue->ue_stop	= NULL;
	ue->ue_deinit	= usb_dev_deinit;
	ue->ue_devtype	= USB_DEV_PORT_MAPPER;

	ud = ue->ue_init(dev_data, NULL);
	if (!ud) {
		goto errout;
	}

	rc = ue->ue_info(ud, USB_INFO_VERSION, &ue->ue_usbver,
			sizeof(ue->ue_usbver));
	if (rc < 0) {
		goto errout;
	}

	rc = ue->ue_info(ud, USB_INFO_SPEED, &ue->ue_usbspeed,
			sizeof(ue->ue_usbspeed));
	if (rc < 0) {
		goto errout;
	}

	de = calloc(1, sizeof(struct pci_xhci_dev_emu));
	if (!de) {
		goto errout;
	}

	de->xsc			= xdev;
	de->dev_ue		= ue;
	de->dev_sc		= ud;
	de->hci.hci_sc		= NULL;
	de->hci.hci_intr	= NULL;
	de->hci.hci_event	= NULL;
	de->hci.hci_address	= 0;

	return de;

errout:
	if (ud)
		ue->ue_deinit(ud);

	free(ue);
	free(de);
	return NULL;
}

static void
pci_xhci_dev_destroy(struct pci_xhci_dev_emu *de)
{
	struct usb_devemu *ue;
	struct usb_dev *ud;
	struct pci_xhci_dev_ep *vdep;
	int i;

	if (de) {
		ue = de->dev_ue;
		ud = de->dev_sc;
		if (ue) {
			if (ue->ue_devtype == USB_DEV_PORT_MAPPER) {
				if (ue->ue_deinit)
					ue->ue_deinit(ud);
			}
		} else
			return;

		if (ue->ue_devtype == USB_DEV_PORT_MAPPER)
			free(ue);

		for (i = 1; i < XHCI_MAX_ENDPOINTS; i++) {
			vdep = &de->eps[i];
			if (vdep->ep_xfer) {
				pci_xhci_free_usb_xfer(vdep->ep_xfer);
				vdep->ep_xfer = NULL;
			}
		}

		free(de);
	}
}

static void
pci_xhci_device_usage(char *opt)
{

	EPRINTLN("Invalid USB emulation \"%s\"", opt);
}

static int
pci_xhci_parse_bus_port(struct pci_xhci_softc *sc, char *opts)
{
	int rc = 0;
	char *tstr;
	int port, bus, index;
	struct usb_devpath path;
	struct usb_native_devinfo di;

	tstr = opts;
	/* 'bus-port' format */
	if (!tstr || dm_strtoi(tstr, &tstr, 10, &bus) || *tstr != '-' ||
		dm_strtoi(tstr + 1, &tstr, 10, &port)) {
		rc = -1;
		goto errout;
	}

	if (bus >= USB_NATIVE_NUM_BUS || port >= USB_NATIVE_NUM_PORT) {
		rc = -1;
		goto errout;
	}

	if (!usb_native_bus_port_existed(bus, port)) {
		rc = -21;
		goto errout;
	}

	port +=1;

	memset(&path, 0, sizeof(path));
	path.bus = bus;
	path.depth = 1;
	path.path[0] = port;
	di.path = path;
	index = pci_xhci_set_native_port_assigned(sc, &di);
	if (index < 0) {
		fprintf(stderr, "fail to assign native_port\r\n");
		goto errout;
	}
	return 0;
errout:
	if (rc)
		fprintf(stderr, "%s fails, rc=%d\r\n", __func__, rc);
	return rc;

}

static int
pci_xhci_parse_opts(struct pci_xhci_softc *sc, char *opts)
{
	struct pci_xhci_dev_emu	**devices;
	struct pci_xhci_dev_emu	*dev;
	struct usb_devemu	*ue;
	void	*devsc;
	char	*uopt, *xopts, *config;
	int	usb3_port, usb2_port, i;


	uopt = NULL;
	usb3_port = sc->usb3_port_start - 1;
	usb2_port = sc->usb2_port_start - 1;
	devices = NULL;

	if (opts == NULL)
		goto portsfinal;

	devices = calloc(XHCI_MAX_DEVS, sizeof(struct pci_xhci_dev_emu *));

	sc->slots = calloc(XHCI_MAX_SLOTS + 1, sizeof(struct pci_xhci_dev_emu *));
	sc->devices = devices;
	sc->ndevices = 0;
	sc->tablet = 0;

	uopt = strdup(opts);
	for (xopts = strtok(uopt, ",");
	     xopts != NULL;
	     xopts = strtok(NULL, ",")) {
		if (usb2_port == ((sc->usb2_port_start-1) + XHCI_MAX_DEVS/2) ||
		    usb3_port == ((sc->usb3_port_start-1) + XHCI_MAX_DEVS/2)) {
			WPRINTF(("pci_xhci max number of USB 2 or 3 "
			     "devices reached, max %d", XHCI_MAX_DEVS/2));
			usb2_port = usb3_port = -1;
			goto done;
		}

		/* device[=<config>] */
		if ((config = strchr(xopts, '=')) == NULL)
			config = "";		/* no config */
		else
			*config++ = '\0';

		if (isdigit(xopts[0])) {
			if (pci_xhci_parse_bus_port(sc, xopts)) {
				pci_xhci_device_usage(xopts);
                                goto done;
                        }
			fprintf(stderr, "pci_xhci adding device %s, opts \"%s\"\r\n",
				xopts, config);
		} else {
			ue = usb_emu_finddev(xopts);
			if (ue == NULL) {
				pci_xhci_device_usage(xopts);
				DPRINTF(("pci_xhci device not found %s", xopts));
				usb2_port = usb3_port = -1;
				goto done;
			}
			DPRINTF(("pci_xhci adding device %s, opts \"%s\"",
			        xopts, config));

			dev = calloc(1, sizeof(struct pci_xhci_dev_emu));
			dev->xsc = sc;
			dev->hci.hci_sc = dev;
			dev->hci.hci_intr = pci_xhci_dev_intr;
			dev->hci.hci_event = pci_xhci_dev_event;

			if (ue->ue_usbver == 2) {
				dev->hci.hci_port = usb2_port + 1;
				devices[usb2_port] = dev;
				usb2_port++;
			} else {
				dev->hci.hci_port = usb3_port + 1;
				devices[usb3_port] = dev;
				usb3_port++;
			}

			dev->hci.hci_address = 0;
			devsc = ue->ue_init(&dev->hci, config);
			if (devsc == NULL) {
				pci_xhci_device_usage(xopts);
				usb2_port = usb3_port = -1;
				goto done;
			}

			dev->dev_ue = ue;
			dev->dev_sc = devsc;

			/* assign slot number to device */
			sc->slots[sc->ndevices] = dev;
			sc->tablet = 1;

			sc->ndevices++;
		}
	}

portsfinal:
	sc->portregs = calloc(XHCI_MAX_DEVS, sizeof(struct pci_xhci_portregs));

	if (sc->ndevices > 0) {
		/* port and slot numbering start from 1 */
		sc->devices--;
		sc->portregs--;
		sc->slots--;

		for (i = 1; i <= XHCI_MAX_DEVS; i++) {
			pci_xhci_init_port(sc, i);
		}
	} else {
		WPRINTF(("pci_xhci no USB devices configured"));
		sc->ndevices = 1;
	}

done:
	if (devices != NULL) {
		if (usb2_port <= 0 && usb3_port <= 0) {
			sc->devices = NULL;
			for (i = 0; devices[i] != NULL; i++)
				free(devices[i]);
			sc->ndevices = -1;

			free(devices);
		}
	}
	free(uopt);
	return (sc->ndevices);
}

static int
pci_xhci_init(struct vmctx *ctx, struct pci_devinst *pi, char *opts)
{
	struct pci_xhci_softc *sc;
	int	error;

	if (xhci_in_use) {
		WPRINTF(("pci_xhci controller already defined"));
		return (-1);
	}
	xhci_in_use = 1;

	sc = calloc(1, sizeof(struct pci_xhci_softc));
	pi->pi_arg = sc;
	sc->xsc_pi = pi;

	sc->usb2_port_start = (XHCI_MAX_DEVS/2) + 1;
	sc->usb3_port_start = 1;

	/* discover devices */
	error = pci_xhci_parse_opts(sc, opts);
	if (error < 0)
		goto done;
	else
		error = 0;

	if (usb_dev_sys_init(pci_xhci_native_usb_dev_conn_cb,
				pci_xhci_native_usb_dev_disconn_cb,
				pci_xhci_usb_dev_notify_cb,
				pci_xhci_usb_dev_intr_cb,
				pci_xhci_usb_dev_lock_ep_cb,
				pci_xhci_usb_dev_unlock_ep_cb,
				sc, usb_get_log_level()) < 0) {
		error = -3;
		goto done;
	}

	sc->caplength = XHCI_SET_CAPLEN(XHCI_CAPLEN) |
	                XHCI_SET_HCIVERSION(0x0100);
	sc->hcsparams1 = XHCI_SET_HCSP1_MAXPORTS(XHCI_MAX_DEVS) |
	                 XHCI_SET_HCSP1_MAXINTR(1) |	/* interrupters */
	                 XHCI_SET_HCSP1_MAXSLOTS(XHCI_MAX_SLOTS);
	sc->hcsparams2 = XHCI_SET_HCSP2_ERSTMAX(XHCI_ERST_MAX) |
	                 XHCI_SET_HCSP2_IST(0x04);
	sc->hcsparams3 = 0;				/* no latency */
	sc->hccparams1 = XHCI_SET_HCCP1_NSS(1) |	/* no 2nd-streams */
	                 XHCI_SET_HCCP1_SPC(1) |	/* short packet */
	                 XHCI_SET_HCCP1_MAXPSA(XHCI_STREAMS_MAX);
	sc->hccparams2 = XHCI_SET_HCCP2_LEC(1) |
	                 XHCI_SET_HCCP2_U3C(1);
	sc->dboff = XHCI_SET_DOORBELL(XHCI_CAPLEN + XHCI_PORTREGS_START +
	            XHCI_MAX_DEVS * sizeof(struct pci_xhci_portregs));

	/* dboff must be 32-bit aligned */
	if (sc->dboff & 0x3)
		sc->dboff = (sc->dboff + 0x3) & ~0x3;

	/* rtsoff must be 32-bytes aligned */
	sc->rtsoff = XHCI_SET_RTSOFFSET(sc->dboff + (XHCI_MAX_SLOTS+1) * 32);
	if (sc->rtsoff & 0x1F)
		sc->rtsoff = (sc->rtsoff + 0x1F) & ~0x1F;

	DPRINTF(("pci_xhci dboff: 0x%x, rtsoff: 0x%x", sc->dboff,
	        sc->rtsoff));

	sc->opregs.usbsts = XHCI_STS_HCH;
	sc->opregs.pgsz = XHCI_PAGESIZE_4K;

	pci_xhci_reset(sc);

	sc->regsend = sc->rtsoff + 0x20 + 32;		/* only 1 intrpter */

	/*
	 * Set extended capabilities pointer to be after regsend;
	 * value of xecp field is 32-bit offset.
	 */
	sc->hccparams1 |= XHCI_SET_HCCP1_XECP(sc->regsend/4);

	pci_set_cfgdata16(pi, PCIR_DEVICE, 0x1E31);
	pci_set_cfgdata16(pi, PCIR_VENDOR, 0x8086);
	pci_set_cfgdata8(pi, PCIR_CLASS, PCIC_SERIALBUS);
	pci_set_cfgdata8(pi, PCIR_SUBCLASS, PCIS_SERIALBUS_USB);
	pci_set_cfgdata8(pi, PCIR_PROGIF,PCIP_SERIALBUS_USB_XHCI);
	pci_set_cfgdata8(pi, PCI_USBREV, PCI_USB_REV_3_0);

	pci_emul_add_msicap(pi, 1);

	/* regsend + xecp registers */
	pci_emul_alloc_bar(pi, 0, PCIBAR_MEM32, sc->regsend + 4*32);
	DPRINTF(("pci_xhci pci_emu_alloc: %d", sc->regsend + 4*32));


	pci_lintr_request(pi);

	pthread_mutex_init(&sc->mtx, NULL);

	/* create vbdp_thread */
	sc->vbdp_polling = true;
	sem_init(&sc->vbdp_sem, 0, 0);
	error = pthread_create(&sc->vbdp_thread, NULL, xhci_vbdp_thread,
			sc);
	if (error)
		goto done;

done:
	if (error) {
		xhci_in_use = 0;

		free(sc);
	}

	return (error);
}

struct pci_devemu pci_de_xhci = {
	.pe_emu =	"xhci",
	.pe_init =	pci_xhci_init,
	.pe_barwrite =	pci_xhci_write,
	.pe_barread =	pci_xhci_read
};
PCI_EMUL_SET(pci_de_xhci);
