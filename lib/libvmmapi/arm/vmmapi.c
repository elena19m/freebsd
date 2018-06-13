#include <sys/cdefs.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include<sys/errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "vmmapi.h"

#define	GB	(1024 * 1024 * 1024UL)

struct vmctx {
	int	fd;
	uint32_t mem_limit;
	enum vm_mmap_style vms;
	size_t	mem_size;
	uint64_t mem_base;
	char	*mem_addr;
	char	*name;
};

#define	CREATE(x)  sysctlbyname("hw.vmm.create", NULL, NULL, (x), strlen((x)))
#define	DESTROY(x) sysctlbyname("hw.vmm.destroy", NULL, NULL, (x), strlen((x)))

static int
vm_device_open(const char *name)
{
        int fd, len;
        char *vmfile;

	len = strlen("/dev/vmm/") + strlen(name) + 1;
	vmfile = malloc(len);
	assert(vmfile != NULL);
	snprintf(vmfile, len, "/dev/vmm/%s", name);

        /* Open the device file */
        fd = open(vmfile, O_RDWR, 0);

	free(vmfile);
        return (fd);
}

int
vm_create(const char *name)
{

	return (CREATE((char *)name));
}

struct vmctx *
vm_open(const char *name)
{
	struct vmctx *vm;

	vm = malloc(sizeof(struct vmctx) + strlen(name) + 1);
	assert(vm != NULL);

	vm->fd = -1;
	vm->mem_limit = 2 * GB;
	vm->name = (char *)(vm + 1);
	strcpy(vm->name, name);

	if ((vm->fd = vm_device_open(vm->name)) < 0)
		goto err;

	return (vm);
err:
	vm_destroy(vm);
	return (NULL);
}

void
vm_destroy(struct vmctx *vm)
{
	assert(vm != NULL);

	if (vm->fd >= 0)
		close(vm->fd);
	DESTROY(vm->name);

	free(vm);
}

int
vm_get_memory_seg(struct vmctx *ctx, uint64_t gpa, size_t *ret_len)
{
	int error;
	struct vm_memory_segment seg;

	bzero(&seg, sizeof(seg));
	seg.gpa = gpa;
	error = ioctl(ctx->fd, VM_GET_MEMORY_SEG, &seg);
	*ret_len = seg.len;
	return (error);
}

uint32_t
vm_get_mem_limit(struct vmctx *ctx)
{

	return (ctx->mem_limit);
}

void
vm_set_mem_limit(struct vmctx *ctx, uint32_t limit)
{

	ctx->mem_limit = limit;
}

static int
setup_memory_segment(struct vmctx *ctx, uint64_t gpa, size_t len, char **addr)
{
	int error;
	struct vm_memory_segment seg;

	/*
	 * Create and optionally map 'len' bytes of memory at guest
	 * physical address 'gpa'
	 */
	bzero(&seg, sizeof(seg));
	seg.gpa = gpa;
	seg.len = len;
	error = ioctl(ctx->fd, VM_MAP_MEMORY, &seg);
	if (error == 0 && addr != NULL) {
		*addr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED,
				ctx->fd, gpa);
	}
	return (error);
}

int
vm_setup_memory(struct vmctx *ctx, uint64_t membase, size_t memsize, enum vm_mmap_style vms)
{
	int error;

	/* XXX VM_MMAP_SPARSE not implemented yet */
	assert(vms == VM_MMAP_ALL);

	ctx->vms = vms;
	ctx->mem_base = membase;

	assert(memsize <= ctx->mem_limit);
	ctx->mem_size = memsize;

	if (ctx->mem_size > 0) {
		error = setup_memory_segment(ctx, ctx->mem_base, ctx->mem_size,
		    &ctx->mem_addr);
		if (error)
			return (error);
	}

	return (0);
}

void *
vm_map_gpa(struct vmctx *ctx, uint64_t gaddr, size_t len)
{

	/* XXX VM_MMAP_SPARSE not implemented yet */
	assert(ctx->vms == VM_MMAP_ALL);

	if (gaddr < ctx->mem_base)
		return ((void *)(ctx->mem_addr + gaddr));
	else
		return ((void *)(ctx->mem_addr + (gaddr - ctx->mem_base)));
}


int
vm_set_register(struct vmctx *ctx, int vcpu, int reg, uint64_t val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;
	vmreg.regval = val;

	error = ioctl(ctx->fd, VM_SET_REGISTER, &vmreg);
	return (error);
}

