#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/linker_set.h>
#include <sys/param.h>
#include <sys/types.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "mmio_emul.h"
#include "mmio_irq.h"

#define	MMIO_EMUL_MEMBASE	0xD000000000UL
#define	MMIO_EMUL_MEMLIMIT	0xFD00000000UL
#define	MEM_ROUNDUP		(1 << 20)
#ifndef max
# define max(A, B) ((A) > (B) ? (A) : (B))
#endif

static uint64_t mmio_emul_membase;

SET_DECLARE(mmio_devemu_set, struct mmio_devemu);

static struct mmio_devemu *mmio_emul_finddef(const char *name);
static void mmio_lintr_route(struct mmio_devinst *mi);
static void mmio_lintr_update(struct mmio_devinst *mi);

static struct mmio_info {
	uint64_t size;			/* address size */
	uint64_t baddr;			/* address */
	char *name;			/* device name */
	char *arg;			/* device arguments */
	struct mmio_info *next;		/* pointer for linked list */
	struct mmio_devinst *mi;	/* pointer to device instance */
} *mmio_info_head = NULL;

/*
 * MMIO options are in the form:
 *
 * size[@<base_addr>]:<emul>[,<config>]
 *
 * - size is the number of bytes required for the device mmio
 * - base_addr is an optional base address for the MMIO mapped device;
 *     if absent, a default value base on the emulated device will be
 *     used;
 * - emul is a string describing the type of device - e.g., virtio-net;
 * - config is an optional string, depending on the device, that is used
 *     for configuration
 *
 * Examples of use:
 *   0x200@0x100000:virtio-net,tap0
 *   0x100@dummy
 */
static void
mmio_parse_opts_usage(const char *args)
{
	fprintf(stderr, "Invalid MMIO arguments \"%s\"\r\n", args);
}

/*
 * checks if the requested address is available
 * checks are not required if one of the pointers is null
 */
static int
mmio_mem_available(uint64_t pa, uint64_t sa, uint64_t pb, uint64_t sb)
{
#define IN_INTERVAL(lower, value, upper)	\
	(((lower) < (value)) && ((value) < (upper)))

	if ((pa == 0) || (pb == 0))
		return 1;

	if ((!IN_INTERVAL(pa, pb, pa + sa)) &&
	    (!IN_INTERVAL(pb, pa, pb + sb)))
		return 1;

	return 0;

#undef IN_INTERVAL
}

int
mmio_parse_opts(const char *args)
{
	char *emul, *config, *str;
	uint64_t size, baddr;
	int error;
	struct mmio_info *mmi;

	error = -1;
	emul = config = NULL;
	baddr = 0, size = 0;
	str = strdup(args);

	if ((emul = strchr(str, ':')) != NULL) {
		*emul++ = '\0';

		/* <size>@<base-addr> */
		if (sscanf(str, "%llx@%llx", &size, &baddr) != 2 &&
		    sscanf(str, "%llu@%llu", &size, &baddr) != 2) {
			/* <size> */
			if (sscanf(str, "%llx", &size) != 1 &&
			    sscanf(str, "%llu", &size) != 1) {
				mmio_parse_opts_usage(str);
				goto parse_error;
			}
		}
	} else {
		mmio_parse_opts_usage(str);
		goto parse_error;
	}

	if ((config = strchr(emul, ',')) != NULL)
		*config++ = '\0';

	/*
	 * check if the required address can be obtained;
	 * if an address has not been requested, ignore the checks
	 * (however, an address will have to be later identified)
	 */
	if (baddr != 0) {
		for (mmi = mmio_info_head; mmi != NULL; mmi = mmi->next)
			if ((mmio_mem_available(mmi->baddr, mmi->size,
						baddr, size)) != 0)
				break;

		if (mmi != NULL) {
			fprintf(stderr, "The requested address 0x%llx is "
				"already bound or overlapping\r\n", baddr);
			goto parse_error;
		}
	}

	mmi = calloc(1, sizeof(struct mmio_info));
	if (mmi == NULL) {
		error = ENOMEM;
		goto parse_error;
	}

	mmi->next = mmio_info_head;
	mmio_info_head = mmi;

	mmi->size = size;
	mmi->baddr = baddr;
	if ((emul != NULL) && (strlen(emul)) > 0)
		mmi->name = strdup(emul);
	else
		mmi->name = NULL;
	if ((config != NULL) && (strlen(config)) > 0)
		mmi->arg = strdup(config);
	else
		mmi->arg = NULL;

	error = 0;

parse_error:
	free(str);

	return error;
}

