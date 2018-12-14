/*
 * Copyright (C) 2015 Mihai Carabas <mihai.carabas@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/queue.h>

#include <machine/vmm.h>
#include <machine/vmparam.h>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>
#include <vmmapi.h>

#include <libutil.h>

#include "boot.h"

#define	gvatovm(addr)		((uint64_t)(addr) - KERNBASE + 	\
				kernel_load_address - memory_base_address)
#define	overlap(x_start, x_end, y_start, y_end)					\
			((x_start) >= (y_start) && (x_start) < (y_end) || 	\
		 	(x_end) >= (y_start) && (x_end) < (y_end))

#define	MB			(1024 * 1024UL)
#define	BSP			0
#define	KERNEL_IMAGE_NAME_LEN	32

#define	GIC_V3_DIST_START	0x2f000000UL
#define	GIC_V3_DIST_SIZE	0x10000UL
#define	GIC_V3_REDIST_START	0x2f100000UL
#define	GIC_V3_REDIST_SIZE	0x200000UL

struct env {
	const char *str;
	SLIST_ENTRY(env) next;
};
static SLIST_HEAD(envhead, env) envhead;

static uint64_t memory_base_address, kernel_load_address;

static char *vmname, *progname;
static struct vmctx *ctx;

static int
env_add(const char *str)
{
	struct env *env;

	env = malloc(sizeof(*env));
	if (env == NULL)
		return (ENOMEM);
	env->str = str;
	SLIST_INSERT_HEAD(&envhead, env, next);

	return (0);
}

static int
env_tostr(char **envstrp, int *envlen)
{
	struct env *env;
	int i;

	*envlen = 0;
	SLIST_FOREACH(env, &envhead, next)
		*envlen = *envlen + strlen(env->str) + 1;
	/* Make room for the two terminating zeroes */
	if (*envlen == 0)
		*envlen = 2;
	else
		(*envlen)++;

	*envstrp = malloc(*envlen * sizeof(char));
	if (*envstrp == NULL)
		return (ENOMEM);

	i = 0;
	SLIST_FOREACH(env, &envhead, next) {
		strncpy(*envstrp + i, env->str, strlen(env->str));
		i += strlen(env->str);
		(*envstrp)[i++] = 0;
	}
	(*envstrp)[i] = 0;

	/*
	 * At this point we have envstr[0] == 0 if the environment is empty.
	 * Add the second 0 to properly terminate the environment string.
	 */
	if (SLIST_EMPTY(&envhead))
		(*envstrp)[1] = 0;

	/*
	for (i = 0; i < *envlen; i++)
		printf("%d ", (int)(*envstrp)[i]);
	printf("\n");
	*/

	return (0);
}

/*
 * Guest virtual machinee
 */
static int
guest_copyin(const void *from, uint64_t to, size_t size)
{
	char *ptr;
	ptr = vm_map_ipa(ctx, to, size);
	if (ptr == NULL)
		return (EFAULT);

	memcpy(ptr, from, size);
	return (0);
}

static int
guest_copyout(uint64_t from, void *to, size_t size)
{
	char *ptr;

	ptr = vm_map_ipa(ctx, from, size);
	if (ptr == NULL)
		return (EFAULT);

	memcpy(to, ptr, size);
	return (0);
}

static void
guest_setreg(enum vm_reg_name vmreg, uint64_t v)
{
	int error;

	error = vm_set_register(ctx, BSP, vmreg, v);
	if (error)
		perror("vm_set_register");
}

#if 0
static int
parse_memsize(const char *optarg, size_t *ret_memsize)
{
	char *endptr;
	size_t optval;
	int error;

	optval = strtoul(optarg, &endptr, 0);
	if (*optarg != '\0' && *endptr == '\0') {
		/* Memory size must be at least one megabyte. */
		if (optval < MB)
			optval = optval * MB;
		*ret_memsize = optval;
		error = 0;
	} else {
		error = expand_number(optarg, ret_memsize);
	}

	return (error);
}
#endif

static void
usage(int code)
{
	fprintf(stderr,
	    "Usage: %s [-h] [-k <kernel-image>] [-e <name=value>] [-b base-address]\n"
	    "       %*s [-m mem-size] [-l load-address] <vmname>\n"
	    "       -k: path to guest kernel image\n"
	    "       -e: guest boot environment\n"
	    "       -b: memory base address\n"
	    "       -m: memory size\n"
	    "       -l: kernel load address in the guest physical memory\n"
	    "       -h: help\n",
	    progname, (int)strlen(progname), "");
	exit(code);
}

