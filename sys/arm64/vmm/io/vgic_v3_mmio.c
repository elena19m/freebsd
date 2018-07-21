#include <sys/malloc.h>

#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>
#include <arm64/vmm/arm64.h>

#include "vgic_v3.h"

#define	GICR_FRAME_RD	0
#define	GICR_FRAME_SGI	GICR_RD_BASE_SIZE

#define	RES0	(0UL)

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

/* The names should always be in ascending order of memory address */
enum vgic_mmio_region_name {
	/* Distributor registers */
	VGIC_GICD_CTLR,
	VGIC_GICD_TYPER,
	VGIC_GICD_IGROUPR,
	VGIC_GICD_ISENABLER,
	VGIC_GICD_ICENABLER,
	VGIC_GICD_IPRIORITYR,
	VGIC_GICD_ICFGR,
	VGIC_GICD_IROUTER,
	VGIC_GICD_PIDR2,
	/* Redistributor registers */
	VGIC_GICR_CTLR,
	VGIC_GICR_TYPER,
	VGIC_GICR_WAKER,
	VGIC_GICR_PIDR2,
	VGIC_GICR_IGROUPR0,
	VGIC_GICR_ISENABLER0,
	VGIC_GICR_ICENABLER0,
	VGIC_GICR_IPRIORITYR,
	VGIC_GICR_ICFGR0,
	VGIC_GICR_ICFGR1,
	VGIC_MMIO_REGIONS_NUM,
};
/*
 * Necessary for calculating the number of Distributor and Redistributor
 * regions emulated.
 */
#define	FIRST_REDIST_MMIO_REGION	VGIC_GICR_CTLR;

enum access_type {
	READ,
	WRITE,
};

MALLOC_DEFINE(M_DIST_MMIO, "ARM VMM VGIC DIST MMIO", "ARM VMM VGIC DIST MMIO");

static int
dist_ctlr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	bool *retu = arg;

	mtx_lock_spin(&dist->dist_mtx);
	*rval = dist->gicd_ctlr;
	mtx_unlock_spin(&dist->dist_mtx);
	/* Writes are never pending */
	*rval &= ~GICD_CTLR_RWP;

	*retu = false;
	return (0);
}

static int
dist_ctlr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	bool *retu = arg;

	mtx_lock_spin(&dist->dist_mtx);

	if ((dist->gicd_ctlr & GICD_CTLR_G1A) != (wval & GICD_CTLR_G1A)) {
		if (!(wval & GICD_CTLR_G1A))
			vgic_v3_irq_toggle_group_enabled(1, false, hyp);
		else
			vgic_v3_irq_toggle_group_enabled(1, true, hyp);
	}
	if ((dist->gicd_ctlr & GICD_CTLR_G1) != (wval & GICD_CTLR_G1)) {
		if (!(wval & GICD_CTLR_G1))
			vgic_v3_irq_toggle_group_enabled(0, false, hyp);
		else
			vgic_v3_irq_toggle_group_enabled(0, true, hyp);
	}
	dist->gicd_ctlr = wval;

	mtx_unlock_spin(&dist->dist_mtx);

	*retu = false;
	return (0);
}

static int
dist_typer_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	bool *retu = arg;

	*rval = dist->gicd_typer;

	*retu = false;
	return (0);
}

static int
dist_typer_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	eprintf("Warning: Attempted write to read-only register GICD_TYPER.\n");

	*retu = false;
	return (0);
}

static void
dist_update_int_group(uint32_t new_igroupr, uint32_t old_igroupr, uint32_t irq,
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
dist_igroupr_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_redist *redist;
	uint32_t *regp;
	size_t regsize, n;
	size_t off;

	redist = &hyp->ctx[vcpuid].vgic_redist;
	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICD_IGROUPR].start;
	regsize = sizeof(*dist->gicd_igroupr);
	n = off / regsize;

	if (n == 0)
		regp = &redist->gicr_igroupr0;
	else
		regp = &dist->gicd_igroupr[n];

	mtx_lock_spin(&dist->dist_mtx);
	if (dir == READ) {
		*val = *regp;
	} else {
		dist_update_int_group(*val, *regp, n * 32, hyp, vcpuid);
		*regp = *val;
	}
	mtx_unlock_spin(&dist->dist_mtx);

	*retu = false;
	return (0);
}

