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

#ifndef _VMM_H_
#define	_VMM_H_

#include <sys/param.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include "pte.h"
#include "pmap.h"

enum vm_suspend_how {
	VM_SUSPEND_NONE,
	VM_SUSPEND_RESET,
	VM_SUSPEND_POWEROFF,
	VM_SUSPEND_HALT,
	VM_SUSPEND_TRIPLEFAULT,
	VM_SUSPEND_LAST
};

/*
 * Identifiers for architecturally defined registers.
 */
enum vm_reg_name {
	VM_REG_GUEST_X0,
	VM_REG_GUEST_X1,
	VM_REG_GUEST_X2,
	VM_REG_GUEST_X3,
	VM_REG_GUEST_X4,
	VM_REG_GUEST_X5,
	VM_REG_GUEST_X6,
	VM_REG_GUEST_X7,
	VM_REG_GUEST_X8,
	VM_REG_GUEST_X9,
	VM_REG_GUEST_X10,
	VM_REG_GUEST_X11,
	VM_REG_GUEST_X12,
	VM_REG_GUEST_X13,
	VM_REG_GUEST_X14,
	VM_REG_GUEST_X15,
	VM_REG_GUEST_X16,
	VM_REG_GUEST_X17,
	VM_REG_GUEST_X18,
	VM_REG_GUEST_X19,
	VM_REG_GUEST_X20,
	VM_REG_GUEST_X21,
	VM_REG_GUEST_X22,
	VM_REG_GUEST_X23,
	VM_REG_GUEST_X24,
	VM_REG_GUEST_X25,
	VM_REG_GUEST_X26,
	VM_REG_GUEST_X27,
	VM_REG_GUEST_X28,
	VM_REG_GUEST_X29,
	VM_REG_GUEST_LR,
	VM_REG_GUEST_SP,
	VM_REG_GUEST_ELR,
	VM_REG_GUEST_SPSR,
	VM_REG_ELR_EL2,
	VM_REG_LAST
};

#define	VM_INTINFO_VECTOR(info)	((info) & 0xff)
#define	VM_INTINFO_DEL_ERRCODE	0x800
#define	VM_INTINFO_RSVD		0x7ffff000
#define	VM_INTINFO_VALID	0x80000000
#define	VM_INTINFO_TYPE		0x700
#define	VM_INTINFO_HWINTR	(0 << 8)
#define	VM_INTINFO_NMI		(2 << 8)
#define	VM_INTINFO_HWEXCEPTION	(3 << 8)
#define	VM_INTINFO_SWINTR	(4 << 8)

#define VM_GUEST_BASE_IPA	0x80000000UL	/* Guest kernel start ipa */

#ifdef _KERNEL

#define	VM_MAX_NAMELEN	32

struct vm;
struct vm_exception;
struct vm_memory_segment;
struct vm_exit;
struct vm_run;
struct vm_object;
struct pmap;
struct hypctx;

typedef int	(*vmm_init_func_t)(int ipinum);
typedef int	(*vmm_cleanup_func_t)(void);
typedef void	(*vmm_resume_func_t)(void);
typedef void *	(*vmi_init_func_t)(struct vm *vm);
typedef int	(*vmi_run_func_t)(void *vmi, int vcpu, register_t rip,
				  struct pmap *pmap, void *rendezvous_cookie,
				  void *suspend_cookie);
typedef void	(*vmi_cleanup_func_t)(void *vmi);
typedef void	(*vmi_mmap_set_func_t)(void *arg, vm_offset_t va,
				       vm_offset_t pa, size_t len,
				       vm_prot_t prot);
typedef vm_paddr_t (*vmi_mmap_get_func_t)(void *arg, vm_offset_t va);
typedef int	(*vmi_get_register_t)(void *vmi, int vcpu, int num,
				      uint64_t *retval);
typedef int	(*vmi_set_register_t)(void *vmi, int vcpu, int num,
				      uint64_t val);
typedef int	(*vmi_get_cap_t)(void *vmi, int vcpu, int num, int *retval);
typedef int	(*vmi_set_cap_t)(void *vmi, int vcpu, int num, int val);
typedef struct vmspace * (*vmi_vmspace_alloc)(vm_offset_t min, vm_offset_t max);
typedef void	(*vmi_vmspace_free)(struct vmspace *vmspace);
typedef struct vlapic * (*vmi_vlapic_init)(void *vmi, int vcpu);
typedef void	(*vmi_vlapic_cleanup)(void *vmi, struct vlapic *vlapic);

