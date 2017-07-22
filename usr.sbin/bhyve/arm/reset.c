#include <stdio.h>
#include <sys/types.h>

#include "mem.h"
#include "reset.h"
#include "vmmapi.h"

#define RESET_PORT	0x1c090100

static int
reset_handler(struct vmctx *ctx, int vcpu, int dir, uint64_t addr, int size, uint64_t *val, void *arg1, long arg2)
{
	vm_destroy(ctx);

	return (RESET_MAGIC);
}

struct mem_range resetport ={
	"reset",
	0,
	reset_handler,
	NULL,
	0,
	RESET_PORT,
	sizeof(int)
};

void
init_reset(void)
{
	register_mem(&resetport);
}