static int
dist_igroupr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_igroupr_access(hyp, vcpuid, fault_ipa, rval, retu, READ);

	return (error);
}

static int
dist_igroupr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_igroupr_access(hyp, vcpuid, fault_ipa, &wval, retu, WRITE);

	return (error);
}

static void
dist_update_int_enabled(uint32_t new_ixenabler, uint32_t old_ixenabler,
    uint32_t irq, struct hyp *hyp, int vcpuid)
{
	uint32_t irq_mask;
	int error;
	int i;
	bool enabled;

	for (i = 0, irq_mask = 0x1; i < 32; i++, irq++, irq_mask <<= 1)
		if ((old_ixenabler & irq_mask) != (new_ixenabler & irq_mask)) {
			enabled = ((new_ixenabler & irq_mask) != 0);
			error = vgic_v3_irq_toggle_enabled(irq, enabled,
			    hyp, vcpuid);
			if (error)
				eprintf("Warning: error while toggling IRQ %u\n", irq);
		}
}

static int
dist_ixenabler_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir,
    enum vgic_mmio_region_name name)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_redist *redist;
	uint32_t *regp;
	uint32_t old_ixenabler;
	size_t off;
	size_t regsize, n;

	redist = &hyp->ctx[vcpuid].vgic_redist;
	off = fault_ipa - hyp->vgic_mmio_regions[name].start;
	regsize = sizeof(*dist->gicd_ixenabler);
	n = off / regsize;

	if (n == 0)
		/* GICD_ICENABLER0 is equivalent to GICR_ICENABLER0 */
		regp = &redist->gicr_ixenabler0;
	else
		regp = &dist->gicd_ixenabler[n];

	mtx_lock_spin(&dist->dist_mtx);
	if (dir == READ) {
		*val = *regp;
	} else {
		old_ixenabler = *regp;
		if (name == VGIC_GICD_ICENABLER)
			*regp &= ~(*val);
		else
			*regp |= *val;
		dist_update_int_enabled(*regp, old_ixenabler, n * 32,
		    hyp, vcpuid);
	}
	mtx_unlock_spin(&dist->dist_mtx);

	*retu = false;
	return (0);
}

static int
dist_isenabler_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_ixenabler_access(hyp, vcpuid, fault_ipa, rval, retu,
	    READ, VGIC_GICD_ISENABLER);

	return (error);
}

static int
dist_isenabler_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_ixenabler_access(hyp, vcpuid, fault_ipa, &wval, retu,
	    WRITE, VGIC_GICD_ISENABLER);

	return (error);
}

static int
dist_icenabler_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_ixenabler_access(hyp, vcpuid, fault_ipa, rval, retu,
	    READ, VGIC_GICD_ICENABLER);

	return (error);
}

static int
dist_icenabler_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_ixenabler_access(hyp, vcpuid, fault_ipa, &wval, retu,
	    WRITE, VGIC_GICD_ICENABLER);

	return (error);
}

static void
dist_update_int_priority(uint32_t new_ipriorityr, uint32_t old_ipriorityr,
    uint32_t irq, struct hyp *hyp, int vcpuid)
{
	uint32_t irq_mask;
	int i;
	uint8_t new_prio;

	irq_mask = 0xff;
	for (i = 0; i < 4; i++) {
		if ((old_ipriorityr & irq_mask) != (new_ipriorityr & irq_mask)) {
			new_prio = (uint8_t)((new_ipriorityr >> (i * 8)) & 0xff);
			vgic_v3_irq_set_priority(irq, new_prio, hyp, vcpuid);
		}
		irq++;
		irq_mask <<= 8;
	}
}

