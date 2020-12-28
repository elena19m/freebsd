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

#include "arm64/mem.h"
#include "mmio_emul.h"
#include "mmio_irq.h"

#define	DEVEMU_MEMLIMIT		0xFD00000000UL
#define	DEVEMU_MEMBASE		0xD000000000UL
#define	MEM_ROUNDUP		(1 << 20)
#ifndef max
# define max(A, B) ((A) > (B) ? (A) : (B))
#endif

static uint64_t mmio_membase;

SET_DECLARE(mmio_set, struct mmio_devemu);

static struct mmio_devemu *mmio_finddef(const char *name);
static void mmio_lintr_route(struct mmio_devinst *di);
static void mmio_lintr_update(struct mmio_devinst *di);

static struct mmio_emul_info {
	uint64_t size;			/* address size */
	uint64_t baddr;			/* address */
	int64_t irq;			/* device interrupt number */
	char *name;			/* device name */
	char *arg;			/* device arguments */
	struct mmio_emul_info *next;	/* pointer for linked list */
	struct mmio_devinst *di;	/* pointer to device instance */
} *mmio_emul_info_head = NULL;

/*
 * MMIO options are in the form:
 *
 * <size>@<base_addr>#<irq>:<emul>[,<config>]
 *
 * - size is the number of bytes required for the device mmio
 * - base_addr is the base address for the MMIO mapped device;
 * - irq specifies the device interrupt number the value MUST be a DECIMAL
 *   integer; if the device does not use interrupts, use -1
 * - emul is a string describing the type of device - e.g., virtio-net;
 * - config is an optional string, depending on the device, that is used
 *     for configuration
 *
 * Examples of use:
 *   0x200@0x100000#25:virtio-net,tap0
 *   0x100@0x200000#-1:dummy
 */
static void
mmio_parse_opts_usage(const char *args)
{
	fprintf(stderr, "Invalid mmio arguments \"%s\"\r\n", args);
}

/*
 * checks if two memory regions overlap
 * checks are not required if one of the pointers is null
 */
static int
mmio_mem_overlap(uint64_t pa, uint64_t sa, uint64_t pb, uint64_t sb)
{
#define IN_INTERVAL(lower, value, upper)	\
	(((lower) < (value)) && ((value) < (upper)))

	if ((pa == 0) || (pb == 0))
		return 0;

	if (IN_INTERVAL(pa, pb, pa + sa) &&
	    IN_INTERVAL(pb, pa, pb + sb))
		return 1;

	return 0;

#undef IN_INTERVAL
}

