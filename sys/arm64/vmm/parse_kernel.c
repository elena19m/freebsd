/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 1998 Peter Wemm <peter@freebsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/link_elf.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_param.h>

#include <machine/vm.h>
#include <machine/vmparam.h>
#include <machine/pmap.h>
#include <machine/elf.h>
#include <machine/vmm.h>

#include "bootparams.h"
#include "mmu.h"

MALLOC_DECLARE(M_HYP);

typedef struct elf_file {
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
} *elf_file_t;

static uint64_t loadimage(void *something_here, elf_file_t ef, uint64_t entry);

static int
load_elf_header(pmap_t guestmap, elf_file_t ef)
{
	Elf_Ehdr  *ehdr;
	vm_paddr_t pa;
	int  err;

	pa = pmap_extract(guestmap, VM_GUEST_BASE_IPA);
	ef->firstpage = (caddr_t)PHYS_TO_DMAP(pa);
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
	ef->firstpage = NULL;
	return (err);
}

int
parse_kernel(pmap_t guestmap, struct vmm_bootparams *bootparams)
{
	struct elf_file ef;
	Elf_Ehdr *ehdr;
	int err;
	size_t kernel_size;

	bzero(&ef, sizeof(struct elf_file));

	err = load_elf_header(guestmap, &ef);
	if (err != 0)
		goto exit;

	ehdr = ef.ehdr;
	if (ehdr->e_type != ET_EXEC) {
		printf("Image not a kernel\n");
		err = EPERM;
		goto exit;
	}
	ef.kernel = 1;

	bootparams->entry_ipa = ehdr->e_entry - VM_MIN_KERNEL_ADDRESS + \
				VM_GUEST_BASE_IPA;
	printf("\tentry_ipa = 0x%zx\n", bootparams->entry_ipa);

	kernel_size = loadimage(NULL, &ef, bootparams->entry_ipa);
	printf("\tkernel_size = %zd\n", kernel_size);

exit:
	return (err);
}

#if 0
int
parse_kernel(uint64_t dest, struct preloaded_file **result)
{
    struct preloaded_file	*fp, *kfp;
    struct elf_file		ef;
    Elf_Ehdr 			*ehdr;
    int				err;

    fp = NULL;
    bzero(&ef, sizeof(struct elf_file));
    ef.fd = -1;

    err = __elfN(load_elf_header)(filename, &ef);
    if (err != 0)
    	return (err);

    ehdr = ef.ehdr;

    fp->f_size = __elfN(loadimage)(fp, &ef, dest);
    if (fp->f_size == 0 || fp->f_addr == 0)
	goto ioerr;

    /* save exec header as metadata */
    file_addmetadata(fp, MODINFOMD_ELFHDR, sizeof(*ehdr), ehdr);

    /* Load OK, return module pointer */
    *result = (struct preloaded_file *)fp;
    err = 0;
    goto out;
    
 ioerr:
    err = EIO;
 oerr:
    file_discard(fp);
 out:
    if (ef.firstpage)
	free(ef.firstpage);
    if (ef.fd != -1)
    	close(ef.fd);
    return(err);
}
#endif

static uint64_t
loadimage(void *something_here, elf_file_t ef, uint64_t entry)
{
	Elf_Ehdr *ehdr;
	Elf_Phdr *phdr;

	ef->off = 0;
	ehdr = ef->ehdr;

	printf("\tehdr->e_phoff = %lu\n", (uint64_t)ehdr->e_phoff);
	phdr = (Elf_Phdr *)(ef->firstpage + ehdr->e_phoff);
	printf("\tphdr = 0x%lx\n", (uint64_t)phdr);

	return 0;
}

#if 0
/*
 * With the file (fd) open on the image, and (ehdr) containing
 * the Elf header, load the image at (off)
 */