int
vm_get_register(struct vmctx *ctx, int vcpu, int reg, uint64_t *ret_val)
{
	int error;
	struct vm_register vmreg;

	bzero(&vmreg, sizeof(vmreg));
	vmreg.cpuid = vcpu;
	vmreg.regnum = reg;

	error = ioctl(ctx->fd, VM_GET_REGISTER, &vmreg);
	*ret_val = vmreg.regval;
	return (error);
}

int
vm_run(struct vmctx *ctx, int vcpu, uint64_t pc, struct vm_exit *vmexit)
{
	int error;
	struct vm_run vmrun;

	bzero(&vmrun, sizeof(vmrun));
	vmrun.cpuid = vcpu;
	vmrun.pc = pc;

	error = ioctl(ctx->fd, VM_RUN, &vmrun);
	bcopy(&vmrun.vm_exit, vmexit, sizeof(struct vm_exit));
	return (error);
}

static struct {
	const char	*name;
	int		type;
} capstrmap[] = {
	{ "hlt_exit",		VM_CAP_HALT_EXIT },
	{ "mtrap_exit",		VM_CAP_MTRAP_EXIT },
	{ "pause_exit",		VM_CAP_PAUSE_EXIT },
	{ "unrestricted_guest",	VM_CAP_UNRESTRICTED_GUEST },
	{ 0 }
};

int
vm_capability_name2type(const char *capname)
{
	int i;

	for (i = 0; capstrmap[i].name != NULL && capname != NULL; i++) {
		if (strcmp(capstrmap[i].name, capname) == 0)
			return (capstrmap[i].type);
	}

	return (-1);
}

const char *
vm_capability_type2name(int type)
{
	int i;

	for (i = 0; capstrmap[i].name != NULL; i++) {
		if (capstrmap[i].type == type)
			return (capstrmap[i].name);
	}

	return (NULL);
}

int
vm_get_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
		  int *retval)
{
	int error;
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;

	error = ioctl(ctx->fd, VM_GET_CAPABILITY, &vmcap);
	*retval = vmcap.capval;
	return (error);
}

int
vm_set_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap, int val)
{
	struct vm_capability vmcap;

	bzero(&vmcap, sizeof(vmcap));
	vmcap.cpuid = vcpu;
	vmcap.captype = cap;
	vmcap.capval = val;
	
	return (ioctl(ctx->fd, VM_SET_CAPABILITY, &vmcap));
}

uint64_t *
vm_get_stats(struct vmctx *ctx, int vcpu, struct timeval *ret_tv,
	     int *ret_entries)
{
	int error;

	static struct vm_stats vmstats;

	vmstats.cpuid = vcpu;

	error = ioctl(ctx->fd, VM_STATS, &vmstats);
	if (error == 0) {
		if (ret_entries)
			*ret_entries = vmstats.num_entries;
		if (ret_tv)
			*ret_tv = vmstats.tv;
		return (vmstats.statbuf);
	} else
		return (NULL);
}

const char *
vm_get_stat_desc(struct vmctx *ctx, int index)
{
	static struct vm_stat_desc statdesc;

	statdesc.index = index;
	if (ioctl(ctx->fd, VM_STAT_DESC, &statdesc) == 0)
		return (statdesc.desc);
	else
		return (NULL);
}

int
vcpu_reset(struct vmctx *vmctx, int vcpu)
{
	return (ENXIO);
}

int
vm_attach_vgic(struct vmctx *ctx, uint64_t distributor_paddr, uint64_t cpu_int_paddr)
{
	struct vm_attach_vgic vav;

	bzero(&vav, sizeof(vav));
	vav.distributor_paddr = distributor_paddr;
	vav.cpu_int_paddr = cpu_int_paddr;

	return (ioctl(ctx->fd, VM_ATTACH_VGIC, &vav));
}

int vm_passthru_memory(struct vmctx *ctx, uint64_t addr, uint64_t size)
{
	struct vm_passthru_memory vpm;

	bzero(&vpm, sizeof(vpm));
	vpm.addr = addr;
	vpm.size = size;

	return (ioctl(ctx->fd, VM_PASSTHRU_MEMORY, &vpm));
}

int
vm_assert_irq(struct vmctx *ctx, uint32_t irq)
{
	struct vm_irq vi;

	bzero(&vi, sizeof(vi));
	vi.irq = irq;
	return (ioctl(ctx->fd, VM_ASSERT_IRQ, &vi));
}

int
vm_deassert_irq(struct vmctx *ctx, uint32_t irq)
{
	struct vm_irq vi;

	bzero(&vi, sizeof(vi));
	vi.irq = irq;
	return (ioctl(ctx->fd, VM_DEASSERT_IRQ, &vi));
}
