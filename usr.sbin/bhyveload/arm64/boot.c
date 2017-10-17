/*- * Copyright (c) 2017 Alexandru Elisei <alexandru.elisei@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/elf.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/linker.h>
#include <sys/errno.h>

#include <machine/vmm.h>
#include <machine/vmparam.h>
#include <bootstrap.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "boot.h"

struct elf_file {
    Elf_Phdr 	*ph;
    Elf_Ehdr	*ehdr;
    Elf_Sym	*symtab;
    Elf_Hashelt	*hashtab;
    Elf_Hashelt	nbuckets;
    Elf_Hashelt	nchains;
    Elf_Hashelt	*buckets;
    Elf_Hashelt	*chains;
    Elf_Rel	*rel;
    size_t	relsz;
    Elf_Rela	*rela;
    size_t	relasz;
    char	*strtab;
    size_t	strsz;
    int		fd;
    caddr_t	firstpage;
    size_t	firstlen;
    int		kernel;
    u_int64_t	off;
};

static uint64_t parse_image(struct preloaded_file *img, struct elf_file *ef);
static void	image_addmetadata(struct preloaded_file *img, int type,
			size_t size, vm_offset_t addr);

static int
load_elf_header(struct elf_file *ef)
{
	Elf_Ehdr  *ehdr;
	int  err;

	ehdr = ef->ehdr = (Elf_Ehdr *)ef->firstpage;
	/* Is it ELF? */
	if (!IS_ELF(*ehdr)) {
		err = EFTYPE;
		goto error;
	}
	if (ehdr->e_ident[EI_CLASS] != ELF_TARG_CLASS || /* Layout ? */
	    ehdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    ehdr->e_ident[EI_VERSION] != EV_CURRENT || /* Version ? */
	    ehdr->e_version != EV_CURRENT ||
	    ehdr->e_machine != ELF_TARG_MACH) { /* Machine ? */
		err = EFTYPE;
		goto error;
	}

	return (0);

error:
	return (err);
}

int
parse_kernel(void *addr, struct vm_bootparams *bootparams)
{
	struct elf_file ef;
	struct preloaded_file img;
	Elf_Ehdr *ehdr;
	int err;
	size_t kernel_size;

	memset(&ef, 0, sizeof(struct elf_file));

	ef.firstpage = (caddr_t)addr;
	err = load_elf_header(&ef);
	if (err != 0)
		goto exit;

	ehdr = ef.ehdr;
	if (ehdr->e_type != ET_EXEC) {
		printf("Image not a kernel\n");
		err = EPERM;
		goto exit;
	}
	ef.kernel = 1;

	printf("\tsizeof(Elf_Ehdr) = %lu\n", sizeof(Elf_Ehdr));
	printf("\tef->firstlen = %zd\n", ef.firstlen);
	printf("\tehdr->e_shoff = %lu\n", ehdr->e_shoff);

	img.f_addr = VM_GUEST_BASE_IPA;
	kernel_size = parse_image(&img, &ef);
	if (kernel_size == 0) {
		err = EIO;
		goto exit;
	}
	img.f_size = kernel_size;

	image_addmetadata(&img, MODINFOMD_ELFHDR, sizeof(*ehdr), (vm_offset_t)ehdr);

	bootparams->entry = ehdr->e_entry - KERNBASE;

	printf("\tkernel_size = %zd\n", kernel_size);
	printf("\tentry = 0x%zx\n", bootparams->entry);

exit:
	return (err);
}