struct vmm_ops {
	/* Module-wide functions */
	vmm_init_func_t		init;
	vmm_cleanup_func_t	cleanup;
	vmm_resume_func_t	resume;
	/* VM specific functions */
	vmi_init_func_t		vminit;
	vmi_run_func_t		vmrun;
	vmi_cleanup_func_t	vmcleanup;
	vmi_mmap_set_func_t	vmmapset;
	vmi_mmap_get_func_t	vmmapget;
	vmi_get_register_t	vmgetreg;
	vmi_set_register_t	vmsetreg;
	vmi_get_cap_t		vmgetcap;
	vmi_set_cap_t		vmsetcap;
};

extern struct vmm_ops vmm_ops_arm;

int vm_create(const char *name, struct vm **retvm);
void vm_destroy(struct vm *vm);
const char *vm_name(struct vm *vm);
int vm_malloc(struct vm *vm, uint64_t gpa, size_t len);
uint64_t vm_gpa2hpa(struct vm *vm, uint64_t gpa, size_t size);
int vm_gpabase2memseg(struct vm *vm, uint64_t gpabase,
		      struct vm_memory_segment *seg);
boolean_t vm_mem_allocated(struct vm *vm, uint64_t gpa);
int vm_get_register(struct vm *vm, int vcpu, int reg, uint64_t *retval);
int vm_set_register(struct vm *vm, int vcpu, int reg, uint64_t val);
int vm_run(struct vm *vm, struct vm_run *vmrun);
void* vm_get_cookie(struct vm *vm);
int vm_get_capability(struct vm *vm, int vcpu, int type, int *val);
int vm_set_capability(struct vm *vm, int vcpu, int type, int val);
int vm_activate_cpu(struct vm *vm, int vcpu);
int vm_attach_vgic(struct vm *vm, uint64_t dist_start, size_t dist_size,
		   uint64_t redist_start, size_t redist_size);
int vm_assert_irq(struct vm *vm, uint32_t irq);
int vm_deassert_irq(struct vm *vm, uint32_t irq);
struct vm_exit *vm_exitinfo(struct vm *vm, int vcpuid);
void vm_exit_suspended(struct vm *vm, int vcpuid, uint64_t rip);
void vm_exit_rendezvous(struct vm *vm, int vcpuid, uint64_t rip);
void vm_exit_astpending(struct vm *vm, int vcpuid, uint64_t rip);

#ifdef _SYS__CPUSET_H_
/*
 * Rendezvous all vcpus specified in 'dest' and execute 'func(arg)'.
 * The rendezvous 'func(arg)' is not allowed to do anything that will
 * cause the thread to be put to sleep.
 *
 * If the rendezvous is being initiated from a vcpu context then the
 * 'vcpuid' must refer to that vcpu, otherwise it should be set to -1.
 *
 * The caller cannot hold any locks when initiating the rendezvous.
 *
 * The implementation of this API may cause vcpus other than those specified
 * by 'dest' to be stalled. The caller should not rely on any vcpus making
 * forward progress when the rendezvous is in progress.
 */
typedef void (*vm_rendezvous_func_t)(struct vm *vm, int vcpuid, void *arg);
void vm_smp_rendezvous(struct vm *vm, int vcpuid, cpuset_t dest,
    vm_rendezvous_func_t func, void *arg);
cpuset_t vm_active_cpus(struct vm *vm);
cpuset_t vm_suspended_cpus(struct vm *vm);
#endif	/* _SYS__CPUSET_H_ */

extern uint64_t hypmode_enabled;
static __inline bool
virt_enabled()
{
	return (hypmode_enabled != 0);
}

static __inline int
vcpu_rendezvous_pending(void *rendezvous_cookie)
{

	return (*(uintptr_t *)rendezvous_cookie != 0);
}

static __inline int
vcpu_suspended(void *suspend_cookie)
{

	return (*(int *)suspend_cookie);
}

enum vcpu_state {
	VCPU_IDLE,
	VCPU_FROZEN,
	VCPU_RUNNING,
	VCPU_SLEEPING,
};

int vcpu_set_state(struct vm *vm, int vcpu, enum vcpu_state state,
    bool from_idle);
enum vcpu_state vcpu_get_state(struct vm *vm, int vcpu, int *hostcpu);

static int __inline
vcpu_is_running(struct vm *vm, int vcpu, int *hostcpu)
{
	return (vcpu_get_state(vm, vcpu, hostcpu) == VCPU_RUNNING);
}

#ifdef _SYS_PROC_H_
static int __inline
vcpu_should_yield(struct vm *vm, int vcpu)
{

	if (curthread->td_flags & (TDF_ASTPENDING | TDF_NEEDRESCHED))
		return (1);
	else if (curthread->td_owepreempt)
		return (1);
	else
		return (0);
}
#endif

void *vcpu_stats(struct vm *vm, int vcpu);
void vcpu_notify_event(struct vm *vm, int vcpuid, bool lapic_intr);

