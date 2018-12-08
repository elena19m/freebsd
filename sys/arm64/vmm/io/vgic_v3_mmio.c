#include <sys/malloc.h>

#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>
#include <arm64/vmm/arm64.h>

#include "vgic_v3.h"

#define	DEBUG 0

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

#define	reg32_idx(ipa, region)		(((ipa) - (region).start) / 4)
#define	reg64_idx(ipa, region)		(((ipa) - (region).start) / 8)

#define	reg_changed(new, old, mask)	(((new) & (mask)) != ((old) & (mask)))

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
#define	FIRST_REDIST_MMIO_REGION	VGIC_GICR_CTLR

MALLOC_DEFINE(M_VGIC_V3_MMIO, "ARM VMM VGIC DIST MMIO", "ARM VMM VGIC DIST MMIO");

static int
dist_ctlr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

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
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	mtx_lock_spin(&dist->dist_mtx);

	if (reg_changed(wval, dist->gicd_ctlr, GICD_CTLR_G1A)) {
		if (!(wval & GICD_CTLR_G1A))
			vgic_v3_irq_toggle_group_enabled(false, hyp);
		else
			vgic_v3_irq_toggle_group_enabled(true, hyp);
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
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

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
mmio_update_int_group(uint32_t new_igroupr, uint32_t old_igroupr,
    uint32_t irq, struct hyp *hyp, int vcpuid)
{
	uint32_t irq_mask;
	int i;
	uint8_t group;

	irq_mask = 0x1;
	for (i = 0; i < 32; i++) {
		if (reg_changed(new_igroupr, old_igroupr, irq_mask)) {
			group = (uint8_t)((new_igroupr >> i) & 0x1);
			vgic_v3_irq_set_group(irq, group, hyp, vcpuid);
		}
		irq++;
		irq_mask <<= 1;
	}
}

static int
dist_igroupr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	size_t n;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_IGROUPR]);
	/*
	 * GIC Architecture specification, p 8-477: "When ARE is 1 for the
	 * Security state of an interrupt, the field for that interrupt is RES0
	 * and an implementation is permitted to* make the field RAZ/WI in this
	 * case".
	 */
	if (n == 0 && aff_routing_en(dist)) {
		*rval = RES0;
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	*rval = dist->gicd_igroupr[n];
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_igroupr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	size_t n;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_IGROUPR]);
	/* See dist_igroupr_read() */
	if (n == 0 && aff_routing_en(dist))
		/* Ignore writes */
		goto out;

	mtx_lock_spin(&dist->dist_mtx);
	mmio_update_int_group(wval, dist->gicd_igroupr[n], n * 32, hyp, vcpuid);
	dist->gicd_igroupr[n] = wval;
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static void
mmio_update_int_enabled(uint32_t new_ixenabler, uint32_t old_ixenabler,
    uint32_t irq, struct hyp *hyp, int vcpuid)
{
	uint32_t irq_mask;
	int error;
	int i;
	bool enabled;

	irq_mask = 0x1;
	for (i = 0; i < 32; i++) {
		if (reg_changed(new_ixenabler, old_ixenabler, irq_mask)) {
			enabled = ((new_ixenabler & irq_mask) != 0);
			error = vgic_v3_irq_toggle_enabled(irq, enabled,
			    hyp, vcpuid);
			if (error)
				eprintf("Warning: error while toggling IRQ %u\n", irq);
		}
		irq++;
		irq_mask <<= 1;
	}
}

