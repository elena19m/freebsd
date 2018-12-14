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

#ifndef	_VMM_DEV_H_
#define	_VMM_DEV_H_

#ifdef _KERNEL
void	vmmdev_init(void);
int	vmmdev_cleanup(void);
#endif

struct vm_memory_segment {
	uint64_t	gpa;	/* in */
	size_t		len;
	int		wired;
};

struct vm_register {
	int		cpuid;
	int		regnum;		/* enum vm_reg_name */
	uint64_t	regval;
};

struct vm_run {
	int		cpuid;
	uint64_t	pc;
	struct vm_exit	vm_exit;

};

struct vm_exception {
	int		cpuid;
	int		vector;
	uint32_t	error_code;
	int		error_code_valid;
	int		restart_instruction;
};

struct vm_capability {
	int		cpuid;
	enum vm_cap_type captype;
	int		capval;
	int		allcpus;
};

#define	MAX_VM_STATS	64
struct vm_stats {
	int		cpuid;				/* in */
	int		num_entries;			/* out */
	struct timeval	tv;
	uint64_t	statbuf[MAX_VM_STATS];
};
struct vm_stat_desc {
	int		index;				/* in */
	char		desc[128];			/* out */
};


struct vm_suspend {
	enum vm_suspend_how how;
};

struct vm_gla2gpa {
	int		vcpuid;		/* inputs */
	int 		prot;		/* PROT_READ or PROT_WRITE */
	uint64_t	gla;
	int		fault;		/* outputs */
	uint64_t	gpa;
};

struct vm_activate_cpu {
	int		vcpuid;
};

struct vm_attach_vgic {
	uint64_t	dist_start;
	size_t		dist_size;
	uint64_t	redist_start;
	size_t		redist_size;
};

struct vm_irq {
	uint32_t irq;
};

#define	VM_ACTIVE_CPUS		0
#define	VM_SUSPENDED_CPUS	1

enum {
	/* general routines */
	IOCNUM_ABIVERS = 0,
	IOCNUM_RUN = 1,
	IOCNUM_SET_CAPABILITY = 2,
	IOCNUM_GET_CAPABILITY = 3,
	IOCNUM_SUSPEND = 4,
	IOCNUM_REINIT = 5,

	/* memory apis */
	IOCNUM_MAP_MEMORY = 10,
	IOCNUM_GET_MEMORY_SEG = 11,
	IOCNUM_GET_GPA_PMAP = 12,
	IOCNUM_GLA2GPA = 13,

	/* register/state accessors */
	IOCNUM_SET_REGISTER = 20,
	IOCNUM_GET_REGISTER = 21,

	/* statistics */
	IOCNUM_VM_STATS = 50, 
	IOCNUM_VM_STAT_DESC = 51,

	/* interrupt injection */
	IOCNUM_ASSERT_IRQ = 80,
	IOCNUM_DEASSERT_IRQ = 81,

	/* vm_cpuset */
	IOCNUM_ACTIVATE_CPU = 90,
	IOCNUM_GET_CPUSET = 91,

	/* vm_attach_vgic */
	IOCNUM_ATTACH_VGIC = 110,
};

#define	VM_RUN		\
	_IOWR('v', IOCNUM_RUN, struct vm_run)
#define	VM_SUSPEND	\
	_IOW('v', IOCNUM_SUSPEND, struct vm_suspend)
#define	VM_REINIT	\
	_IO('v', IOCNUM_REINIT)
#define	VM_MAP_MEMORY	\
	_IOWR('v', IOCNUM_MAP_MEMORY, struct vm_memory_segment)
#define	VM_GET_MEMORY_SEG \
	_IOWR('v', IOCNUM_GET_MEMORY_SEG, struct vm_memory_segment)
#define	VM_SET_REGISTER \
	_IOW('v', IOCNUM_SET_REGISTER, struct vm_register)
#define	VM_GET_REGISTER \
	_IOWR('v', IOCNUM_GET_REGISTER, struct vm_register)
#define	VM_SET_CAPABILITY \
	_IOW('v', IOCNUM_SET_CAPABILITY, struct vm_capability)
#define	VM_GET_CAPABILITY \
	_IOWR('v', IOCNUM_GET_CAPABILITY, struct vm_capability)
#define	VM_STATS \
	_IOWR('v', IOCNUM_VM_STATS, struct vm_stats)
#define	VM_STAT_DESC \
	_IOWR('v', IOCNUM_VM_STAT_DESC, struct vm_stat_desc)
#define VM_ASSERT_IRQ \
	_IOW('v', IOCNUM_ASSERT_IRQ, struct vm_irq)
#define VM_DEASSERT_IRQ \
	_IOW('v', IOCNUM_DEASSERT_IRQ, struct vm_irq)
#define	VM_GLA2GPA	\
	_IOWR('v', IOCNUM_GLA2GPA, struct vm_gla2gpa)
#define	VM_ACTIVATE_CPU	\
	_IOW('v', IOCNUM_ACTIVATE_CPU, struct vm_activate_cpu)
#define	VM_GET_CPUS	\
	_IOW('v', IOCNUM_GET_CPUSET, struct vm_cpuset)
#define	VM_ATTACH_VGIC	\
	_IOW('v', IOCNUM_ATTACH_VGIC, struct vm_attach_vgic)
#endif