/*
 * This function is called after a VM-exit that occurred during exception or
 * interrupt delivery through the IDT. The format of 'intinfo' is described
 * in Figure 15-1, "EXITINTINFO for All Intercepts", APM, Vol 2.
 *
 * If a VM-exit handler completes the event delivery successfully then it
 * should call vm_exit_intinfo() to extinguish the pending event. For e.g.,
 * if the task switch emulation is triggered via a task gate then it should
 * call this function with 'intinfo=0' to indicate that the external event
 * is not pending anymore.
 *
 * Return value is 0 on success and non-zero on failure.
 */
int vm_exit_intinfo(struct vm *vm, int vcpuid, uint64_t intinfo);

/*
 * This function is called before every VM-entry to retrieve a pending
 * event that should be injected into the guest. This function combines
 * nested events into a double or triple fault.
 *
 * Returns 0 if there are no events that need to be injected into the guest
 * and non-zero otherwise.
 */
int vm_entry_intinfo(struct vm *vm, int vcpuid, uint64_t *info);

int vm_get_intinfo(struct vm *vm, int vcpuid, uint64_t *info1, uint64_t *info2);

enum vm_reg_name vm_segment_name(int seg_encoding);

struct vm_copyinfo {
	uint64_t	gpa;
	size_t		len;
	void		*hva;
	void		*cookie;
};

int vcpu_trace_exceptions(struct vm *vm, int vcpuid);
#endif	/* _KERNEL */

#define	VM_MAXCPU	1

#define	VM_DIR_READ	0
#define	VM_DIR_WRITE	1

struct vie {
	uint8_t access_size:4, sign_extend:1, dir:1, unused:2;
	enum vm_reg_name reg;
};

struct vre {
	uint32_t inst_syndrome;
	uint8_t dir:1, unused:7;
	enum vm_reg_name reg;
};

/*
 * Identifiers for optional vmm capabilities
 */
enum vm_cap_type {
	VM_CAP_HALT_EXIT,
	VM_CAP_MTRAP_EXIT,
	VM_CAP_PAUSE_EXIT,
	VM_CAP_UNRESTRICTED_GUEST,
	VM_CAP_MAX
};
enum vm_exitcode {
	VM_EXITCODE_BOGUS,
	VM_EXITCODE_INST_EMUL,
	VM_EXITCODE_REG_EMUL,
	VM_EXITCODE_HVC,
	VM_EXITCODE_SUSPENDED,
	VM_EXITCODE_HYP,
	VM_EXITCODE_WFI,
	VM_EXITCODE_MAX
};

enum task_switch_reason {
	TSR_CALL,
	TSR_IRET,
	TSR_JMP,
	TSR_IDT_GATE,	/* task gate in IDT */
};

struct vm_task_switch {
	uint16_t	tsssel;		/* new TSS selector */
	int		ext;		/* task switch due to external event */
	uint32_t	errcode;
	int		errcode_valid;	/* push 'errcode' on the new stack */
	enum task_switch_reason reason;
};

struct vm_exit {
	enum vm_exitcode	exitcode;
	int			inst_length;
	uint64_t		pc;
	union {
		/*
		 * ARM specific payload.
		 */
		struct {
			uint32_t	exception_nr;
			uint32_t	esr_el2;	/* Exception Syndrome Register */
			uint64_t	far_el2;	/* Fault Address Register */
			uint64_t	hpfar_el2;	/* Hypervisor IPA Fault Address Register */
		} hyp;
		struct {
			struct vre 	vre;
		} reg_emul;
		struct {
			uint64_t	gpa;
			int		fault_type;
		} paging;
		struct {
			uint64_t	gpa;
			struct vie	vie;
		} inst_emul;

		struct {
			struct hypctx *hypctx;
		} wfi;
		/*
		 * VMX specific payload. Used when there is no "better"
		 * exitcode to represent the VM-exit.
		 */
		struct {
			int		status;		/* vmx inst status */
			/*
			 * 'exit_reason' and 'exit_qualification' are valid
			 * only if 'status' is zero.
			 */
			uint32_t	exit_reason;
			uint64_t	exit_qualification;
			/*
			 * 'inst_error' and 'inst_type' are valid
			 * only if 'status' is non-zero.
			 */
			int		inst_type;
			int		inst_error;
		} vmx;
		/*
		 * SVM specific payload.
		 */
		struct {
			uint64_t	exitcode;
			uint64_t	exitinfo1;
			uint64_t	exitinfo2;
		} svm;
		struct {
#ifdef __aarch64__
#else
			uint32_t	code;		/* ecx value */
			uint64_t	wval;
#endif
		} msr;
		struct {
			int		vcpu;
			uint64_t	rip;
		} spinup_ap;
		struct {
			uint64_t	rflags;
		} hlt;
		struct {
			int		vector;
		} ioapic_eoi;
		struct {
			enum vm_suspend_how how;
		} suspended;
		struct vm_task_switch task_switch;
	} u;
};

#endif	/* _VMM_H_ */