static int
__elfN(loadimage)(struct preloaded_file *fp, elf_file_t ef, u_int64_t off)
{
    int 	i;
    u_int	j;
    Elf_Ehdr	*ehdr;
    Elf_Phdr	*phdr, *php;
    Elf_Shdr	*shdr;
    char	*shstr;
    int		ret;
    vm_offset_t firstaddr;
    vm_offset_t lastaddr;
    size_t	chunk;
    ssize_t	result;
    Elf_Addr	ssym, esym;
    Elf_Dyn	*dp;
    Elf_Addr	adp;
    Elf_Addr	ctors;
    int		ndp;
    int		symstrindex;
    int		symtabindex;
    Elf_Size	size;
    u_int	fpcopy;
    Elf_Sym	sym;
    Elf_Addr	p_start, p_end;

    dp = NULL;
    shdr = NULL;
    ret = 0;
    firstaddr = lastaddr = 0;
    ehdr = ef->ehdr;
    ef->off = off;

    if (ef->kernel)
	__elfN(relocation_offset) = off;

    phdr = (Elf_Phdr *)(ef->firstpage + ehdr->e_phoff);

    for (i = 0; i < ehdr->e_phnum; i++) {
	/* We want to load PT_LOAD segments only.. */
	if (phdr[i].p_type != PT_LOAD)
	    continue;

#ifdef ELF_VERBOSE
	printf("Segment: 0x%lx@0x%lx -> 0x%lx-0x%lx",
	    (long)phdr[i].p_filesz, (long)phdr[i].p_offset,
	    (long)(phdr[i].p_vaddr + off),
	    (long)(phdr[i].p_vaddr + off + phdr[i].p_memsz - 1));
#else
	if ((phdr[i].p_flags & PF_W) == 0) {
	    printf("text=0x%lx ", (long)phdr[i].p_filesz);
	} else {
	    printf("data=0x%lx", (long)phdr[i].p_filesz);
	    if (phdr[i].p_filesz < phdr[i].p_memsz)
		printf("+0x%lx", (long)(phdr[i].p_memsz -phdr[i].p_filesz));
	    printf(" ");
	}
#endif
	fpcopy = 0;
	if (ef->firstlen > phdr[i].p_offset) {
	    fpcopy = ef->firstlen - phdr[i].p_offset;
	    archsw.arch_copyin(ef->firstpage + phdr[i].p_offset,
			       phdr[i].p_vaddr + off, fpcopy);
	}
	if (phdr[i].p_filesz > fpcopy) {
	    if (kern_pread(ef->fd, phdr[i].p_vaddr + off + fpcopy,
		phdr[i].p_filesz - fpcopy, phdr[i].p_offset + fpcopy) != 0) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "_loadimage: read failed\n");
		goto out;
	    }
	}
	/* clear space from oversized segments; eg: bss */
	if (phdr[i].p_filesz < phdr[i].p_memsz) {
#ifdef ELF_VERBOSE
	    printf(" (bss: 0x%lx-0x%lx)",
		(long)(phdr[i].p_vaddr + off + phdr[i].p_filesz),
		(long)(phdr[i].p_vaddr + off + phdr[i].p_memsz - 1));
#endif

	    kern_bzero(phdr[i].p_vaddr + off + phdr[i].p_filesz,
		phdr[i].p_memsz - phdr[i].p_filesz);
	}
#ifdef ELF_VERBOSE
	printf("\n");
