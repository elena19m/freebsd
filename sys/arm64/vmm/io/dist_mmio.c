#include <sys/malloc.h>

#include <machine/vmm.h>
#include <machine/vmm_instruction_emul.h>
#include <arm64/vmm/arm64.h>

#include "vgic_v3.h"

enum access_type {
	READ,
	WRITE,
};

#define	RES0	(0UL)

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
	if (dir == READ)
		*val = *regp;
	else
		*regp = *val;
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

static int
dist_ixenabler_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir,
    enum vgic_mmio_region_name name)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	struct vgic_v3_redist *redist;
	uint32_t *regp;
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
		if (name == VGIC_GICD_ICENABLER)
			*regp &= ~(*val);
		else
			*regp |= *val;
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
	if (dir == READ)
		*val = dist->gicd_ipriorityr[n];
	else
		dist->gicd_ipriorityr[n] = *val;
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
dist_icfgr_access(struct hyp *hyp, int vcpuid, uint64_t fault_ipa,
    uint64_t *val, bool *retu, enum access_type dir)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	size_t regsize, n;
	size_t off;

	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICD_ICFGR].start;
	regsize = sizeof(*dist->gicd_icfgr);
	n = off / regsize;

	mtx_lock_spin(&dist->dist_mtx);
	if (dir == READ) {
		*val = dist->gicd_icfgr[n];
	} else {
		if (n == 0) {
			eprintf("Warning: Write to read-only register GICD_ICFGR0.\n");
			goto out;
		} else {
			dist->gicd_icfgr[n] = *val;
		}
	}
	mtx_unlock_spin(&dist->dist_mtx);

out:
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
	int error;

	error = dist_irouter_access(hyp, vcpuid, fault_ipa, rval, retu, READ);

	return (error);
}

static int
dist_irouter_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	struct hyp *hyp = vm_get_cookie(vm);
	bool *retu = arg;
	int error;

	error = dist_irouter_access(hyp, vcpuid, fault_ipa, &wval, retu, WRITE);

	return (error);
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

#define	alloc_registers(regs, num, size)				\
do {									\
	size = n * sizeof(*regs);					\
	regs = malloc(size, M_DIST_MMIO, M_WAITOK | M_ZERO);		\
} while (0)

#define	div_round_up(n, div)	(((n) + (div) - 1) / (div))

void
dist_mmio_init(struct hyp *hyp)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	size_t n, region_size;

	KASSERT(hyp->vgic_mmio_regions != NULL,
	    ("vgic_mmio_regions not allocated"));

#define	init_mmio_region(name, addr, size, accessor)			\
	hyp->vgic_mmio_regions[name] = (struct vgic_mmio_region){ 	\
		.start 	= dist->start + addr,				\
		.end 	= dist->start + addr + size,			\
		.read 	= accessor ##_read,				\
		.write 	= accessor ##_write,				\
	}

	init_mmio_region(VGIC_GICD_CTLR, GICD_CTLR, sizeof(dist->gicd_ctlr),
	    dist_ctlr);
	init_mmio_region(VGIC_GICD_TYPER, GICD_TYPER, sizeof(dist->gicd_typer),
	    dist_typer);

	n = div_round_up(dist->nirqs, 32);
	alloc_registers(dist->gicd_igroupr, n, region_size);
	init_mmio_region(VGIC_GICD_IGROUPR, GICD_IGROUPR_BASE,
	    region_size, dist_igroupr);


	/* ARM GIC Architecture Specification, page 8-471. */
	n = (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1;
	alloc_registers(dist->gicd_ixenabler, n , region_size);
	init_mmio_region(VGIC_GICD_ISENABLER, GICD_ISENABLER_BASE,
	    region_size, dist_isenabler);
	init_mmio_region(VGIC_GICD_ICENABLER, GICD_ICENABLER_BASE,
	    region_size, dist_icenabler);

	/* ARM GIC Architecture Specification, page 8-483. */
	n = 8 * ((dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1);
	alloc_registers(dist->gicd_ipriorityr, n, region_size);
	init_mmio_region(VGIC_GICD_IPRIORITYR, GICD_IPRIORITYR_BASE,
	    region_size, dist_ipriorityr);

	n = div_round_up(dist->nirqs, 16);
	alloc_registers(dist->gicd_icfgr, n, region_size);
	init_mmio_region(VGIC_GICD_ICFGR, GICD_ICFGR_BASE,
	    region_size, dist_icfgr);

	n = div_round_up(dist->nirqs, 32);
	alloc_registers(dist->gicd_igroupr, n, region_size);
	init_mmio_region(VGIC_GICD_IGROUPR, GICD_IGROUPR_BASE,
	    region_size, dist_igroupr);

	/* ARM GIC Architecture Specification, page 8-485. */
	n = 32 * (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK + 1) - 1;
	alloc_registers(dist->gicd_irouter, n, region_size);
	init_mmio_region(VGIC_GICD_IROUTER, GICD_IROUTER_BASE,
	    region_size, dist_irouter);

	init_mmio_region(VGIC_GICD_PIDR2, GICD_PIDR2, sizeof(dist->gicd_pidr2),
	    dist_pidr2);
}

void
dist_mmio_destroy(struct hyp *hyp)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;

	if (!hyp->vgic_mmio_regions)
		return;

	free(dist->gicd_igroupr, M_DIST_MMIO);
	free(dist->gicd_ixenabler, M_DIST_MMIO);
	free(dist->gicd_ipriorityr, M_DIST_MMIO);
	free(dist->gicd_icfgr, M_DIST_MMIO);
	free(dist->gicd_irouter, M_DIST_MMIO);
}
