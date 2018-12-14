/*-
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VMMAPI_H_
#define	_VMMAPI_H_

struct vmctx;
struct vm_exit;
enum vm_cap_type;

/*
 * Different styles of mapping the memory assigned to a VM into the address
 * space of the controlling process.
 */
enum vm_mmap_style {
	VM_MMAP_NONE,		/* no mapping */
	VM_MMAP_ALL,		/* fully and statically mapped */
	VM_MMAP_SPARSE,		/* mappings created on-demand */
};

int	vm_create(const char *name);
struct vmctx *vm_open(const char *name);
void	vm_destroy(struct vmctx *ctx);
int	vm_parse_memsize(const char *optarg, size_t *memsize);
int	vm_get_memory_seg(struct vmctx *ctx, uint64_t gpa, size_t *ret_len);
int	vm_setup_memory(struct vmctx *ctx, uint64_t membase, size_t len, enum vm_mmap_style s);
void	*vm_map_ipa(struct vmctx *ctx, uint64_t gaddr, size_t len);
uint32_t vm_get_mem_limit(struct vmctx *ctx);
void	vm_set_mem_limit(struct vmctx *ctx, uint32_t limit);
int	vm_set_register(struct vmctx *ctx, int vcpu, int reg, uint64_t val);
int	vm_get_register(struct vmctx *ctx, int vcpu, int reg, uint64_t *retval);
int	vm_run(struct vmctx *ctx, int vcpu, uint64_t rip,
	       struct vm_exit *ret_vmexit);
const char *vm_capability_type2name(int type);
int	vm_get_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
			  int *retval);
int	vm_set_capability(struct vmctx *ctx, int vcpu, enum vm_cap_type cap,
			  int val);
int	vm_assert_irq(struct vmctx *ctx, uint32_t irq);
int	vm_deassert_irq(struct vmctx *ctx, uint32_t irq);

/*
 * Return a pointer to the statistics buffer. Note that this is not MT-safe.
 */
uint64_t *vm_get_stats(struct vmctx *ctx, int vcpu, struct timeval *ret_tv,
		       int *ret_entries);
const char *vm_get_stat_desc(struct vmctx *ctx, int index);

/* Reset vcpu register state */
int	vcpu_reset(struct vmctx *ctx, int vcpu);

int	vm_attach_vgic(struct vmctx *ctx, uint64_t dist_start, size_t dist_size,
		uint64_t redist_start, size_t redist_size);
#endif	/* _VMMAPI_H_ */