static int
mmio_emul_mem_handler(struct vmctx *ctx, int vcpu, int dir, uint64_t addr,
		      int size, uint64_t *val, void *arg1, long arg2)
{
	struct mmio_devinst *mi = arg1;
	struct mmio_devemu *me = mi->mi_d;
	uint64_t offset;

	assert(mi->addr.baddr <= addr &&
	       addr + size <= mi->addr.baddr + mi->addr.size);

	offset = addr - mi->addr.baddr;

	if (dir == MEM_F_WRITE)
		(*me->me_write)(ctx, vcpu, mi, offset, size, *val);
	else
		*val  = (*me->me_read)(ctx, vcpu, mi, offset, size);

	return (0);
}

static void
modify_mmio_registration(struct mmio_devinst *mi, int registration)
{
	int error;
	struct mem_range mr;

	bzero(&mr, sizeof(struct mem_range));
	mr.name = mi->mi_name;
	mr.base = mi->addr.baddr;
	mr.size = mi->addr.size;
	if (registration) {
		mr.flags = MEM_F_RW;
		mr.handler = mmio_emul_mem_handler;
		mr.arg1 = mi;
		mr.arg2 = 0;
		error = register_mem(&mr);
	} else {
		error = unregister_mem(&mr);
	}

	assert(error == 0);
}

static void
register_mmio(struct mmio_devinst *mi)
{
	return modify_mmio_registration(mi, 1);
}

static void
unregister_mmio(struct mmio_devinst *mi)
{
	return modify_mmio_registration(mi, 0);
}

/*
 * Update the MMIO address that is decoded
 */
static void
update_mem_address(struct mmio_devinst *mi, uint64_t addr)
{
	/* TODO: check if the decoding is running */
	unregister_mmio(mi);

	mi->addr.baddr = addr;

	register_mmio(mi);
}

static int
mmio_emul_alloc_resource(uint64_t *baseptr, uint64_t limit, uint64_t size,
			uint64_t *addr)
{
	uint64_t base;

	assert((size & (size - 1)) == 0);	/* must be a power of 2 */

	base = roundup2(*baseptr, size);

	if (base + size <= limit) {
		*addr = base;
		*baseptr = base + size;
		return (0);
	} else
		return (-1);
}

int
mmio_emul_alloc_mem(struct mmio_devinst *mi)
{
	int error;
	uint64_t *baseptr, limit, addr, mask, size;

	if ((size & (size - 1)) != 0)
		size = 1UL << flsl(size); /* round up to a power of 2 */

	baseptr = &mi->addr.baddr;
	size = mi->addr.size;
	limit = MMIO_EMUL_MEMLIMIT;
	/* XXX: a define for this might be useful.
	 * Value basen on PCIM_BAR_MEM_BASE
	 */
	mask = ~0xfUL;

	error = mmio_emul_alloc_resource(baseptr, limit, size, &addr);
	if (error != 0)
		return (error);

	mi->addr.baddr = addr;

	register_mmio(mi);

	return (0);
}

static struct mmio_devemu *
mmio_emul_finddev(char *name)
{
	struct mmio_devemu **mdpp, *mdp;

	SET_FOREACH(mdpp, mmio_devemu_set) {
		mdp = *mdpp;
		if (!strcmp(mdp->me_emu, name))
			return (mdp);
	}

	return (NULL);
}