int
mmio_parse_opts(const char *args)
{
	char *emul, *config, *str;
	uint64_t size, baddr;
	int64_t irq;
	int error;
	struct mmio_emul_info *dif;

	error = -1;
	emul = config = NULL;
	baddr = 0, size = 0;
	str = strdup(args);

	if ((emul = strchr(str, ':')) != NULL) {
		*emul++ = '\0';

		/* <size>@<base-addr>#<irq> */
		if (sscanf(str, "%jx@%jx#%jd", &size, &baddr, &irq) != 3 &&
		    sscanf(str, "%jx@%jx#%jd", &size, &baddr, &irq) != 3) {
			mmio_parse_opts_usage(str);
			goto parse_error;
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
		for (dif = mmio_emul_info_head; dif != NULL; dif = dif->next)
			if (mmio_mem_overlap(dif->baddr, dif->size,
					       baddr, size))
				break;

		if (dif != NULL) {
			fprintf(stderr, "The requested address 0x%jx is "
				"already bound or overlapping\r\n", baddr);
			error = EINVAL;
			goto parse_error;
		}
	}

	dif = calloc(1, sizeof(struct mmio_emul_info));
	if (dif == NULL) {
		error = ENOMEM;
		goto parse_error;
	}

	dif->next = mmio_emul_info_head;
	mmio_emul_info_head = dif;

	dif->size = size;
	dif->baddr = baddr;
	dif->irq = irq;
	if ((emul != NULL) && (strlen(emul)) > 0)
		dif->name = strdup(emul);
	else
		dif->name = NULL;
	if ((config != NULL) && (strlen(config)) > 0)
		dif->arg = strdup(config);
	else
		dif->arg = NULL;

	error = 0;

parse_error:
	free(str);

	return error;
}

static int
mmio_mem_handler(struct vmctx *ctx, int vcpu, int dir, uint64_t addr,
		      int size, uint64_t *val, void *arg1, long arg2)
{
	struct mmio_devinst *di = arg1;
	struct mmio_devemu *de = di->pi_d;
	uint64_t offset;
	int bidx = (int) arg2;

	assert(di->addr.baddr <= addr &&
	       addr + size <= di->addr.baddr + di->addr.size);

	offset = addr - di->addr.baddr;

	if (dir == MEM_F_WRITE) {
		if (size == 8) {
			(*de->de_write)(ctx, vcpu, di, bidx, offset,
					4, *val & 0xffffffff);
			(*de->de_write)(ctx, vcpu, di, bidx, offset + 4,
					4, *val >> 32);
		} else {
			(*de->de_write)(ctx, vcpu, di, bidx, offset,
					size, *val);
		}
	} else {
		if (size == 8) {
			*val = (*de->de_read)(ctx, vcpu, di, bidx,
						 offset, 4);
			*val |= (*de->de_read)(ctx, vcpu, di, bidx,
						  offset + 4, 4) << 32;
		} else {
			*val = (*de->de_read)(ctx, vcpu, di, bidx,
						 offset, size);
		}
	}

	return (0);
}

static void
modify_mmio_registration(struct mmio_devinst *di, int registration)
{
	int error;
	struct mem_range mr;

	bzero(&mr, sizeof(struct mem_range));
	mr.name = di->pi_name;
	mr.base = di->addr.baddr;
	mr.size = di->addr.size;
	if (registration) {
		mr.flags = MEM_F_RW;
		mr.handler = mmio_mem_handler;
		mr.arg1 = di;
		mr.arg2 = 0;
		error = register_mem(&mr);
	} else {
		error = unregister_mem(&mr);
	}

	assert(error == 0);
}

static void
register_mmio(struct mmio_devinst *di)
{
	return modify_mmio_registration(di, 1);
}

static void
unregister_mmio(struct mmio_devinst *di)
{
	return modify_mmio_registration(di, 0);
}

/*
 * Update the MMIO address that is decoded
 */
static void
update_mem_address(struct mmio_devinst *di, uint64_t addr)
{
	/* TODO: check if the decoding is running */
	unregister_mmio(di);

	di->addr.baddr = addr;

	register_mmio(di);
}

static int
mmio_alloc_resource(uint64_t *baseptr, uint64_t limit, uint64_t size,
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
mmio_alloc_mem(struct mmio_devinst *di)
{
	int error;
	uint64_t *baseptr, limit, addr, size;

	baseptr = &di->addr.baddr;
	size = di->addr.size;
	limit = DEVEMU_MEMLIMIT;

	if ((size & (size - 1)) != 0)
		/* Round up to a power of 2 */
		size = 1UL << flsl(size);

	error = mmio_alloc_resource(baseptr, limit, size, &addr);
	if (error != 0)
		return (error);

	di->addr.baddr = addr;

	register_mmio(di);

	return (0);
}

static struct mmio_devemu *
mmio_finddev(char *name)
{
	struct mmio_devemu **dpp, *dp;

	SET_FOREACH(dpp, mmio_set) {
		dp = *dpp;
		if (!strcmp(dp->de_emu, name))
			return (dp);
	}

	return (NULL);
}

static int
mmio_init(struct vmctx *ctx, struct mmio_devemu *de, struct mmio_emul_info *dif)
{
	struct mmio_devinst *di;
	int error;

	di = calloc(1, sizeof(struct mmio_devinst));
	if (di == NULL)
		return (ENOMEM);

	di->pi_d = de;
	di->pi_vmctx = ctx;
	snprintf(di->pi_name, DI_NAMESZ, "%s-mmio", de->de_emu);
	di->di_lintr.state = IDLE;
	di->di_lintr.irq = dif->irq;
	pthread_mutex_init(&di->di_lintr.lock, NULL);
	di->addr.baddr = dif->baddr;
	di->addr.size = dif->size;
	/* some devices (e.g., virtio-net) use these as uniquifiers; irq number
	 * should be unique and sufficient */
	di->pi_slot = dif->irq;
	di->di_func = dif->irq;

	error = (*de->de_init)(ctx, di, dif->arg);

	if (error == 0) {
		dif->di = di;
	} else {
		fprintf(stderr, "Device \"%s\": initialization failed\r\n",
			di->pi_name);
		fprintf(stderr, "Device arguments were: %s\r\n", dif->arg);
		free(di);
	}

	return (error);
}

static void
init_mmio_error(const char *name)
{
	struct mmio_devemu **mdpp, *mdp;

	fprintf(stderr, "Device \"%s\" does not exist\r\n", name);
	fprintf(stderr, "The following devices are available:\r\n");

	SET_FOREACH(mdpp, mmio_set) {
		mdp = *mdpp;
		fprintf(stderr, "\t%s\r\n", mdp->de_emu);
	}
}

int init_mmio(struct vmctx *ctx)
{
	struct mmio_devemu *de;
	struct mmio_emul_info *dif;
	int error;

	mmio_membase = DEVEMU_MEMBASE;

	for (dif = mmio_emul_info_head; dif != NULL; dif = dif->next) {
		if (dif->name == NULL)
			continue;

		de = mmio_finddev(dif->name);
		if (de == NULL) {
			init_mmio_error(dif->name);
			return (1);
		}

		error = mmio_init(ctx, de, dif);
		if (error != 0)
			return (error);

		/*
		 * as specified in the amd64 implementation, add some
		 * slop to the memory resources decoded, in order to
		 * give the guest some flexibility to reprogram the addresses
		 */
		mmio_membase += MEM_ROUNDUP;
		mmio_membase = roundup2(mmio_membase, MEM_ROUNDUP);
	}

	/* activate the interrupts */
	for (dif = mmio_emul_info_head; dif != NULL; dif = dif->next)
		if (dif->di != NULL)
			mmio_lintr_route(dif->di);

	/* TODO: register fallback handlers? */

	return (0);
}

void
mmio_lintr_request(struct mmio_devinst *di)
{
	/* do nothing */
}

static void
mmio_lintr_route(struct mmio_devinst *di)
{
	/* do nothing */
}

void
mmio_lintr_assert(struct mmio_devinst *di)
{
	pthread_mutex_lock(&di->di_lintr.lock);
	if (di->di_lintr.state == IDLE) {
		di->di_lintr.state = ASSERTED;
		mmio_irq_assert(di);
	}
	pthread_mutex_unlock(&di->di_lintr.lock);
}

void
mmio_lintr_deassert(struct mmio_devinst *di)
{
	pthread_mutex_lock(&di->di_lintr.lock);
	if (di->di_lintr.state == ASSERTED) {
		mmio_irq_deassert(di);
		di->di_lintr.state = IDLE;
	} else if (di->di_lintr.state == PENDING) {
		di->di_lintr.state = IDLE;
	}
	pthread_mutex_unlock(&di->di_lintr.lock);
}

/* TODO: Add dummy? */