#endif

	if (archsw.arch_loadseg != NULL)
	    archsw.arch_loadseg(ehdr, phdr + i, off);

	if (firstaddr == 0 || firstaddr > (phdr[i].p_vaddr + off))
	    firstaddr = phdr[i].p_vaddr + off;
	if (lastaddr == 0 || lastaddr < (phdr[i].p_vaddr + off + phdr[i].p_memsz))
	    lastaddr = phdr[i].p_vaddr + off + phdr[i].p_memsz;
    }
    lastaddr = roundup(lastaddr, sizeof(long));

    /*
     * Get the section headers.  We need this for finding the .ctors
     * section as well as for loading any symbols.  Both may be hard
     * to do if reading from a .gz file as it involves seeking.  I
     * think the rule is going to have to be that you must strip a
     * file to remove symbols before gzipping it.
     */
    chunk = ehdr->e_shnum * ehdr->e_shentsize;
    if (chunk == 0 || ehdr->e_shoff == 0)
	goto nosyms;
    shdr = alloc_pread(ef->fd, ehdr->e_shoff, chunk);
    if (shdr == NULL) {
	printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
	    "_loadimage: failed to read section headers");
	goto nosyms;
    }
    file_addmetadata(fp, MODINFOMD_SHDR, chunk, shdr);

    /*
     * Read the section string table and look for the .ctors section.
     * We need to tell the kernel where it is so that it can call the
     * ctors.
     */
    chunk = shdr[ehdr->e_shstrndx].sh_size;
    if (chunk) {
	shstr = alloc_pread(ef->fd, shdr[ehdr->e_shstrndx].sh_offset, chunk);
	if (shstr) {
	    for (i = 0; i < ehdr->e_shnum; i++) {
		if (strcmp(shstr + shdr[i].sh_name, ".ctors") != 0)
		    continue;
		ctors = shdr[i].sh_addr;
		file_addmetadata(fp, MODINFOMD_CTORS_ADDR, sizeof(ctors),
		    &ctors);
		size = shdr[i].sh_size;
		file_addmetadata(fp, MODINFOMD_CTORS_SIZE, sizeof(size),
		    &size);
		break;
	    }
	    free(shstr);
	}
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

    /* Ok, committed to a load. */
#ifndef ELF_VERBOSE
    printf("syms=[");
#endif
    ssym = lastaddr;
    for (i = symtabindex; i >= 0; i = symstrindex) {
#ifdef ELF_VERBOSE
	char	*secname;

	switch(shdr[i].sh_type) {
	    case SHT_SYMTAB:		/* Symbol table */
		secname = "symtab";
		break;
	    case SHT_STRTAB:		/* String table */
		secname = "strtab";
		break;
	    default:
		secname = "WHOA!!";
		break;
	}
#endif

	size = shdr[i].sh_size;
	archsw.arch_copyin(&size, lastaddr, sizeof(size));
	lastaddr += sizeof(size);

#ifdef ELF_VERBOSE
	printf("\n%s: 0x%jx@0x%jx -> 0x%jx-0x%jx", secname,
	    (uintmax_t)shdr[i].sh_size, (uintmax_t)shdr[i].sh_offset,
	    (uintmax_t)lastaddr, (uintmax_t)(lastaddr + shdr[i].sh_size));
#else
	if (i == symstrindex)
	    printf("+");
	printf("0x%lx+0x%lx", (long)sizeof(size), (long)size);
#endif

	if (lseek(ef->fd, (off_t)shdr[i].sh_offset, SEEK_SET) == -1) {
	    printf("\nelf" __XSTRING(__ELF_WORD_SIZE) "_loadimage: could not seek for symbols - skipped!");
	    lastaddr = ssym;
	    ssym = 0;
	    goto nosyms;
	}
	result = archsw.arch_readin(ef->fd, lastaddr, shdr[i].sh_size);
	if (result < 0 || (size_t)result != shdr[i].sh_size) {
	    printf("\nelf" __XSTRING(__ELF_WORD_SIZE) "_loadimage: could not read symbols - skipped! (%ju != %ju)", (uintmax_t)result,
		(uintmax_t)shdr[i].sh_size);
	    lastaddr = ssym;
	    ssym = 0;
	    goto nosyms;
	}
	/* Reset offsets relative to ssym */
	lastaddr += shdr[i].sh_size;
	lastaddr = roundup(lastaddr, sizeof(size));
	if (i == symtabindex)
	    symtabindex = -1;
	else if (i == symstrindex)
	    symstrindex = -1;
    }
    esym = lastaddr;
#ifndef ELF_VERBOSE
    printf("]");
#endif

    file_addmetadata(fp, MODINFOMD_SSYM, sizeof(ssym), &ssym);
    file_addmetadata(fp, MODINFOMD_ESYM, sizeof(esym), &esym);

nosyms:
    printf("\n");

    ret = lastaddr - firstaddr;
    fp->f_addr = firstaddr;

    php = NULL;
    for (i = 0; i < ehdr->e_phnum; i++) {
	if (phdr[i].p_type == PT_DYNAMIC) {
	    php = phdr + i;
	    adp = php->p_vaddr;
	    file_addmetadata(fp, MODINFOMD_DYNAMIC, sizeof(adp), &adp);
	    break;
	}
    }

    if (php == NULL)	/* this is bad, we cannot get to symbols or _DYNAMIC */
	goto out;

    ndp = php->p_filesz / sizeof(Elf_Dyn);
    if (ndp == 0)
	goto out;
    dp = malloc(php->p_filesz);
    if (dp == NULL)
	goto out;
    archsw.arch_copyout(php->p_vaddr + off, dp, php->p_filesz);

    ef->strsz = 0;
    for (i = 0; i < ndp; i++) {
	if (dp[i].d_tag == 0)
	    break;
	switch (dp[i].d_tag) {
	case DT_HASH:
	    ef->hashtab = (Elf_Hashelt*)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_STRTAB:
	    ef->strtab = (char *)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_STRSZ:
	    ef->strsz = dp[i].d_un.d_val;
	    break;
	case DT_SYMTAB:
	    ef->symtab = (Elf_Sym*)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_REL:
	    ef->rel = (Elf_Rel *)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_RELSZ:
	    ef->relsz = dp[i].d_un.d_val;
	    break;
	case DT_RELA:
	    ef->rela = (Elf_Rela *)(uintptr_t)(dp[i].d_un.d_ptr + off);
	    break;
	case DT_RELASZ:
	    ef->relasz = dp[i].d_un.d_val;
	    break;
	default:
	    break;
	}
    }
    if (ef->hashtab == NULL || ef->symtab == NULL ||
	ef->strtab == NULL || ef->strsz == 0)
	goto out;
    COPYOUT(ef->hashtab, &ef->nbuckets, sizeof(ef->nbuckets));
    COPYOUT(ef->hashtab + 1, &ef->nchains, sizeof(ef->nchains));
    ef->buckets = ef->hashtab + 2;
    ef->chains = ef->buckets + ef->nbuckets;

    if (__elfN(lookup_symbol)(fp, ef, "__start_set_modmetadata_set", &sym) != 0)
	return 0;
    p_start = sym.st_value + ef->off;
    if (__elfN(lookup_symbol)(fp, ef, "__stop_set_modmetadata_set", &sym) != 0)
	return ENOENT;
    p_end = sym.st_value + ef->off;

    if (__elfN(parse_modmetadata)(fp, ef, p_start, p_end) == 0)
	goto out;

    if (ef->kernel)			/* kernel must not depend on anything */
	goto out;

out:
    if (dp)
	free(dp);
    if (shdr)
	free(shdr);
    return ret;
}