static int
dist_ixenabler_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    void *arg, enum vgic_mmio_region_name name)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	size_t n;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[name]);
	/*
	 * GIC Architecture specification, p 8-471: "When ARE is 1 for the
	 * Security state of an interrupt, the field for that interrupt is RES0
	 * and an implementation is permitted to* make the field RAZ/WI in this
	 * case".
	 */
	if (n == 0 && aff_routing_en(dist)) {
		*rval = RES0;
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	*rval = dist->gicd_ixenabler[n];
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_ixenabler_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    void *arg, enum vgic_mmio_region_name name)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	uint32_t old_ixenabler;
	size_t n;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[name]);
	/* See dist_ixenabler_read() */
	if (n == 0 && aff_routing_en(dist))
		/* Ignore writes */
		goto out;

	mtx_lock_spin(&dist->dist_mtx);

	old_ixenabler = dist->gicd_ixenabler[n];
	if (name == VGIC_GICD_ICENABLER)
		dist->gicd_ixenabler[n] &= ~wval;
	else
		dist->gicd_ixenabler[n] |= wval;
	mmio_update_int_enabled(dist->gicd_ixenabler[n], old_ixenabler, n * 32,
	    hyp, vcpuid);

	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_isenabler_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	return (dist_ixenabler_read(vm, vcpuid, fault_ipa, rval, arg,
	    VGIC_GICD_ISENABLER));
}

static int
dist_isenabler_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	return (dist_ixenabler_write(vm, vcpuid, fault_ipa, wval, arg,
	    VGIC_GICD_ISENABLER));
}

static int
dist_icenabler_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	return (dist_ixenabler_read(vm, vcpuid, fault_ipa, rval, arg,
	    VGIC_GICD_ICENABLER));
}

static int
dist_icenabler_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	return (dist_ixenabler_write(vm, vcpuid, fault_ipa, wval, arg,
	    VGIC_GICD_ICENABLER));
}

static void
mmio_update_int_priority(uint32_t new_ipriorityr, uint32_t old_ipriorityr,
    uint32_t irq, struct hyp *hyp, int vcpuid)
{
	uint32_t irq_mask;
	int i;
	uint8_t new_prio;

	irq_mask = 0xff;
	for (i = 0; i < 4; i++) {
		if (reg_changed(new_ipriorityr, old_ipriorityr, irq_mask)) {
			new_prio = (uint8_t)((new_ipriorityr >> (i * 8)) & 0xff);
			vgic_v3_irq_set_priority(irq, new_prio, hyp, vcpuid);
		}
		irq++;
		irq_mask <<= 8;
	}
}

/* XXX: Registers are byte accessible. */
static int
dist_ipriorityr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;
	size_t n;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_IPRIORITYR]);
	/*
	 * GIC Architecture specification, p 8-483: when affinity
	 * routing is enabled, GICD_IPRIORITYR<n> is RAZ/WI for
	 * n = 0 to 7.
	 */
	if (aff_routing_en(dist) && n <= 7) {
		*rval = RES0;
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	*rval = dist->gicd_ipriorityr[n];
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);

}

static int
dist_ipriorityr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;
	size_t n;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_IPRIORITYR]);
	/* See dist_ipriorityr_read() */
	if (aff_routing_en(dist) && n <= 7)
		/* Ignore writes */
		goto out;

	mtx_lock_spin(&dist->dist_mtx);

	mmio_update_int_priority(wval, dist->gicd_ipriorityr[n], n * 4,
	    hyp, vcpuid);
	dist->gicd_ipriorityr[n] = wval;

	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_icfgr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;
	size_t n;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_ICFGR]);
	/*
	 * ARM GIC Architecture Specification, p 8-472: "For SGIs,
	 * Int_config fields are RO, meaning that GICD_ICFGR0 is RO."
	 */
	if (n == 0) {
		*rval = RES0;
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	*rval = dist->gicd_icfgr[n];
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);

}