static int
mmio_emul_init(struct vmctx *ctx, struct mmio_devemu *me, struct mmio_info *mmi)
{
	struct mmio_devinst *mi;
	int error;

	mi = calloc(1, sizeof(struct mmio_devinst));
	if (mi == NULL)
		return (ENOMEM);

	mi->mi_cfgregs = calloc(max(mmi->size, MMIO_REGNUM), sizeof(u_char));
	if (mi->mi_cfgregs == NULL) {
		free(mi);
		return (ENOMEM);
	}

	mi->mi_d = me;
	mi->mi_vmctx = ctx;
	snprintf(mi->mi_name, MI_NAMESZ, "%s-mmio", me->me_emu);
	mi->mi_lintr.state = IDLE;
	mi->mi_lintr.irq = 0;
	pthread_mutex_init(&mi->mi_lintr.lock, NULL);
	mi->mi_cfgspace = mi->mi_cfgregs + MMIO_REGNUM;
	mi->addr.baddr = mmi->baddr;
	mi->addr.size = mmi->size;

	error = (*me->me_init)(ctx, mi, mmi->arg);

	if (error == 0) {
		mmi->mi = mi;
	} else {
		free(mi->mi_cfgregs);
		free(mi);
	}

	return (error);
}

static void
init_mmio_error(const char *name)
{
	struct mmio_devemu **mdpp, *mdp;

	fprintf(stderr, "Device \"%s\" does not exist\r\n", name);
	fprintf(stderr, "The following devices are available:\r\n");

	SET_FOREACH(mdpp, mmio_devemu_set) {
		mdp = *mdpp;
		fprintf(stderr, "\t%s\r\n", mdp->me_emu);
	}
}

int init_mmio(struct vmctx *ctx)
{
	struct mmio_devemu *me;
	struct mmio_info *mmi;
	int error;

	mmio_emul_membase = MMIO_EMUL_MEMBASE;

	for (mmi = mmio_info_head; mmi != NULL; mmi = mmi->next) {
		if (mmi->name == NULL)
			continue;

		me = mmio_emul_finddev(mmi->name);
		if (me == NULL) {
			init_mmio_error(mmi->name);
			return (1);
		}

		error = mmio_emul_init(ctx, me, mmi);
		if (error != 0)
			return (error);

		/*
		 * as specified in the amd64 implementation, add some
		 * slop to the memory resources decoded, in order to
		 * give the guest some flexibility to reprogram the addresses 
		 */
		mmio_emul_membase += MEM_ROUNDUP;
		mmio_emul_membase = roundup2(mmio_emul_membase, MEM_ROUNDUP);
	}

	/* activate the interrupts */
	for (mmi = mmio_info_head; mmi != NULL; mmi = mmi->next)
		if (mmi->mi != NULL)
			mmio_lintr_route(mmi->mi);

	/* TODO: register fallback handlers? */

	return (0);
}

void
mmio_lintr_request(struct mmio_devinst *mi)
{
	/* questionable use */
}

static void
mmio_lintr_route(struct mmio_devinst *mi)
{
	/* questionable use */
}

void
mmio_lintr_assert(struct mmio_devinst *mi)
{
	pthread_mutex_lock(&mi->mi_lintr.lock);
	if (mi->mi_lintr.state == IDLE) {
		mi->mi_lintr.state = ASSERTED;
		mmio_irq_assert(mi);
	}
	pthread_mutex_unlock(&mi->mi_lintr.lock);
}

void
mmio_lintr_deassert(struct mmio_devinst *mi)
{
	pthread_mutex_lock(&mi->mi_lintr.lock);
	if (mi->mi_lintr.state == ASSERTED) {
		mmio_irq_deassert(mi);
		mi->mi_lintr.state = PENDING;
	} else if (mi->mi_lintr.state == PENDING) {
		mi->mi_lintr.state = ASSERTED;
		mmio_irq_assert(mi);
	}
	pthread_mutex_unlock(&mi->mi_lintr.lock);
}

/* TODO: Add dummy? */