static uint64_t
parse_image(struct preloaded_file *img, struct elf_file *ef)
{
	Elf_Ehdr *ehdr;
	Elf_Phdr *phdr;
	Elf_Shdr *shdr;
	Elf_Addr ctors;
	Elf_Size size;
	vm_offset_t firstaddr, lastaddr;
	vm_offset_t shstr_addr;
	char *shstr;
	int symstrindex;
	int symtabindex;
	size_t chunk_len;
	int i;
	unsigned int j;

	printf("\n\t\tLOADIMAGE\n\n");

	ef->off = 0;
	ehdr = ef->ehdr;
	phdr = (Elf_Phdr *)(ef->firstpage + ehdr->e_phoff);

	firstaddr = lastaddr = 0;
	for (i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD)
			continue;
		if (firstaddr == 0 || firstaddr > phdr[i].p_vaddr)
		    firstaddr = phdr[i].p_vaddr;
		/* We mmap'ed the kernel, so p_memsz == p_filesz. */
		if (lastaddr == 0 || lastaddr < (phdr[i].p_vaddr + phdr[i].p_filesz))
		    lastaddr = phdr[i].p_vaddr + phdr[i].p_filesz;
	}
	lastaddr = roundup(lastaddr, sizeof(long));

	/*
	 * Get the section headers.  We need this for finding the .ctors
	 * section as well as for loading any symbols.  Both may be hard
	 * to do if reading from a .gz file as it involves seeking.  I
	 * think the rule is going to have to be that you must strip a
	 * file to remove symbols before gzipping it.
	 */
	chunk_len = ehdr->e_shnum * ehdr->e_shentsize;
	if (chunk_len == 0 || ehdr->e_shoff == 0)
		goto nosyms;
	shdr = (Elf_Shdr *)(ef->firstpage + ehdr->e_shoff);
	image_addmetadata(img, MODINFOMD_SHDR, chunk_len, (vm_offset_t)shdr);

	printf("\tshdr = 0x%016lx\n", (uint64_t)shdr);
	printf("\tshdr[1].sh_type = %d\n", shdr[1].sh_type);

	/*
	 * Read the section string table and look for the .ctors section.
	 * We need to tell the kernel where it is so that it can call the
	 * ctors.
	 */
	chunk_len = shdr[ehdr->e_shstrndx].sh_size;
	if (chunk_len > 0) {
		shstr_addr = (vm_offset_t)(ef->firstpage + shdr[ehdr->e_shstrndx].sh_offset);
		shstr = malloc(chunk_len);
		memcpy(shstr, (void *)shstr_addr, chunk_len);
		for (i = 0; i < ehdr->e_shnum; i++) {
			if (strcmp(shstr + shdr[i].sh_name, ".ctors") != 0)
				continue;
			ctors = shdr[i].sh_addr;
			image_addmetadata(img, MODINFOMD_CTORS_ADDR,
					sizeof(ctors), (vm_offset_t)&ctors);
			size = shdr[i].sh_size;
			image_addmetadata(img, MODINFOMD_CTORS_SIZE,
					sizeof(size), (vm_offset_t)&size);
			break;
		}
		free(shstr);
	}

	/*
	 * Now load any symbols.
	 */
	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < ehdr->e_shnum; i++) {
		if (shdr[i].sh_type != SHT_SYMTAB)
			continue;
		for (j = 0; j < ehdr->e_phnum; j++) {
			if (phdr[j].p_type != PT_LOAD)
				continue;
			if (shdr[i].sh_offset >= phdr[j].p_offset &&
					(shdr[i].sh_offset + shdr[i].sh_size <=
					 phdr[j].p_offset + phdr[j].p_filesz)) {
				shdr[i].sh_offset = 0;
				shdr[i].sh_size = 0;
				break;
			}
		}
		if (shdr[i].sh_offset == 0 || shdr[i].sh_size == 0)
			continue;		/* alread loaded in a PT_LOAD above */
		/* Save it for loading below */
		symtabindex = i;
		symstrindex = shdr[i].sh_link;
	}
	if (symtabindex < 0 || symstrindex < 0)
		goto nosyms;

	printf("\tsymtabindex = %d\n", symtabindex);
	printf("\tsymstrindex = %d\n", symstrindex);
	printf("\n");

nosyms:
	return 1;
}

static void
image_addmetadata(struct preloaded_file *img, int type,
		size_t size, vm_offset_t addr)
{
	struct file_metadata *md;

	md = malloc(sizeof(struct file_metadata) - sizeof(md->md_data) + size);
	md->md_size = size;
	md->md_type = type;
	memcpy(md->md_data, (void *)addr, size);
	md->md_next = img->f_metadata;
	img->f_metadata = md;
}