static int
dist_icfgr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;
	size_t n;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_ICFGR]);
	if (n == 0)
		/* Ignore writes */
		goto out;

	mtx_lock_spin(&dist->dist_mtx);
	dist->gicd_icfgr[n] = wval;
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_irouter_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	size_t n;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg64_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_IROUTER]);
	/* GIC Architecture Manual, p 8-485: registers 0 to 31 are reserved */
	if (n <= 31) {
		eprintf("Warning: Read from register GICD_IROUTER%zu\n", n);
		*rval = RES0;
		goto out;
	}

	/*
	 * GIC Architecture Manual, p 8-485: when affinity routing is not
	 * enabled, the registers are RAZ/WI.
	 */
	if (!aff_routing_en(dist)) {
		*rval = RES0;
		goto out;
	}

	mtx_lock_spin(&dist->dist_mtx);
	*rval = dist->gicd_irouter[n];
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_irouter_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	size_t n;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	n = reg64_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICD_IROUTER]);
	if (n <= 31) {
		eprintf("Warning: Write to register GICD_IROUTER%zu\n", n);
		goto out;
	}

	/* See dist_irouter_read() */
	if (!aff_routing_en(dist))
		/* Ignore writes */
		goto out;

	mtx_lock_spin(&dist->dist_mtx);
	dist->gicd_irouter[n] = wval;
	mtx_unlock_spin(&dist->dist_mtx);

out:
	*retu = false;
	return (0);
}

static int
dist_pidr2_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

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

#if (DEBUG > 0)
	eprintf("\n");
#endif

	*retu = false;
	return (0);
}

static int
redist_ctlr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_write(wval, gicr_ctlr, vm, vcpuid);

#if (DEBUG > 0)
	eprintf("\n");
#endif

	*retu = false;
	return (0);
}

static int
redist_typer_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_read(gicr_typer, rval, vm, vcpuid);

#if (DEBUG > 0)
	eprintf("\n");
#endif

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

#if (DEBUG > 0)
	eprintf("\n");
#endif

	*retu = false;
	return (0);
}

static int
redist_waker_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	/* Ignore writes */
#if (DEBUG > 0)
	eprintf("\n");
#endif

	*retu = false;
	return (0);
}

static int
redist_igroupr0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	*rval = redist->gicr_igroupr0;

#if (DEBUG > 0)
	eprintf("\n");
#endif

	*retu = false;
	return (0);
}

static int
redist_igroupr0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	mmio_update_int_group((uint32_t)wval, redist->gicr_igroupr0, 0,
	    hyp, vcpuid);
	redist->gicr_igroupr0 = (uint32_t)wval;

#if (DEBUG > 0)
	eprintf("\n");
#endif

	*retu = false;
	return (0);
}

static int
redist_ixenabler_read(void *vm, int vcpuid, uint64_t *rval, void *arg,
    enum vgic_mmio_region_name reg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	*rval = redist->gicr_ixenabler0;

	*retu = false;
	return (0);
}

static int
redist_ixenabler_write(void *vm, int vcpuid, uint64_t wval, void *arg,
    enum vgic_mmio_region_name reg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	uint32_t old_ixenabler0, new_ixenabler0;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	old_ixenabler0 = redist->gicr_ixenabler0;
	if (reg == VGIC_GICR_ICENABLER0)
		new_ixenabler0 = old_ixenabler0 & ~wval;
	else
		new_ixenabler0 = old_ixenabler0 | wval;
	mmio_update_int_enabled(new_ixenabler0, old_ixenabler0, 0, hyp, vcpuid);
	redist->gicr_ixenabler0 = new_ixenabler0;

	*retu = false;
	return (0);
}


static int
redist_isenabler0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
#if (DEBUG > 0)
	eprintf("\n");
#endif
	return (redist_ixenabler_read(vm, vcpuid, rval, arg,
	    VGIC_GICR_ISENABLER0));
}

static int
redist_isenabler0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
#if (DEBUG > 0)
	eprintf("\n");
#endif
	return (redist_ixenabler_write(vm, vcpuid, wval, arg,
	    VGIC_GICR_ISENABLER0));
}

static int
redist_icenabler0_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
#if (DEBUG > 0)
	eprintf("\n");
#endif
	return (redist_ixenabler_read(vm, vcpuid, rval, arg,
	    VGIC_GICR_ICENABLER0));
}

static int
redist_icenabler0_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
#if (DEBUG > 0)
	eprintf("\n");
#endif
	return (redist_ixenabler_write(vm, vcpuid, wval, arg,
	    VGIC_GICR_ICENABLER0));
}