static char invalid_name[] = "bad";

char *
fake_modname(const char *name)
{
    const char *sp, *ep;
    char *fp;
    size_t len;

    sp = strrchr(name, '/');
    if (sp)
	sp++;
    else
	sp = name;
    ep = strrchr(name, '.');
    if (ep) {
	    if (ep == name) {
		sp = invalid_name;
		ep = invalid_name + sizeof(invalid_name) - 1;
	    } 
    } else
	ep = name + strlen(name);
    len = ep - sp;
    fp = malloc(len + 1);
    if (fp == NULL)
	return NULL;
    memcpy(fp, sp, len);
    fp[len] = '\0';
    return fp;
}

#if (defined(__i386__) || defined(__powerpc__)) && __ELF_WORD_SIZE == 64
struct mod_metadata64 {
	int		md_version;	/* structure version MDTV_* */  
	int		md_type;	/* type of entry MDT_* */
	u_int64_t	md_data;	/* specific data */
	u_int64_t	md_cval;	/* common string label */
};
#endif
#if defined(__amd64__) && __ELF_WORD_SIZE == 32
struct mod_metadata32 {
	int		md_version;	/* structure version MDTV_* */  
	int		md_type;	/* type of entry MDT_* */
	u_int32_t	md_data;	/* specific data */
	u_int32_t	md_cval;	/* common string label */
};
#endif

