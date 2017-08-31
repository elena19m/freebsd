#ifndef _MMIO_EMUL_H_
#define _MMIO_EMUL_H_

#include <sys/types.h>

#include <assert.h>

struct vmctx;
struct mmio_devinst;

struct mmio_devemu {
	char *me_emu;		/* Device emulation name */

	/* Instance creation */
	int (*me_init)(struct vmctx *ctx, struct mmio_devinst *mi,
			char *opts);

	/* Read / Write callbacks */
	void     (*me_write)(struct vmctx *ctx, int vcpu,
			     struct mmio_devinst *mi, uint64_t offset,
			     size_t size, uint64_t val);

	uint64_t (*me_read) (struct vmctx *ctx, int vcpu,
			     struct mmio_devinst *mi, uint64_t offset,
			     size_t size);
};

#define	MMIO_EMUL_SET(x)	DATA_SET(mmio_devemu_set, x);
#define	MI_NAMESZ		40
#define	MMIO_REGMAX		0xff
#define	MMIO_REGNUM		(MMIO_REGMAX + 1)

struct mmio_addr {
	uint64_t baddr;
	uint64_t size;
};

enum lintr_stat {
	IDLE,
	ASSERTED,
	PENDING
};

struct mmio_devinst {
	struct mmio_devemu	*mi_d;			/* Back ref to device */
	struct vmctx		*mi_vmctx;		/* Owner VM context */

	char			mi_name[MI_NAMESZ];	/* Instance name */

	struct {
		enum lintr_stat	state;
		uint32_t	irq;
		pthread_mutex_t	lock;
	} mi_lintr;

	void			*mi_arg;		/* Private data */

	u_char			*mi_cfgregs;		/* Config regsters */
	/* Config space; equivalent to mi_cfgregs + MMIO_REGNUM */
	u_char			*mi_cfgspace;

	struct mmio_addr	addr;			/* Address info */
};

int mmio_parse_opts(const char *args);
int mmio_emul_alloc_mem(struct mmio_devinst *mi);
int init_mmio(struct vmctx *ctx);
void mmio_lintr_request(struct mmio_devinst *mi);
void mmio_lintr_assert(struct mmio_devinst *mi);
void mmio_lintr_deassert(struct mmio_devinst *mi);

static __inline void
mmio_set_cfgreg(struct mmio_devinst *mi, size_t offset, uint32_t val)
{
	assert(offset <= (MMIO_REGMAX - 3) && (offset & 3) == 0);
	*(uint32_t *)(mi->mi_cfgregs + offset) = val;
}

static __inline uint32_t
mmio_get_cfgreg(struct mmio_devinst *mi, size_t offset)
{
	assert(offset <= (MMIO_REGMAX - 3) && (offset & 3) == 0);
	return (*(uint32_t *)(mi->mi_cfgregs + offset));
}

#endif /* _MMIO_EMUL_H_ */
