#ifndef _EMUL_H_
#define _EMUL_H_

#include <sys/types.h>

#include <assert.h>

struct vmctx;
struct mmio_devinst;

// TODO suggestive naming
struct mmio_devemu {
	char *de_emu;		/* Device emulation name */

	/* Instance creation */
	int      (*de_init)(struct vmctx *ctx, struct mmio_devinst *di,
			    char *opts);

	/* Read / Write callbacks */
	void     (*de_write)(struct vmctx *ctx, int vcpu,
			     struct mmio_devinst *di, int baridx,
			     uint64_t offset, int size, uint64_t val);

	uint64_t (*de_read)(struct vmctx *ctx, int vcpu,
			    struct mmio_devinst *di, int baridx,
			    uint64_t offset, int size);
};

#define	MMIO_EMUL_SET(x)	DATA_SET(mmio_set, x);
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

// TODO suggestive naming
struct mmio_devinst {
	struct mmio_devemu	*pi_d;			/* Back ref to device */
	struct vmctx		*pi_vmctx;		/* Owner VM context */
	/* unused for mmio device emulation; may be used as uniquifiers */
	int			pi_slot, di_func;

	char			pi_name[DI_NAMESZ];	/* Instance name */

	struct {
		enum lintr_stat	state;
		int64_t		irq;
		pthread_mutex_t	lock;
	} di_lintr;

	void			*pi_arg;		/* Private data */

	u_char			pi_cfgregs[MMIO_REGNUM];/* Config regsters */

	struct devinst_addr	addr;			/* Address info */
};

int mmio_parse_opts(const char *args);
int mmio_alloc_mem(struct mmio_devinst *di);
int init_mmio(struct vmctx *ctx);
void mmio_lintr_request(struct mmio_devinst *di);
void mmio_lintr_assert(struct mmio_devinst *di);
void mmio_lintr_deassert(struct mmio_devinst *di);

static __inline void
mmio_set_cfgreg8(struct mmio_devinst *di, size_t offset, uint32_t val)
{
	assert(offset <= MMIO_REGMAX);
	*(uint32_t *)(di->pi_cfgregs + offset) = val;
}

static __inline void
mmio_set_cfgreg16(struct mmio_devinst *di, size_t offset, uint32_t val)
{
	assert(offset <= (MMIO_REGMAX - 1) && (offset & 1) == 0);
	*(uint32_t *)(di->pi_cfgregs + offset) = val;
}

static __inline void
mmio_set_cfgreg32(struct mmio_devinst *di, size_t offset, uint32_t val)
{
	assert(offset <= (MMIO_REGMAX - 3) && (offset & 3) == 0);
	*(uint32_t *)(di->pi_cfgregs + offset) = val;
}

static __inline uint8_t
mmio_get_cfgreg8(struct mmio_devinst *di, size_t offset)
{
	assert(offset <= MMIO_REGMAX);
	return (*(uint32_t *)(di->pi_cfgregs + offset));
}

static __inline uint16_t
mmio_get_cfgreg16(struct mmio_devinst *di, size_t offset)
{
	assert(offset <= (MMIO_REGMAX - 1) && (offset & 1) == 0);
	return (*(uint32_t *)(di->pi_cfgregs + offset));
}

static __inline uint32_t
mmio_get_cfgreg32(struct mmio_devinst *di, size_t offset)
{
	assert(offset <= (MMIO_REGMAX - 3) && (offset & 3) == 0);
	return (*(uint32_t *)(di->pi_cfgregs + offset));
}

#endif /* _EMUL_H_ */