int
__elfN(load_modmetadata)(struct preloaded_file *fp, u_int64_t dest)
{
	struct elf_file		 ef;
	int			 err, i, j;
	Elf_Shdr		*sh_meta, *shdr = NULL;
	Elf_Shdr		*sh_data[2];
	char			*shstrtab = NULL;
	size_t			 size;
	Elf_Addr		 p_start, p_end;

	bzero(&ef, sizeof(struct elf_file));
	ef.fd = -1;

	err = __elfN(load_elf_header)(fp->f_name, &ef);
	if (err != 0)
		goto out;

	if (ef.kernel == 1 || ef.ehdr->e_type == ET_EXEC) {
		ef.kernel = 1;
	} else if (ef.ehdr->e_type != ET_DYN) {
		err = EFTYPE;
		goto out;
	}

	size = ef.ehdr->e_shnum * ef.ehdr->e_shentsize;
	shdr = alloc_pread(ef.fd, ef.ehdr->e_shoff, size);
	if (shdr == NULL) {
		err = ENOMEM;
		goto out;
	}

	/* Load shstrtab. */
	shstrtab = alloc_pread(ef.fd, shdr[ef.ehdr->e_shstrndx].sh_offset,
	    shdr[ef.ehdr->e_shstrndx].sh_size);
	if (shstrtab == NULL) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "load_modmetadata: unable to load shstrtab\n");
		err = EFTYPE;
		goto out;
	}

	/* Find set_modmetadata_set and data sections. */
	sh_data[0] = sh_data[1] = sh_meta = NULL;
	for (i = 0, j = 0; i < ef.ehdr->e_shnum; i++) {
		if (strcmp(&shstrtab[shdr[i].sh_name],
		    "set_modmetadata_set") == 0) {
			sh_meta = &shdr[i];
		}
		if ((strcmp(&shstrtab[shdr[i].sh_name], ".data") == 0) ||
		    (strcmp(&shstrtab[shdr[i].sh_name], ".rodata") == 0)) {
			sh_data[j++] = &shdr[i];
		}
	}
	if (sh_meta == NULL || sh_data[0] == NULL || sh_data[1] == NULL) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
    "load_modmetadata: unable to find set_modmetadata_set or data sections\n");
		err = EFTYPE;
		goto out;
	}

	/* Load set_modmetadata_set into memory */
	err = kern_pread(ef.fd, dest, sh_meta->sh_size, sh_meta->sh_offset);
	if (err != 0) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
    "load_modmetadata: unable to load set_modmetadata_set: %d\n", err);
		goto out;
	}
	p_start = dest;
	p_end = dest + sh_meta->sh_size;
	dest += sh_meta->sh_size;

	/* Load data sections into memory. */
	err = kern_pread(ef.fd, dest, sh_data[0]->sh_size,
	    sh_data[0]->sh_offset);
	if (err != 0) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "load_modmetadata: unable to load data: %d\n", err);
		goto out;
	}

	/*
	 * We have to increment the dest, so that the offset is the same into
	 * both the .rodata and .data sections.
	 */
	ef.off = -(sh_data[0]->sh_addr - dest);
	dest +=	(sh_data[1]->sh_addr - sh_data[0]->sh_addr);

	err = kern_pread(ef.fd, dest, sh_data[1]->sh_size,
	    sh_data[1]->sh_offset);
	if (err != 0) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "load_modmetadata: unable to load data: %d\n", err);
		goto out;
	}

	err = __elfN(parse_modmetadata)(fp, &ef, p_start, p_end);
	if (err != 0) {
		printf("\nelf" __XSTRING(__ELF_WORD_SIZE)
		    "load_modmetadata: unable to parse metadata: %d\n", err);
		goto out;
	}