/* TODO: Registers are byte accessible. */
static int
dist_ipriorityr_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	size_t regsize, n;
	size_t off;

	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICD_IPRIORITYR].start;
	regsize = sizeof(*dist->gicd_ipriorityr);
	n = off / regsize;
	/*
	 * GIC Architecture specification, p 8-483: when affinity
	 * routing is enabled, GICD_IPRIORITYR<n> is RAZ/WI for
	 * n = 0 to 7.
	 */
	if (dist->gicd_ctlr & GICD_CTLR_ARE_NS && n <= 7) {
		if (dir == READ)
			*val = RES0;
		/* Ignore writes, fall through */
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	if (dir == READ) {
		*val = dist->gicd_ipriorityr[n];
	} else {
		dist_update_int_priority(*val, dist->gicd_ipriorityr[n],
		    n * 4, hyp, vcpuid);
		dist->gicd_ipriorityr[n] = *val;
	}
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_ipriorityr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_ipriorityr_access(hyp, vcpuid, fault_ipa, rval, retu,
	    READ);

	return (error);
}

static int
dist_ipriorityr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_ipriorityr_access(hyp, vcpuid, fault_ipa, &wval, retu,
	    WRITE);

	return (error);
}

static int
_dist_icfgr_access(struct hyp *hyp, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	size_t regsize, off;
	size_t n;

	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICD_ICFGR].start;
	regsize = sizeof(*dist->gicd_icfgr);
	n = off / regsize;

	/*
	 * ARM GIC Architecture Specification, p 8-472: "For SGIs,
	 * Int_config fields are RO, meaning that GICD_ICFGR0 is RO."
	 */
	if (n == 0) {
		if (dir == READ)
			*val = RES0;
		else
			/* Ignore writes */
			eprintf("Warning: Write to read-only register GICD_ICFGR0.\n");
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	if (dir == READ)
		*val = dist->gicd_icfgr[n];
	else
		dist->gicd_icfgr[n] = *val;
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_icfgr_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir)
{
	struct vgic_v3_redist *redist;
	uint32_t *regp;

	if (fault_ipa == hyp->vgic_mmio_regions[VGIC_GICR_ICFGR0].start) {
		redist = &hyp->ctx[vcpuid].vgic_redist;
		regp = &redist->gicr_icfgr0;
	} else if (fault_ipa == hyp->vgic_mmio_regions[VGIC_GICR_ICFGR1].start) {
		redist = &hyp->ctx[vcpuid].vgic_redist;
		regp = &redist->gicr_icfgr1;
	} else {
		return (_dist_icfgr_access(hyp, fault_ipa, val, retu, dir));
	}

	if (dir == READ)
		*val = *regp;
	else
		 *regp = *val;

	*retu = false;
	return (0);
}

static int
dist_icfgr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_icfgr_access(hyp, vcpuid, fault_ipa, rval, retu, READ);

	return (error);

}

static int
dist_icfgr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_icfgr_access(hyp, vcpuid, fault_ipa, &wval, retu, WRITE);

	return (error);
}

static int
redist_icfgr0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_icfgr_access(hyp, vcpuid, fault_ipa, rval, retu, READ);
	eprintf("\n");

	return (error);
}

static int
redist_icfgr0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_icfgr_access(hyp, vcpuid, fault_ipa, &wval, retu, WRITE);
	eprintf("\n");

	return (0);
}

static int
redist_icfgr1_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_icfgr_access(hyp, vcpuid, fault_ipa, rval, retu, READ);
	eprintf("\n");

	return (error);
}

static int
redist_icfgr1_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_icfgr_access(hyp, vcpuid, fault_ipa, &wval, retu, WRITE);
	eprintf("\n");

	return (0);
}

static int
dist_irouter_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	size_t regsize, n;
	size_t off;

	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICD_IROUTER].start;
	regsize = sizeof(*dist->gicd_irouter);
	n = off / regsize;

	/* GIC Architecture Manual, p 8-485: registers 0 to 31 are reserved */
	if (n <= 31) {
		if (dir == READ) {
			eprintf("Warning: Read from register GICD_IROUTER%zu\n", n);
			*val = RES0;
		} else {
			eprintf("Warning: Write to register GICD_IROUTER%zu\n", n);
		}
		goto out;
	}

	/*
	 * GIC Architecture Manual, p 8-485: when affinity routing is not
	 * enabled, the registers are RAZ/WI.
	 */
	if (!(dist->gicd_ctlr & GICD_CTLR_ARE_NS)) {
		if (dir == READ)
			*val = RES0;
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	if (dir == READ)
		*val = dist->gicd_irouter[n];
	else
		dist->gicd_irouter[n] = *val;
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_irouter_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{

	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;

	return (dist_irouter_access(hyp, vcpuid, fault_ipa, rval, retu, READ));
}

static int
dist_irouter_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;

	return (dist_irouter_access(hyp, vcpuid, fault_ipa, &wval, retu, WRITE));
}