static int
redist_ipriorityr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	size_t n;
	bool *retu = arg;

#if (DEBUG > 0)
	eprintf("\n");
#endif

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICR_IPRIORITYR]);
	*rval = redist->gicr_ipriorityr[n];

	*retu = false;
	return (0);
}

static int
redist_ipriorityr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_redist *redist;
	size_t n;
	uint32_t old_ipriorityr;
	bool *retu = arg;

#if (DEBUG > 0)
	eprintf("\n");
#endif

	hyp = vm_get_cookie(vm);
	redist = &hyp->ctx[vcpuid].vgic_redist;

	n = reg32_idx(fault_ipa, hyp->vgic_mmio_regions[VGIC_GICR_IPRIORITYR]);
	old_ipriorityr = redist->gicr_ipriorityr[n];
	mmio_update_int_priority(wval, old_ipriorityr, n * 4, hyp, vcpuid);
	redist->gicr_ipriorityr[n] = wval;

	*retu = false;
	return (0);
}

static int
redist_pidr2_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	struct hyp *hyp;
	struct vgic_v3_dist *dist;
	bool *retu = arg;

	hyp = vm_get_cookie(vm);
	dist = &hyp->vgic_dist;

	/* GICR_PIDR2 has the same value as GICD_PIDR2 */
	*rval = dist->gicd_pidr2;
#if (DEBUG > 0)
	eprintf("\n");
#endif

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

	redist_simple_read(gicr_icfgr0, rval, vm, vcpuid);

	*retu = false;
	return (0);
}

static int
redist_icfgr1_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	redist_simple_write(wval, gicr_icfgr0, vm, vcpuid);

	*retu = false;
	return (0);
}

#define	alloc_registers(regs, num, size)				\
do {									\
	size = n * sizeof(*regs);					\
	regs = malloc(size, M_VGIC_V3_MMIO, M_WAITOK | M_ZERO);		\
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
redist_mmio_init_regions(struct hyp *hyp, int vcpuid)
{
	struct vgic_v3_redist *redist;
	vm_offset_t start;

	redist = &hyp->ctx[vcpuid].vgic_redist;
	start = redist->start + GICR_FRAME_RD + GICR_CTLR;
	/*
	hyp->vgic_mmio_regions[VGIC_GICR_CTLR] = (struct vgic_mmio_region) {
		.start 	= start,
		.end	= start + sizeof(redist->gicr_ctlr),
		.read	= redist_ctlr_read,
		.write	= redist_ctlr_write,
	};
	*/
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
	int redist_region_num, dist_region_num, region_num;
	int ncpus = 1;

	dist_region_num = FIRST_REDIST_MMIO_REGION;
	redist_region_num = \
	    ncpus * (VGIC_MMIO_REGIONS_NUM - FIRST_REDIST_MMIO_REGION);
	region_num = dist_region_num + redist_region_num;

	hyp->vgic_mmio_regions = \
	    malloc(region_num * sizeof(*hyp->vgic_mmio_regions),
	    M_VGIC_V3_MMIO, M_WAITOK | M_ZERO);
	hyp->vgic_mmio_regions_num = region_num;

	dist_mmio_init_regions(dist, hyp);

	/* TODO: Do it for all VCPUs */
	redist_mmio_init_regions(hyp, 0);
}

void
vgic_v3_mmio_destroy(struct hyp *hyp)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;

	if (!hyp->vgic_mmio_regions)
		return;
	free(hyp->vgic_mmio_regions, M_VGIC_V3_MMIO);

	free(dist->gicd_igroupr, M_VGIC_V3_MMIO);
	free(dist->gicd_ixenabler, M_VGIC_V3_MMIO);
	free(dist->gicd_ipriorityr, M_VGIC_V3_MMIO);
	free(dist->gicd_icfgr, M_VGIC_V3_MMIO);
	free(dist->gicd_irouter, M_VGIC_V3_MMIO);
}
