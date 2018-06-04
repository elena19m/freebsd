#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>
#include <arm64/vmm/arm64.h>

#include "vgic_v3.h"

enum access_type {
	READ,
	WRITE,
};

#define	RES0	(0UL)

#define	GICR_FRAME_RD	0
#define	GICR_FRAME_SGI	GICR_RD_BASE_SIZE

#define redist_simple_read(src, destp, vm, vcpuid)			\
do {									\
	struct hyp *hyp = vm_get_cookie(vm);				\
	struct vgic_v3_redist *redist = &hyp->ctx[vcpuid].vgic_redist;	\
	*destp = redist->src;						\
} while (0);

#define redist_simple_write(src, dest, vm, vcpuid)			\
do {									\
	struct hyp *hyp = vm_get_cookie(vm);				\
	struct vgic_v3_redist *redist = &hyp->ctx[vcpuid].vgic_redist;	\
	redist->dest = src;						\
} while (0);

static int
redist_ctlr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_ctlr, rval, vm, vcpuid);
	/* Writes are never pending */
	*rval &= ~GICR_CTLR_RWP & ~GICR_CTLR_UWP;

	*retu = false;
	return (0);
}

static int
redist_ctlr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_write(wval, gicr_ctlr, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_typer_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_typer, rval, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_typer_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	eprintf("Warning: Attempted write to read-only register GICR_TYPER.\n");

	*retu = false;
	return (0);
}

static int
redist_waker_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	/* Redistributor is always awake */
	*rval = 0 & ~GICR_WAKER_PS & ~GICR_WAKER_CA;

	*retu = false;
	return (0);
}

static int
redist_waker_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	/* Ignore writes */

	*retu = false;
	return (0);
}

static int
redist_igroupr0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_igroupr0, rval, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_igroupr0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_write(wval, gicr_igroupr0, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_isenabler0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_ixenabler0, rval, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_isenabler0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	uint32_t ixenabler0;
	bool *retu = arg;

	redist_simple_read(gicr_ixenabler0, &ixenabler0, vm, vcpuid);
	/* A write of 1 enables the interrupt */
	ixenabler0 |= wval;
	redist_simple_write(ixenabler0, gicr_ixenabler0, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_icenabler0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_ixenabler0, rval, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_icenabler0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	uint32_t ixenabler0;
	bool *retu = arg;

	redist_simple_read(gicr_ixenabler0, &ixenabler0, vm, vcpuid);
	/* A write of 1 disabled the interrupt */
	ixenabler0 &= ~wval;
	redist_simple_write(ixenabler0, gicr_ixenabler0, vm, vcpuid);

	*retu = false;
	return (0);
}


static int
redist_ipriorityr_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir)
{
	struct vgic_v3_redist *redist = &hyp->ctx[vcpuid].vgic_redist;
	size_t regsize, n;
	size_t off;

	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICR_IPRIORITYR].start;
	regsize = sizeof(*redist->gicr_ipriorityr);
	n = off / regsize;

	if (dir == READ)
		*val = redist->gicr_ipriorityr[n];
	else
		redist->gicr_ipriorityr[n] = *val;

	*retu = false;
	return (0);
}

static int
redist_ipriorityr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = redist_ipriorityr_access(hyp, vcpuid, fault_ipa, rval, retu,
	    READ);

	return (error);
}

static int
redist_ipriorityr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = redist_ipriorityr_access(hyp, vcpuid, fault_ipa, &wval, retu,
	    WRITE);

	return (error);
}

static int
redist_icfgr0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_icfgr0, rval, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_icfgr0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_write(wval, gicr_icfgr0, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_icfgr1_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_icfgr1, rval, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_icfgr1_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_write(wval, gicr_icfgr1, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_pidr2_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	bool *retu = arg;

	/* GICR_PIDR2 has the same value as GICD_PIDR2 */
	*rval = dist->gicd_pidr2;

	*retu = false;
	return (0);
}

static int
redist_pidr2_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	eprintf("Warning: Attempted write to read-only register GICR_PIDR2.\n");

	*retu = false;
	return (0);
}

void
redist_mmio_init(struct hypctx *hypctx)
{
	struct hyp *hyp = hypctx->hyp;
	struct vgic_v3_redist *redist = &hypctx->vgic_redist;

	KASSERT(hyp->vgic_mmio_regions != NULL, ("vgic_mmio_regions not allocated"));

#define	init_mmio_region(addr, frame, num, regbits, shortname)		\
	hyp->vgic_mmio_regions[VGIC_ ##addr] = (struct vgic_mmio_region){ \
		.start 	= redist->start + frame + addr,			\
		.end 	= redist->start + frame + addr + num * (regbits / 8), \
		.read 	= redist_ ##shortname ##_read,			\
		.write 	= redist_ ##shortname ##_write,			\
	}
	init_mmio_region(GICR_CTLR, GICR_FRAME_RD, 1, 32, ctlr);
	init_mmio_region(GICR_TYPER, GICR_FRAME_RD, 1, 64, typer);
	init_mmio_region(GICR_WAKER, GICR_FRAME_RD, 1, 32, waker);
	init_mmio_region(GICR_PIDR2, GICR_FRAME_RD, 1, 32, pidr2);
	init_mmio_region(GICR_IGROUPR0, GICR_FRAME_SGI, 1, 32, igroupr0);
	init_mmio_region(GICR_ISENABLER0, GICR_FRAME_SGI, 1, 32, isenabler0);
	init_mmio_region(GICR_ICENABLER0, GICR_FRAME_SGI, 1, 32, icenabler0);

#define	init_mmio_region_base(addr, frame, num, regbits, shortname)	\
	hyp->vgic_mmio_regions[VGIC_ ##addr] = (struct vgic_mmio_region){ \
		.start 	= redist->start + frame + addr ##_BASE,		\
		.end 	= redist->start + frame + addr ##_BASE + num * (regbits / 8), \
		.read 	= redist_ ##shortname ##_read,			\
		.write 	= redist_ ##shortname ##_write,			\
	}
	init_mmio_region_base(GICR_IPRIORITYR, GICR_FRAME_SGI,
	    VGIC_PRV_I_NUM / 4, 32, ipriorityr);
	init_mmio_region_base(GICR_ICFGR0, GICR_FRAME_SGI, 1, 32, icfgr0);
	init_mmio_region_base(GICR_ICFGR1, GICR_FRAME_SGI, 1, 32, icfgr1);
}

void
redist_mmio_destroy(struct hypctx *hypctx)
{
	return;
}