out:
	if (shstrtab != NULL)
		free(shstrtab);
	if (shdr != NULL)
		free(shdr);
	if (ef.firstpage != NULL)
		free(ef.firstpage);
	if (ef.fd != -1)
		close(ef.fd);
	return (err);
}

int
__elfN(parse_modmetadata)(struct preloaded_file *fp, elf_file_t ef,
    Elf_Addr p_start, Elf_Addr p_end)
{
    struct mod_metadata md;
#if (defined(__i386__) || defined(__powerpc__)) && __ELF_WORD_SIZE == 64
    struct mod_metadata64 md64;
#elif defined(__amd64__) && __ELF_WORD_SIZE == 32
    struct mod_metadata32 md32;
#endif
    struct mod_depend *mdepend;
    struct mod_version mver;
    char *s;
    int error, modcnt, minfolen;
    Elf_Addr v, p;

    modcnt = 0;
    p = p_start;
    while (p < p_end) {
	COPYOUT(p, &v, sizeof(v));
	error = __elfN(reloc_ptr)(fp, ef, p, &v, sizeof(v));
	if (error == EOPNOTSUPP)
	    v += ef->off;
	else if (error != 0)
	    return (error);
#if (defined(__i386__) || defined(__powerpc__)) && __ELF_WORD_SIZE == 64
	COPYOUT(v, &md64, sizeof(md64));
	error = __elfN(reloc_ptr)(fp, ef, v, &md64, sizeof(md64));
	if (error == EOPNOTSUPP) {
	    md64.md_cval += ef->off;
	    md64.md_data += ef->off;
	} else if (error != 0)
	    return (error);
	md.md_version = md64.md_version;
	md.md_type = md64.md_type;
	md.md_cval = (const char *)(uintptr_t)md64.md_cval;
	md.md_data = (void *)(uintptr_t)md64.md_data;
#elif defined(__amd64__) && __ELF_WORD_SIZE == 32
	COPYOUT(v, &md32, sizeof(md32));
	error = __elfN(reloc_ptr)(fp, ef, v, &md32, sizeof(md32));
	if (error == EOPNOTSUPP) {
	    md32.md_cval += ef->off;
	    md32.md_data += ef->off;
	} else if (error != 0)
	    return (error);
	md.md_version = md32.md_version;
	md.md_type = md32.md_type;
	md.md_cval = (const char *)(uintptr_t)md32.md_cval;
	md.md_data = (void *)(uintptr_t)md32.md_data;
#else
	COPYOUT(v, &md, sizeof(md));
	error = __elfN(reloc_ptr)(fp, ef, v, &md, sizeof(md));
	if (error == EOPNOTSUPP) {
	    md.md_cval += ef->off;
	    md.md_data = (void *)((uintptr_t)md.md_data + (uintptr_t)ef->off);
	} else if (error != 0)
	    return (error);
#endif
	p += sizeof(Elf_Addr);
	switch(md.md_type) {
	  case MDT_DEPEND:
	    if (ef->kernel)		/* kernel must not depend on anything */
	      break;
	    s = strdupout((vm_offset_t)md.md_cval);
	    minfolen = sizeof(*mdepend) + strlen(s) + 1;
	    mdepend = malloc(minfolen);
	    if (mdepend == NULL)
		return ENOMEM;
	    COPYOUT((vm_offset_t)md.md_data, mdepend, sizeof(*mdepend));
	    strcpy((char*)(mdepend + 1), s);
	    free(s);
	    file_addmetadata(fp, MODINFOMD_DEPLIST, minfolen, mdepend);
	    free(mdepend);
	    break;
	  case MDT_VERSION:
	    s = strdupout((vm_offset_t)md.md_cval);
	    COPYOUT((vm_offset_t)md.md_data, &mver, sizeof(mver));
	    file_addmodule(fp, s, mver.mv_version, NULL);
	    free(s);
	    modcnt++;
	    break;
	}
    }
    if (modcnt == 0) {
	s = fake_modname(fp->f_name);
	file_addmodule(fp, s, 1, NULL);
	free(s);
    }
    return 0;
}

