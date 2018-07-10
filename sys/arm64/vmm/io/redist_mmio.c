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

static void
redist_update_int_group(uint32_t new_igroupr, uint32_t old_igroupr, uint32_t irq,
    struct hyp *hyp, int vcpuid)
{
	uint32_t irq_mask;
	int i;
	uint8_t group;

	irq_mask = 0x1;
	for (i = 0; i < 32; i++) {
		if ((old_igroupr & irq_mask) != (new_igroupr & irq_mask)) {
			group = (uint8_t)((new_igroupr >> i) & 0x1);
			vgic_v3_irq_set_group(irq, group, hyp, vcpuid);
		}
		irq++;
		irq_mask <<= 1;
	}
}


static int
redist_igroupr0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	struct vgic_v3_redist *redist = &hyp->ctx[vcpuid].vgic_redist;
	bool *retu = arg;

	redist_update_int_group((uint32_t)wval, redist->gicr_igroupr0, 0,
	    hyp, vcpuid);
	redist->gicr_igroupr0 = (uint32_t)wval;

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
	uint32_t irq, old_regval;
	uint8_t new_prio;

	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICR_IPRIORITYR].start;
	regsize = sizeof(*redist->gicr_ipriorityr);
	n = off / regsize;

	if (dir == READ) {
		*val = redist->gicr_ipriorityr[n];
	} else {
		old_regval = redist->gicr_ipriorityr[n];
		if ((old_regval & 0xff) != (*val & 0xff)) {
			irq = n * 4 + 0;
			new_prio = (uint8_t)(*val);
			vgic_v3_irq_set_priority(irq, new_prio, hyp, vcpuid);
		}

		if (((old_regval >> 8) & 0xff) != ((*val >> 8) & 0xff)) {
			irq = n * 4 + 1;
			new_prio = (uint8_t)(*val >> 8);
			vgic_v3_irq_set_priority(irq, new_prio, hyp, vcpuid);
		}

		if (((old_regval >> 16) & 0xff) != ((*val >> 16) & 0xff)) {
			irq = n * 4 + 2;
			new_prio = (uint8_t)(*val >> 16);
			vgic_v3_irq_set_priority(irq, new_prio, hyp, vcpuid);
		}

		if (((old_regval >> 24) & 0xff) != ((*val >> 24) & 0xff)) {
			irq = n * 4 + 2;
			new_prio = (uint8_t)(*val >> 24);
			vgic_v3_irq_set_priority(irq, new_prio, hyp, vcpuid);
		}

		redist->gicr_ipriorityr[n] = *val;
	}

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

	KASSERT(hyp->vgic_mmio_regions != NULL,
	    ("vgic_mmio_regions not allocated"));

#define	init_mmio_region(name, addr, frame, size, accessor)		\
	hyp->vgic_mmio_regions[name] = (struct vgic_mmio_region) { 	\
		.start 	= redist->start + frame + addr,			\
		.end 	= redist->start + frame + addr + size,		\
		.read 	= accessor ##_read,				\
		.write 	= accessor ##_write,				\
	};

	init_mmio_region(VGIC_GICR_CTLR, GICR_CTLR, GICR_FRAME_RD,
	    sizeof(redist->gicr_ctlr), redist_ctlr);

	init_mmio_region(VGIC_GICR_TYPER, GICR_TYPER, GICR_FRAME_RD,
	    sizeof(redist->gicr_typer), redist_typer);

	init_mmio_region(VGIC_GICR_WAKER, GICR_WAKER, GICR_FRAME_RD,
	    4, redist_waker);

	init_mmio_region(VGIC_GICR_PIDR2, GICR_PIDR2, GICR_FRAME_RD,
	    4, redist_pidr2);

	init_mmio_region(VGIC_GICR_IGROUPR0, GICR_IGROUPR0, GICR_FRAME_SGI,
	    sizeof(redist->gicr_igroupr0), redist_igroupr0);

	init_mmio_region(VGIC_GICR_ISENABLER0, GICR_ISENABLER0, GICR_FRAME_SGI,
	    sizeof(redist->gicr_ixenabler0), redist_isenabler0);

	init_mmio_region(VGIC_GICR_ICENABLER0, GICR_ICENABLER0, GICR_FRAME_SGI,
	    sizeof(redist->gicr_ixenabler0), redist_icenabler0);

	init_mmio_region(VGIC_GICR_IPRIORITYR, GICR_IPRIORITYR_BASE, GICR_FRAME_SGI,
	    sizeof(redist->gicr_ipriorityr), redist_ipriorityr);

	init_mmio_region(VGIC_GICR_ICFGR0, GICR_ICFGR0_BASE, GICR_FRAME_SGI,
	    sizeof(redist->gicr_icfgr0), redist_icfgr0);

	init_mmio_region(VGIC_GICR_ICFGR1, GICR_ICFGR1_BASE, GICR_FRAME_SGI,
	    sizeof(redist->gicr_icfgr1), redist_icfgr1);
}

void
redist_mmio_destroy(struct hypctx *hypctx)
{
	return;
}
