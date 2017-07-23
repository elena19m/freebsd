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

#include <machine/vmm.h>

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

#define	MB	(1024 * 1024UL)
#define	GB	(1024 * 1024 * 1024UL)
#define	BSP	0
#define KERNEL_IMAGE_NAME_LEN	32

static char *vmname, *progname;
static struct vmctx *ctx;


/*
 * Guest virtual machinee
 */
static int
guest_copyin(const void *from, uint64_t to, size_t size)
{
	char *ptr;
	ptr = vm_map_gpa(ctx, to, size);
	if (ptr == NULL)
		return (EFAULT);

	memcpy(ptr, from, size);
	return (0);
}

static int
guest_copyout(uint64_t from, void *to, size_t size)
{
	char *ptr;

	ptr = vm_map_gpa(ctx, from, size);
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
	if (error) {
		perror("vm_set_register");
	}
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: %s [-k <kernel-image>] -l <kernel-load-address>, -b <memory-base-address>\n"
	    "       %*s [-m mem-size] [-p periphbase] <vmname>\n",
	    progname,
	    (int)strlen(progname), "");
	exit(1);
}

int
main(int argc, char** argv)
{
	uint64_t mem_size;
	int opt, error;
	int kernel_image_fd;
	uint64_t kernel_load_address, memory_base_address;
	uint64_t periphbase;
	char kernel_image_name[KERNEL_IMAGE_NAME_LEN];
	struct stat st;
	void *addr;

	progname = basename(argv[0]);

	mem_size = 128 * MB;
	kernel_load_address = 0xc0000000;
	memory_base_address = 0xc0000000;
	periphbase = 0x2c000000;
	strncpy(kernel_image_name, "kernel.bin", KERNEL_IMAGE_NAME_LEN);

	while ((opt = getopt(argc, argv, "k:l:b:m:p")) != -1) {
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
			mem_size = strtoul(optarg, NULL, 0) * MB;
			break;
		case 'p':
			periphbase = strtoul(optarg, NULL, 0);
		case '?':
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

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

	addr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, kernel_image_fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap kernel_image_fd");
		exit(1);
	}

	if (guest_copyin(addr, kernel_load_address - memory_base_address, st.st_size)) {
		perror("guest_copyin");
		exit(1);
	}

	error = vm_attach_vgic(ctx, periphbase + 0x1000, periphbase + 0x2000);
	if (error) {
	}
	munmap(addr, st.st_size);

	guest_setreg(VM_REG_GUEST_PC, kernel_load_address);
	return 0;
}