int
main(int argc, char** argv)
{
	struct vm_bootparams bootparams;
	uint64_t mem_size;
	int opt, error;
	int kernel_image_fd;
	uint64_t periphbase;
	char kernel_image_name[KERNEL_IMAGE_NAME_LEN];
	struct stat st;
	void *addr;
	char *envstr;
	int envlen;

	progname = basename(argv[0]);

	mem_size = 128 * MB;
	memory_base_address = VM_GUEST_BASE_IPA;
	kernel_load_address = memory_base_address;
	periphbase = 0x2c000000UL;
	strncpy(kernel_image_name, "kernel.bin", KERNEL_IMAGE_NAME_LEN);
	memset(&bootparams, 0, sizeof(struct vm_bootparams));

	while ((opt = getopt(argc, argv, "hk:l:b:m:e:")) != -1) {
		switch (opt) {
		case 'k':
			strncpy(kernel_image_name, optarg, KERNEL_IMAGE_NAME_LEN);
			break;
		case 'l':
			kernel_load_address = strtoul(optarg, NULL, 0);
			break;
		case 'b':
			memory_base_address = strtoul(optarg, NULL, 0);
			break;
		case 'm':
			error = vm_parse_memsize(optarg, &mem_size);
			if (error) {
				fprintf(stderr, "Invalid memsize '%s'\n", optarg);
				exit(1);
			}
			break;
		case 'e':
			error = env_add(optarg);
			if (error) {
				perror("env_add");
				exit(1);
			}
			break;
		case 'h':
			usage(0);
		default:
			fprintf(stderr, "Unknown argument '%c'\n", opt);
			usage(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		fprintf(stderr, "Missing or unknown arguments\n");
		usage(1);
	}

	if (kernel_load_address < memory_base_address) {
		fprintf(stderr, "Kernel load address is below memory base address\n");
		exit(1);
	}

	vmname = argv[0];

	kernel_image_fd = open(kernel_image_name, O_RDONLY);
	if (kernel_image_fd == -1) {
		perror("open kernel_image_name");
		exit(1);
	}

	error = vm_create(vmname);
	if (error) {
		perror("vm_create");
		exit(1);
	}

	ctx = vm_open(vmname);
	if (ctx == NULL) {
		perror("vm_open");
		exit(1);
	}

	error = vm_setup_memory(ctx, memory_base_address, mem_size, VM_MMAP_ALL);
	if (error) {
		perror("vm_setup_memory");
		exit(1);
	}

	error = fstat(kernel_image_fd, &st);
	if (error) {
		perror("fstat");
		exit(1);
	}

	if ((uint64_t)st.st_size > mem_size) {
		fprintf(stderr, "Kernel image larger than memory size\n");
		exit(1);
	}
	if (kernel_load_address + st.st_size >= memory_base_address + mem_size) {
		fprintf(stderr, "Kernel image out of bounds of guest memory\n");
		exit(1);
	}

	addr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, kernel_image_fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap kernel_image_fd");
		exit(1);
	}

	if (guest_copyin(addr, kernel_load_address - memory_base_address, st.st_size) != 0) {
		perror("guest_copyin");
		exit(1);
	}

	error = env_tostr(&envstr, &envlen);
	if (error) {
		perror("parse boot environment\n");
		exit(1);
	}

	bootparams.envstr = envstr;
	bootparams.envlen = envlen;
	error = parse_kernel(addr, st.st_size, ctx, &bootparams);
	if (error) {
		fprintf(stderr, "Error parsing image\n");
		exit(1);
	}

	/*
	fprintf(stderr, "bootparams.envp_gva = 0x%016lx\n", bootparams.envp_gva);
	fprintf(stderr, "gvatom(bootparams.envp_gva) = 0x%016lx\n", gvatovm(bootparams.envp_gva));
	fprintf(stderr, "vm_map_ipa() = 0x%016lx\n", (uint64_t)vm_map_ipa(ctx, gvatovm(bootparams.envp_gva), PAGE_SIZE));
	fprintf(stderr, "\n");

	fprintf(stderr, "bootparams.mudulep_gva = 0x%016lx\n", bootparams.modulep_gva);
	fprintf(stderr, "gvatom(bootparams.modulep_gva) = 0x%016lx\n", gvatovm(bootparams.modulep_gva));
	fprintf(stderr, "vm_map_ipa() = 0x%016lx\n", (uint64_t)vm_map_ipa(ctx, gvatovm(bootparams.modulep_gva), PAGE_SIZE));
	fprintf(stderr, "\n");
	*/

	/* Copy the environment string in the guest memory */
	if (guest_copyin((void *)envstr, gvatovm(bootparams.envp_gva), envlen) != 0) {
		perror("guest_copyin");
		exit(1);
	}

	/* Copy the module data in the guest memory */
	if (guest_copyin(bootparams.modulep, gvatovm(bootparams.modulep_gva), bootparams.module_len) != 0) {
		perror("guest_copyin");
		exit(1);
	}

	uint64_t mem_end = memory_base_address + mem_size;
	uint64_t dist_end = GIC_V3_DIST_START + GIC_V3_DIST_SIZE;
	uint64_t redist_end = GIC_V3_REDIST_START + GIC_V3_REDIST_SIZE;

	if (overlap(GIC_V3_DIST_SIZE, dist_end, memory_base_address, mem_end)) {
		fprintf(stderr, "Guest memory overlaps with VGIC Distributor\n");
		exit(1);
	}

	if (overlap(GIC_V3_REDIST_SIZE, redist_end, memory_base_address, mem_end)) {
		fprintf(stderr, "Guest memory overlaps with VGIC Redistributor\n");
		exit(1);
	}

	error = vm_attach_vgic(ctx, GIC_V3_DIST_START, GIC_V3_DIST_SIZE,
			GIC_V3_REDIST_START, GIC_V3_REDIST_SIZE);
	if (error) {
		fprintf(stderr, "Error attaching VGIC to the virtual machine\n");
		exit(1);
	}

	munmap(addr, st.st_size);

	guest_setreg(VM_REG_ELR_EL2, kernel_load_address + bootparams.entry_off);
	guest_setreg(VM_REG_GUEST_X0, bootparams.modulep_gva);

	return 0;
}