static unsigned long
elf_hash(const char *name)
{
    const unsigned char *p = (const unsigned char *) name;
    unsigned long h = 0;
    unsigned long g;

    while (*p != '\0') {
	h = (h << 4) + *p++;
	if ((g = h & 0xf0000000) != 0)
	    h ^= g >> 24;
	h &= ~g;
    }
    return h;
}

static const char __elfN(bad_symtable)[] = "elf" __XSTRING(__ELF_WORD_SIZE) "_lookup_symbol: corrupt symbol table\n";
int
__elfN(lookup_symbol)(struct preloaded_file *fp, elf_file_t ef, const char* name,
		  Elf_Sym *symp)
{
    Elf_Hashelt symnum;
    Elf_Sym sym;
    char *strp;
    unsigned long hash;

    hash = elf_hash(name);
    COPYOUT(&ef->buckets[hash % ef->nbuckets], &symnum, sizeof(symnum));

    while (symnum != STN_UNDEF) {
	if (symnum >= ef->nchains) {
	    printf(__elfN(bad_symtable));
	    return ENOENT;
	}

	COPYOUT(ef->symtab + symnum, &sym, sizeof(sym));
	if (sym.st_name == 0) {
	    printf(__elfN(bad_symtable));
	    return ENOENT;
	}

	strp = strdupout((vm_offset_t)(ef->strtab + sym.st_name));
	if (strcmp(name, strp) == 0) {
	    free(strp);
	    if (sym.st_shndx != SHN_UNDEF ||
		(sym.st_value != 0 &&
		 ELF_ST_TYPE(sym.st_info) == STT_FUNC)) {
		*symp = sym;
		return 0;
	    }
	    return ENOENT;
	}
	free(strp);
	COPYOUT(&ef->chains[symnum], &symnum, sizeof(symnum));
    }
    return ENOENT;
}

/*
 * Apply any intra-module relocations to the value. p is the load address
 * of the value and val/len is the value to be modified. This does NOT modify
 * the image in-place, because this is done by kern_linker later on.
 *
 * Returns EOPNOTSUPP if no relocation method is supplied.
 */
static int
__elfN(reloc_ptr)(struct preloaded_file *mp, elf_file_t ef,
    Elf_Addr p, void *val, size_t len)
{
	size_t n;
	Elf_Rela a;
	Elf_Rel r;
	int error;

	/*
	 * The kernel is already relocated, but we still want to apply
	 * offset adjustments.
	 */
	if (ef->kernel)
		return (EOPNOTSUPP);

	for (n = 0; n < ef->relsz / sizeof(r); n++) {
		COPYOUT(ef->rel + n, &r, sizeof(r));

		error = __elfN(reloc)(ef, __elfN(symaddr), &r, ELF_RELOC_REL,
		    ef->off, p, val, len);
		if (error != 0)
			return (error);
	}
	for (n = 0; n < ef->relasz / sizeof(a); n++) {
		COPYOUT(ef->rela + n, &a, sizeof(a));

		error = __elfN(reloc)(ef, __elfN(symaddr), &a, ELF_RELOC_RELA,
		    ef->off, p, val, len);
		if (error != 0)
			return (error);
	}

	return (0);
}

static Elf_Addr
__elfN(symaddr)(struct elf_file *ef, Elf_Size symidx)
{

	/* Symbol lookup by index not required here. */
	return (0);
}
#endif
