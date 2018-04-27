#ifndef _DEVEMU_H_
#define _DEVEMU_H_

#include <sys/types.h>

#include <assert.h>

struct vmctx;
struct devemu_inst;

struct devemu_dev {
	char *de_emu;		/* Device emulation name */

	/* Instance creation */
	int      (*de_init)(struct vmctx *ctx, struct devemu_inst *di,
			    char *opts);

	/* Read / Write callbacks */
	void     (*de_write)(struct vmctx *ctx, int vcpu,
			     struct devemu_inst *di, int baridx,
			     uint64_t offset, int size, uint64_t val);

	uint64_t (*de_read)(struct vmctx *ctx, int vcpu,
			    struct devemu_inst *di, int baridx,
			    uint64_t offset, int size);
};

#define	DEVEMU_SET(x)	DATA_SET(devemu_set, x);
#define	DI_NAMESZ		40
#define	MMIO_REGMAX		0xff
#define	MMIO_REGNUM		(MMIO_REGMAX + 1)

struct devinst_addr {
	uint64_t baddr;
	uint64_t size;
};

enum lintr_stat {
	IDLE,
	ASSERTED,
	PENDING
};

struct devemu_inst {
	struct devemu_dev	*di_d;			/* Back ref to device */
	struct vmctx		*di_vmctx;		/* Owner VM context */
	/* unused for mmio device emulation; may be used as uniquifiers */
	int			di_slot, di_func;

	char			di_name[DI_NAMESZ];	/* Instance name */

	struct {
		enum lintr_stat	state;
		uint32_t	irq;
		pthread_mutex_t	lock;
	} di_lintr;

	void			*di_arg;		/* Private data */

	u_char			di_cfgregs[MMIO_REGNUM];/* Config regsters */

	struct devinst_addr	addr;			/* Address info */
};

int devemu_parse_opts(const char *args);
int devemu_alloc_mem(struct devemu_inst *di);
int init_devemu(struct vmctx *ctx);
void devemu_lintr_request(struct devemu_inst *di);
void devemu_lintr_assert(struct devemu_inst *di);
void devemu_lintr_deassert(struct devemu_inst *di);

static __inline void
devemu_set_cfgreg(struct devemu_inst *di, size_t offset, uint32_t val)
{
	assert(offset <= (MMIO_REGMAX - 3) && (offset & 3) == 0);
	*(uint32_t *)(di->di_cfgregs + offset) = val;
}

static __inline uint32_t
devemu_get_cfgreg(struct devemu_inst *di, size_t offset)
{
	assert(offset <= (MMIO_REGMAX - 3) && (offset & 3) == 0);
	return (*(uint32_t *)(di->di_cfgregs + offset));
}

#endif /* _DEVEMU_H_ */