static int
dist_pidr2_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	bool *retu = arg;

	*rval = dist->gicd_pidr2;

	*retu = false;
	return (0);
}

static int
dist_pidr2_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	eprintf("Warning: Attempted write to read-only register GICD_PIDR2.\n");

	*retu = false;
	return (0);
}

static int
redist_ctlr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_ctlr, rval, vm, vcpuid);
	/* Writes are never pending */
	*rval &= ~GICR_CTLR_RWP & ~GICR_CTLR_UWP;

	eprintf("\n");

	*retu = false;
	return (0);
}

static int
redist_ctlr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_write(wval, gicr_ctlr, vm, vcpuid);

	eprintf("\n");

	*retu = false;
	return (0);
}

static int
redist_typer_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_typer, rval, vm, vcpuid);

	eprintf("\n");

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

	eprintf("\n");

	*retu = false;
	return (0);
}

static int
redist_waker_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	/* Ignore writes */
	eprintf("\n");

	*retu = false;
	return (0);
}

static int
redist_igroupr0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_igroupr0, rval, vm, vcpuid);
	eprintf("\n");

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

	eprintf("\n");

	*retu = false;
	return (0);
}

static void
redist_update_int_enabled(uint32_t new_ixenabler, uint32_t old_ixenabler,
    struct hyp *hyp, int vcpuid)
{
	uint32_t irq, irq_mask;
	int error;
	bool enabled;

	for (irq = 0, irq_mask = 0x1; irq < 32; irq++, irq_mask <<= 1) {
		if ((old_ixenabler & irq_mask) != (new_ixenabler & irq_mask)) {
			enabled = ((new_ixenabler & irq_mask) != 0);
			error = vgic_v3_irq_toggle_enabled(irq, enabled,
			    hyp, vcpuid);
			if (error)
				eprintf("Warning: error while toggling IRQ %u\n", irq);
		}
	}
}

static int
redist_ixenabler_access(void *vm, int vcpuid, uint64_t *val,
    enum access_type dir, bool clear)
{
	struct hyp *hyp = vm_get_cookie(vm);
	struct vgic_v3_redist *redist = &hyp->ctx[vcpuid].vgic_redist;
	uint32_t old_ixenabler0, new_ixenabler0;

	if (dir == READ) {
		*val = redist->gicr_ixenabler0;
	} else {
		old_ixenabler0 = redist->gicr_ixenabler0;
		if (clear)
			new_ixenabler0 = old_ixenabler0 & ~(*val);
		else
			new_ixenabler0 = old_ixenabler0 | *val;
		redist_update_int_enabled(new_ixenabler0, old_ixenabler0,
		    hyp, vcpuid);
		redist->gicr_ixenabler0 = new_ixenabler0;
	}

	return (0);
}

static int
redist_isenabler0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	int error;
	bool *retu = arg;

	error = redist_ixenabler_access(vm, vcpuid, rval, READ, false);
	eprintf("\n");

	*retu = false;
	return (error);
}

static int
redist_isenabler0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	int error;
	bool *retu = arg;

	error = redist_ixenabler_access(vm, vcpuid, &wval, WRITE, false);
	eprintf("\n");

	*retu = false;
	return (error);
}

static int
redist_icenabler0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	int error;
	bool *retu = arg;

	error = redist_ixenabler_access(vm, vcpuid, rval, READ, false);
	eprintf("\n");

	*retu = false;
	return (error);
}

