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

#define dist_simple_read(src, destp, vm)		\
do {							\
	struct hyp *hyp = vm_get_cookie(vm);		\
	struct vgic_v3_dist *dist = &hyp->vgic_dist;	\
	*destp = dist->src;				\
} while (0);

#define dist_simple_write(src, dest, vm)		\
do {							\
	struct hyp *hyp = vm_get_cookie(vm);		\
	struct vgic_v3_dist *dist = &hyp->vgic_dist;	\
	dist->dest = src;				\
} while (0);

MALLOC_DEFINE(M_DIST_MMIO, "ARM VMM VGIC DIST MMIO", "ARM VMM VGIC DIST MMIO");

static int
dist_ctlr_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	dist_simple_read(gicd_ctlr, rval, vm);
	/* Writes are never pending */
	*rval &= ~GICD_CTLR_RWP;

	*retu = false;
	return (0);
}

static int
dist_ctlr_write(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t wval,
    int size, void *arg)
{
	bool *retu = arg;

	dist_simple_write(wval, gicd_ctlr, vm);

	*retu = false;
	return (0);
}

static int
dist_typer_read(void *vm, int vcpuid, uint64_t fault_ipa, uint64_t *rval,
    int size, void *arg)
{
	bool *retu = arg;

	dist_simple_read(gicd_typer, rval, vm);

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
	struct vgic_v3_redist *redist = &hyp->ctx[vcpuid].vgic_redist;
	uint32_t *regp;
	size_t regsize, n;
	size_t off;

	off = fault_ipa - hyp->vgic_mmio_regions[VGIC_GICD_IGROUPR].start;
	regsize = sizeof(*dist->gicd_igroupr);
	n = off / regsize;

	if (n == 0)
		regp = &redist->gicr_igroupr0;
	else
		regp = &dist->gicd_igroupr[n];

	if (dir == READ)
		*val = *regp;
	else
		*regp = *val;

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
	struct vgic_v3_redist *redist = &hyp->ctx[vcpuid].vgic_redist;
	uint32_t *regp;
	size_t off;
	size_t regsize, n;

	off = fault_ipa - hyp->vgic_mmio_regions[name].start;
	regsize = sizeof(*dist->gicd_ixenabler);
	n = off / regsize;

	if (n == 0)
		/* GICD_ICENABLER0 is equivalent to GICR_ICENABLER0 */
		regp = &redist->gicr_ixenabler0;
	else
		regp = &dist->gicd_ixenabler[n];

	if (dir == READ) {
		*val = *regp;
	} else {
		if (name == VGIC_GICD_ICENABLER)
			*regp &= ~(*val);
		else
			*regp |= *val;
	}

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

	if (dir == READ)
		*val = dist->gicd_ipriorityr[n];
	else
		dist->gicd_ipriorityr[n] = *val;

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

	if (dir == READ)
		*val = dist->gicd_irouter[n];
	else
		dist->gicd_irouter[n] = *val;

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
	bool *retu = arg;

	dist_simple_read(gicd_pidr2, rval, vm);

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

#define	div_round_up(n, div)	(((n) + (div) - 1) / (div))

void
dist_mmio_init(struct hyp *hyp)
{
	struct vgic_v3_dist *dist = &hyp->vgic_dist;
	size_t n;

	KASSERT(hyp->vgic_mmio_regions != NULL, ("vgic_mio_regions not allocated"));

#define	init_mmio_region(addr, num, regbits, shortname)			\
	hyp->vgic_mmio_regions[VGIC_ ##addr] = (struct vgic_mmio_region){ \
		.start 	= dist->start + addr,				\
		.end 	= dist->start + addr + num * (regbits >> 3),	\
		.read 	= dist_ ##shortname ##_read,			\
		.write 	= dist_ ##shortname ##_write,			\
	}
	init_mmio_region(GICD_CTLR, 1, 32, ctlr);
	init_mmio_region(GICD_TYPER, 1, 32, typer);

	n = div_round_up(dist->nirqs, 32);
	dist->gicd_igroupr = malloc(n * sizeof(*dist->gicd_igroupr),
	    M_DIST_MMIO, M_WAITOK | M_ZERO);
#define	init_mmio_region_base(addr, num, regbits, shortname)		\
	hyp->vgic_mmio_regions[VGIC_ ##addr] = (struct vgic_mmio_region){ \
		.start 	= dist->start + addr ##_BASE,			\
		.end 	= dist->start + addr ##_BASE + num * (regbits >> 3), \
		.read 	= dist_ ##shortname ##_read,			\
		.write 	= dist_ ##shortname ##_write,			\
	}
	init_mmio_region_base(GICD_IGROUPR, 1, 32, igroupr);


	/* ARM GIC Architecture Specification, page 8-471. */
	n = (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1;
	dist->gicd_ixenabler = malloc(n * sizeof(*dist->gicd_ixenabler),
	    M_DIST_MMIO, M_WAITOK | M_ZERO);
	init_mmio_region_base(GICD_ISENABLER, n, 32, isenabler);
	init_mmio_region_base(GICD_ICENABLER, n, 32, icenabler);

	/* ARM GIC Architecture Specification, page 8-483. */
	n = 8 * ((dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK) + 1);
	dist->gicd_ipriorityr = malloc(n * sizeof(*dist->gicd_ipriorityr),
	    M_DIST_MMIO, M_WAITOK | M_ZERO);
	init_mmio_region_base(GICD_IPRIORITYR, n, 32, ipriorityr);

	n = div_round_up(dist->nirqs, 16);
	dist->gicd_icfgr = malloc(n * sizeof(*dist->gicd_icfgr),
	    M_DIST_MMIO, M_WAITOK | M_ZERO);
	init_mmio_region_base(GICD_ICFGR, n, 32, icfgr);

	n = div_round_up(dist->nirqs, 32);
	dist->gicd_igroupr = malloc(n * sizeof(*dist->gicd_igroupr),
	    M_DIST_MMIO, M_WAITOK | M_ZERO);
	init_mmio_region_base(GICD_IGROUPR, n, 32, igroupr);

	/* ARM GIC Architecture Specification, page 8-485. */
	n = 32 * (dist->gicd_typer & GICD_TYPER_ITLINESNUM_MASK + 1) - 1;
	dist->gicd_irouter = malloc(n * sizeof(*dist->gicd_irouter),
	    M_DIST_MMIO, M_WAITOK | M_ZERO);
	init_mmio_region_base(GICD_IROUTER, n, 64, irouter);

	init_mmio_region(GICD_PIDR2, 1, 32, pidr2);
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