static int
redist_icenabler0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	int error;
	bool *retu = arg;

	error = redist_ixenabler_access(vm, vcpuid, &wval, WRITE, true);
	eprintf("\n");

	*retu = false;
	return (error);

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
	eprintf("\n");

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
	eprintf("\n");

	return (error);
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
	eprintf("\n");

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

#define	alloc_registers(regs, num, size)				\
do {									\
	size = n * sizeof(*regs);					\
	regs = malloc(size, M_DIST_MMIO, M_WAITOK | M_ZERO);		\
} while (0)

#define	div_round_up(n, div)	(((n) + (div) - 1) / (div))

static inline void
init_mmio_region(struct hyp *hyp, size_t regidx, vm_offset_t start,
    size_t size, mem_region_read_t read_fn, mem_region_write_t write_fn)
{
	hyp->vgic_mmio_regions[regidx] = (struct vgic_mmio_region) {
		.start	= start,
		.end 	= start + size,
		.read	= read_fn,
		.write	= write_fn,
	};
}

static void
dist_mmio_init_regions(struct vgic_v3_dist *dist, struct hyp *hyp)
{
	size_t n;
	size_t region_size;

	init_mmio_region(hyp, VGIC_GICD_CTLR, dist->start +  GICD_CTLR,
	    sizeof(dist->gicd_ctlr), dist_ctlr_read, dist_ctlr_write);
	init_mmio_region(hyp, VGIC_GICD_TYPER, dist->start + GICD_TYPER,
	    sizeof(dist->gicd_typer), dist_typer_read, dist_typer_write);

	n = div_round_up(dist->nirqs, 32);
	alloc_registers(dist->gicd_igroupr, n, region_size);
	init_mmio_region(hyp, VGIC_GICD_IGROUPR, dist->start + GICD_IGROUPR_BASE,
	    region_size, dist_igroupr_read, dist_igroupr_write);


	/* ARM GIC Architecture Specification, page 8-471. */
	n = (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1;
	alloc_registers(dist->gicd_ixenabler, n , region_size);
	init_mmio_region(hyp, VGIC_GICD_ISENABLER, dist->start + GICD_ISENABLER_BASE,
	    region_size, dist_isenabler_read, dist_isenabler_write);
	init_mmio_region(hyp, VGIC_GICD_ICENABLER, dist->start +  GICD_ICENABLER_BASE,
	    region_size, dist_icenabler_read, dist_icenabler_write);

	/* ARM GIC Architecture Specification, page 8-483. */
	n = 8 * ((dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1);
	alloc_registers(dist->gicd_ipriorityr, n, region_size);
	init_mmio_region(hyp, VGIC_GICD_IPRIORITYR, dist->start + GICD_IPRIORITYR_BASE,
	    region_size, dist_ipriorityr_read, dist_ipriorityr_write);

	n = div_round_up(dist->nirqs, 16);
	alloc_registers(dist->gicd_icfgr, n, region_size);
	init_mmio_region(hyp, VGIC_GICD_ICFGR, dist->start + GICD_ICFGR_BASE,
	    region_size, dist_icfgr_read, dist_icfgr_write);

	n = div_round_up(dist->nirqs, 32);
	alloc_registers(dist->gicd_igroupr, n, region_size);
	init_mmio_region(hyp, VGIC_GICD_IGROUPR, dist->start + GICD_IGROUPR_BASE,
	    region_size, dist_igroupr_read, dist_igroupr_write);

	/* ARM GIC Architecture Specification, page 8-485. */
	n = 32 * (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK + 1) - 1;
	alloc_registers(dist->gicd_irouter, n, region_size);
	init_mmio_region(hyp, VGIC_GICD_IROUTER, dist->start + GICD_IROUTER_BASE,
	    region_size, dist_irouter_read, dist_irouter_write);

	init_mmio_region(hyp, VGIC_GICD_PIDR2, dist->start + GICD_PIDR2,
	    sizeof(dist->gicd_pidr2), dist_pidr2_read, dist_pidr2_write);
}

static void
redist_mmio_init_regions(struct vgic_v3_redist *redist, struct hyp *hyp)
{
	vm_offset_t start;

	start = redist->start + GICR_FRAME_RD + GICR_CTLR;
	init_mmio_region(hyp, VGIC_GICR_CTLR, start, sizeof(redist->gicr_ctlr),
	    redist_ctlr_read, redist_ctlr_write);

	start = redist->start + GICR_FRAME_RD + GICR_TYPER;
	init_mmio_region(hyp, VGIC_GICR_TYPER, start, sizeof(redist->gicr_typer),
	    redist_typer_read, redist_typer_write);

	start = redist->start + GICR_FRAME_RD + GICR_WAKER;
	init_mmio_region(hyp, VGIC_GICR_WAKER, start, 4, redist_waker_read,
	    redist_waker_write);

	start = redist->start + GICR_FRAME_RD + GICR_PIDR2;
	init_mmio_region(hyp, VGIC_GICR_PIDR2, start, 4, redist_pidr2_read,
	    redist_pidr2_write);

	start = redist->start + GICR_FRAME_SGI + GICR_IGROUPR0;
	init_mmio_region(hyp, VGIC_GICR_IGROUPR0, start,
	    sizeof(redist->gicr_igroupr0), redist_igroupr0_read, redist_igroupr0_write);

	start = redist->start + GICR_FRAME_SGI + GICR_ISENABLER0;
	init_mmio_region(hyp, VGIC_GICR_ISENABLER0, start,
	    sizeof(redist->gicr_ixenabler0), redist_isenabler0_read,
	    redist_isenabler0_write);

	start = redist->start + GICR_FRAME_SGI + GICR_ICENABLER0;
	init_mmio_region(hyp, VGIC_GICR_ICENABLER0, start,
	    sizeof(redist->gicr_ixenabler0), redist_icenabler0_read,
	    redist_icenabler0_write);

	start = redist->start + GICR_FRAME_SGI + GICR_IPRIORITYR_BASE;
	init_mmio_region(hyp, VGIC_GICR_IPRIORITYR, start,
	    sizeof(redist->gicr_ipriorityr), redist_ipriorityr_read,
	    redist_ipriorityr_write);

	start = redist->start + GICR_FRAME_SGI + GICR_ICFGR0_BASE;
	init_mmio_region(hyp, VGIC_GICR_ICFGR0, start,
	    sizeof(redist->gicr_icfgr0), redist_icfgr0_read, redist_icfgr0_write);

	start = redist->start + GICR_FRAME_SGI + GICR_ICFGR1_BASE;
	init_mmio_region(hyp, VGIC_GICR_ICFGR1, start,
	    sizeof(redist->gicr_icfgr1), redist_icfgr1_read, redist_icfgr1_write);
}

void
vgic_v3_mmio_init(struct hyp *hyp)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_redist *redist;
	int redist_region_num, dist_region_num, region_num;
	int ncpus;

	ncpus = 1;
	dist_region_num = FIRST_REDIST_MMIO_REGION;
	redist_region_num = \
	    ncpus * (VGIC_MMIO_REGIONS_NUM - FIRST_REDIST_MMIO_REGION);
	region_num = dist_region_num + redist_region_num;

	hyp->vgic_mmio_regions = \
	    malloc(region_num * sizeof(*hyp->vgic_mmio_regions),
	    M_DIST_MMIO, M_WAITOK | M_ZERO);
	hyp->vgic_mmio_regions_num = region_num;

	dist_mmio_init_regions(dist, hyp);

	/* TODO: Do it for all VCPUs */
	redist = &hyp->ctx[0].vgic_redist;
	redist_mmio_init_regions(redist, hyp);
}

void
vgic_v3_mmio_destroy(struct hyp *hyp)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;

	if (!hyp->vgic_mmio_regions)
		return;
	free(hyp->vgic_mmio_regions, M_DIST_MMIO);

	free(dist->gicd_igroupr, M_DIST_MMIO);
	free(dist->gicd_ixenabler, M_DIST_MMIO);
	free(dist->gicd_ipriorityr, M_DIST_MMIO);
	free(dist->gicd_icfgr, M_DIST_MMIO);
	free(dist->gicd_irouter, M_DIST_MMIO);
}
