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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/smp.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>


#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpufunc.h>
#include <machine/cpu-v6.h>
#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "mmu.h"
#include "arm.h"
#include "hyp.h"
#include "vgic.h"
#include "vtimer.h"

#define	HANDLED		1
#define	UNHANDLED	0

MALLOC_DEFINE(M_HYP, "ARM VMM HYP", "ARM VMM HYP");

extern char init_hyp_vector[];
extern char hyp_vector[];
extern char hyp_code_start[];
extern char hypervisor_stub_vect[];
extern char hypmode_enabled[];

lpae_pd_entry_t *hyp_l1pd;
char *stack;

static uint64_t vmid_generation = 0;
static struct mtx vmid_generation_mtx;

static void set_vttbr(struct hyp* hyp) {
	if (hyp->vmid_generation &&
	    ((hyp->vmid_generation & ~VMID_GENERATION_MASK) !=
	    (atomic_load_64(&vmid_generation) & ~VMID_GENERATION_MASK)))
		goto out;

	mtx_lock(&vmid_generation_mtx);

	/* Another VCPU has change the VMID already */
	if (hyp->vmid_generation &&
	    ((hyp->vmid_generation & ~VMID_GENERATION_MASK) !=
	    (vmid_generation & ~VMID_GENERATION_MASK))) {
		mtx_unlock(&vmid_generation_mtx);
		goto out;
	}

	vmid_generation++;
	if (!(vmid_generation & VMID_GENERATION_MASK))
		vmid_generation++;

	hyp->vmid_generation = vmid_generation;
	mtx_unlock(&vmid_generation_mtx);
out:
	hyp->vttbr = BUILD_VTTBR((hyp->vmid_generation & VMID_GENERATION_MASK), hyp->l1pd_phys);
}

static int
arm_init(int ipinum)
{
	char *stack_top;
	lpae_vm_paddr_t phys_hyp_l1pd;

	if (hypmode_enabled[0]) {
		printf("arm_init: processor didn't boot in HYP-mode (no support)\n");
		return (ENXIO);
	}

	mtx_init(&vmid_generation_mtx, "vmid_generation_mtx", NULL, MTX_DEF);

	stack = malloc(PAGE_SIZE, M_HYP, M_WAITOK | M_ZERO);
	stack_top = stack + PAGE_SIZE;

	hyp_l1pd = malloc(2 * LPAE_L1_ENTRIES * sizeof(lpae_pd_entry_t),
	    M_HYP, M_WAITOK | M_ZERO);

	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)stack,
	    (lpae_vm_paddr_t)vtophys(stack),
	    PAGE_SIZE,
	    VM_PROT_READ | VM_PROT_WRITE,
	    false);

	/*
	 * Create two mappings:
	 * - one identity - VA == PA
	 * - one normal mappings to HYP pagetable
	 */
	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)hyp_code_start,
	    (lpae_vm_paddr_t)vtophys(hyp_code_start),
	    PAGE_SIZE,
	    VM_PROT_READ | VM_PROT_WRITE,
	    false);

	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)vtophys(hyp_code_start),
	    (lpae_vm_paddr_t)vtophys(hyp_code_start),
	    PAGE_SIZE,
	    VM_PROT_READ | VM_PROT_WRITE,
	    false);

	/*
	 * Install the temporary vector from which
	 * will do the initialization part of VMM
	 */
	vmm_call_hyp((void *)vtophys(&init_hyp_vector[0]));

	/*
	 * Special init call to activate the MMU
	 * and change the exception vector.
	 * - r0 - first parameter unused
	 * - r1 - stack pointer
	 * - r2 - lower 32 bits for the HTTBR
	 * - r3 - upper 32 bits for the HTTBR
	 */

	phys_hyp_l1pd = (lpae_vm_paddr_t)vtophys(hyp_l1pd);
	vmm_call_hyp(&hyp_vector[0], stack_top, LOW(phys_hyp_l1pd), HIGH(phys_hyp_l1pd));

	/* Initialize VGIC infrastructure */
	if (vgic_hyp_init()) {
		return (ENXIO);
	}

	vtimer_hyp_init();

	return 0;
}

static int
arm_cleanup(void)
{
	vmm_call_hyp((void *) vtophys(vmm_stub_install), (void *)vtophys(&hypervisor_stub_vect[0]));

	free(stack, M_HYP);

	lpae_vmcleanup(NULL);

	free(hyp_l1pd, M_HYP);

	mtx_destroy(&vmid_generation_mtx);

	return 0;
}

static void
arm_restore(void)
{

	;
}

static void *
arm_vminit(struct vm *vm, pmap_t pmap)
{
	struct hyp *hyp;
	struct hypctx *hypctx;
	int i;

	hyp = malloc(sizeof(struct hyp), M_HYP, M_WAITOK | M_ZERO);
	if ((uintptr_t)hyp & PAGE_MASK) {
		panic("malloc of struct hyp not aligned on %d byte boundary",
		      PAGE_SIZE);
	}
	hyp->vm = vm;

	hyp->vgic_attached = false;

	mtx_init(&hyp->vgic_distributor.distributor_lock, "Distributor Lock", "", MTX_SPIN);

	hyp->l1pd_phys = (lpae_pd_entry_t) vtophys(&hyp->l1pd[0]);
	set_vttbr(hyp);

	for (i = 0; i < VM_MAXCPU; i++) {
		hypctx = &hyp->ctx[i];
		hypctx->vcpu = i;
		hypctx->hyp = hyp;
		hypctx->hcr = HCR_GUEST_MASK & ~HCR_TSW & ~HCR_TAC;
		hypctx->midr = cp15_midr_get();
		hypctx->mpidr = (cp15_mpidr_get() & MPIDR_SMP_MASK) |
		    MPIDR_AFF1_LEVEL(i) |
		    MPIDR_AFF0_LEVEL(i);
		hypctx->regs.r_cpsr = PSR_SVC32_MODE | PSR_A | PSR_I | PSR_F;
		vtimer_cpu_init(hypctx);
	}

	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)hyp,
	    (lpae_vm_paddr_t)vtophys(hyp),
	    sizeof(struct hyp),
	    VM_PROT_READ | VM_PROT_WRITE,
	    false);

	vtimer_init(hyp);

	return (hyp);
}

static enum vm_reg_name
get_vm_reg_name(uint32_t reg_nr, uint32_t mode)
{
	switch(reg_nr) {
		case 0:
			return VM_REG_GUEST_R0;
		case 1:
			return VM_REG_GUEST_R1;
		case 2:
			return VM_REG_GUEST_R2;
		case 3:
			return VM_REG_GUEST_R3;
		case 4:
			return VM_REG_GUEST_R4;
		case 5:
			return VM_REG_GUEST_R5;
		case 6:
			return VM_REG_GUEST_R6;
		case 7:
			return VM_REG_GUEST_R7;
		case 8:
			if (mode == PSR_FIQ32_MODE)
				return VM_REG_GUEST_R8_FIQ;
			else
				return VM_REG_GUEST_R8;
		case 9:
			if (mode == PSR_FIQ32_MODE)
				return VM_REG_GUEST_R9_FIQ;
			else
				return VM_REG_GUEST_R9;
		case 10:
			if (mode == PSR_FIQ32_MODE)
				return VM_REG_GUEST_R10_FIQ;
			else
				return VM_REG_GUEST_R10;
		case 11:
			if (mode == PSR_FIQ32_MODE)
				return VM_REG_GUEST_R11_FIQ;
			else
				return VM_REG_GUEST_R11;
		case 12:
			if (mode == PSR_FIQ32_MODE)
				return VM_REG_GUEST_R12_FIQ;
			else
				return VM_REG_GUEST_R12;
		case 13:
			if (mode == PSR_FIQ32_MODE)
				return VM_REG_GUEST_SP_FIQ;
			else if (mode == PSR_SVC32_MODE)
				return VM_REG_GUEST_SP_SVC;
			else if (mode == PSR_ABT32_MODE)
				return VM_REG_GUEST_SP_ABT;
			else if (mode == PSR_UND32_MODE)
				return VM_REG_GUEST_SP_UND;
			else if (mode == PSR_IRQ32_MODE)
				return VM_REG_GUEST_SP_IRQ;
			else
				return VM_REG_GUEST_SP;
		case 14:
			if (mode == PSR_FIQ32_MODE)
				return VM_REG_GUEST_LR_FIQ;
			else if (mode == PSR_SVC32_MODE)
				return VM_REG_GUEST_LR_SVC;
			else if (mode == PSR_ABT32_MODE)
				return VM_REG_GUEST_LR_ABT;
			else if (mode == PSR_UND32_MODE)
				return VM_REG_GUEST_LR_UND;
			else if (mode == PSR_IRQ32_MODE)
				return VM_REG_GUEST_LR_IRQ;
			else
				return VM_REG_GUEST_LR;
	}
	return VM_REG_LAST;
}

static int hyp_handle_exception(struct hyp *hyp, int vcpu, struct vm_exit *vmexit)
{
	int handled;
	int hsr_ec, hsr_il, hsr_iss;
	struct hypctx *hypctx = &hyp->ctx[vcpu];

	handled = UNHANDLED;
	hsr_ec = HSR_EC(vmexit->u.hyp.hsr);
	hsr_il = HSR_IL(vmexit->u.hyp.hsr);
	hsr_iss = HSR_ISS(vmexit->u.hyp.hsr);

	switch(hsr_ec) {
		case HSR_EC_UNKN:
			printf("%s:%d Unknown exception\n",__func__, __LINE__);
			break;
		case HSR_EC_WFI_WFE:
			vmexit->exitcode = VM_EXITCODE_WFI;
			vmexit->u.wfi.hypctx = hypctx;
			break;
		case HSR_EC_MCR_MRC_CP15:
			printf("%s:%d MCR/MRC CP15 - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_MCRR_MRRC_CP15:
			printf("%s:%d MCRR/MRRC CP15 - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_MCR_MRC_CP14:
			printf("%s:%d MCR/MRC CP14 - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_LDC_STC_CP14:
			printf("%s:%d LDC/STC CP14 - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_HCPTR_CP0_CP13:
			printf("%s:%d MCR/MRC CP14 - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_MRC_VMRS_CP10:
			printf("%s:%d MCR/VMRS CP14 - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_BXJ:
			printf("%s:%d BXJ - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_MRRC_CP14:
			printf("%s:%d MRRC CP14 - unimplemented\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_SVC:
			panic("%s:%d SVC called from hyp-mode\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_HVC:
			printf("%s:%d HVC called from hyp-mode - unsupported\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_SMC:
			printf("%s:%d SMC called from hyp-mode - unsupported\n",
			    __func__, __LINE__);
			break;
		case HSR_EC_PABT:
			printf("%s:%d PABT from guest at address %x - unimplemented\n",
			    __func__, __LINE__, vmexit->u.hyp.hifar);
			break;
		case HSR_EC_PABT_HYP:
			printf("%s:%d PABT taken from HYP mode at %x with HSR: %x\n",
			    __func__, __LINE__, vmexit->u.hyp.hifar, vmexit->u.hyp.hsr);
			break;
		case HSR_EC_DABT:
			if (HSR_ISS_ISV(hsr_iss)) {
				if (LPAE_TRANSLATION_FAULT(HSR_ISS_DFSC(hsr_iss))) {
					/*
					 * The page is not mapped and a possible MMIO access
					 * Build the instruction info and return to user to emulate
					 */
					vmexit->exitcode = VM_EXITCODE_INST_EMUL;
					vmexit->u.inst_emul.gpa = ((uint64_t)(vmexit->u.hyp.hpfar >> 4) << 12) |
						(vmexit->u.hyp.hdfar & ((1 << 12) - 1));
					vmexit->u.inst_emul.vie.access_size = HSR_ISS_ACCESS_SIZE(HSR_ISS_SAS(hsr_iss));
					vmexit->u.inst_emul.vie.sign_extend = HSR_ISS_SSE(hsr_iss);
					vmexit->u.inst_emul.vie.dir = HSR_ISS_WnR(hsr_iss);
					vmexit->u.inst_emul.vie.reg = get_vm_reg_name(HSR_ISS_SRT(hsr_iss),
					    vmexit->u.hyp.mode);
				} else {
					printf("%s:%d DABT from guest at address %x with hsr %x with a stage-2 fault != translation\n",
					    __func__, __LINE__, vmexit->u.hyp.hdfar, vmexit->u.hyp.hsr);
				}
			} else {
				printf("%s:%d DABT from guest at address %x with hsr %x, hpfar: %x without a stage-2 fault translation\n",
				    __func__, __LINE__, vmexit->u.hyp.hdfar, vmexit->u.hyp.hsr, vmexit->u.hyp.hpfar);
			}
			break;
		case HSR_EC_DABT_HYP:
			printf("%s:%d DABT taken from HYP mode at %x with HSR: %x\n",
			    __func__, __LINE__, vmexit->u.hyp.hdfar, vmexit->u.hyp.hsr);
			break;
		default:
			printf("%s:%d Unknown HSR_EC code: %x\n",__func__, __LINE__, hsr_ec);
			break;
	}
	return handled;
}

static int
hyp_exit_process(struct hyp *hyp, int vcpu, struct vm_exit *vmexit)
{
	int handled;
	struct hypctx *hypctx;

	hypctx = &hyp->ctx[vcpu];

	handled = UNHANDLED;

	vmexit->exitcode = VM_EXITCODE_BOGUS;

	switch(vmexit->u.hyp.exception_nr) {
	case EXCEPTION_UNDEF:
		panic("%s undefined exception\n", __func__);
		break;
	case EXCEPTION_SVC:
		panic("%s take SVC exception to hyp mode\n", __func__);
		break;
	/* The following are in the same category and are distinguished using HSR */
	case EXCEPTION_PABT:
	case EXCEPTION_DABT:
	case EXCEPTION_HVC:
		vmexit->exitcode = VM_EXITCODE_HYP;
		handled = hyp_handle_exception(hyp, vcpu, vmexit);

		break;
	case EXCEPTION_FIQ:
	case EXCEPTION_IRQ:
		handled = HANDLED;
		break;
	default:
		printf("%s unknown exception: %d\n",__func__, vmexit->u.hyp.exception_nr);
		vmexit->exitcode = VM_EXITCODE_HYP;
		break;
	}
	return (handled);
}

static int
arm_vmrun(void *arg, int vcpu, register_t pc, pmap_t pmap,
	void *rend_cookie, void *suspended_cookie)
{
	int rc;
	int handled;
	register_t regs;
	struct hyp *hyp;
	struct hypctx *hypctx;
	struct vm *vm;
	struct vm_exit *vmexit;

	hyp = arg;
	hypctx = &hyp->ctx[vcpu];
	vm = hyp->vm;
	vmexit = vm_exitinfo(vm, vcpu);

	hypctx->regs.r_pc = (uint32_t) pc;

	do {
		handled = UNHANDLED;

		regs = intr_disable();

		vgic_flush_hwstate(hypctx);
		vtimer_flush_hwstate(hypctx);

		rc = vmm_call_hyp((void *)hyp_enter_guest, hypctx);

		vmexit->pc = hypctx->regs.r_pc;

		vmexit->u.hyp.exception_nr = rc;
		vmexit->inst_length = HSR_IL(hypctx->exit_info.hsr) ? 4 : 2;

		vmexit->u.hyp.hsr = hypctx->exit_info.hsr;
		vmexit->u.hyp.hifar = hypctx->exit_info.hifar;
		vmexit->u.hyp.hdfar = hypctx->exit_info.hdfar;
		vmexit->u.hyp.hpfar = hypctx->exit_info.hpfar;
		vmexit->u.hyp.mode = hypctx->regs.r_cpsr & PSR_MODE;

		intr_restore(regs);

		handled = hyp_exit_process(hyp, vcpu, vmexit);

		vtimer_sync_hwstate(hypctx);
		vgic_sync_hwstate(hypctx);

	} while(handled);
	return 0;
}

static void
arm_vmcleanup(void *arg)
{
	struct hyp *hyp = arg;

	/* Unmap from HYP-mode the hyp tructure */
	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)hyp,
	    (lpae_vm_paddr_t)vtophys(hyp),
	    sizeof(struct hyp),
	    VM_PROT_NONE,
	    false);

	lpae_vmcleanup(&(hyp->l1pd[0]));
	free(hyp, M_HYP);
}

static uint32_t *
hypctx_regptr(struct hypctx *hypctx, int reg)
{

	switch (reg) {
	case VM_REG_GUEST_R0:
		return (&hypctx->regs.r[0]);
	case VM_REG_GUEST_R1:
		return (&hypctx->regs.r[1]);
	case VM_REG_GUEST_R2:
		return (&hypctx->regs.r[2]);
	case VM_REG_GUEST_R3:
		return (&hypctx->regs.r[3]);
	case VM_REG_GUEST_R4:
		return (&hypctx->regs.r[4]);
	case VM_REG_GUEST_R5:
		return (&hypctx->regs.r[5]);
	case VM_REG_GUEST_R6:
		return (&hypctx->regs.r[6]);
	case VM_REG_GUEST_R7:
		return (&hypctx->regs.r[7]);
	case VM_REG_GUEST_R8:
		return (&hypctx->regs.r[8]);
	case VM_REG_GUEST_R9:
		return (&hypctx->regs.r[9]);
	case VM_REG_GUEST_R10:
		return (&hypctx->regs.r[10]);
	case VM_REG_GUEST_R11:
		return (&hypctx->regs.r[11]);
	case VM_REG_GUEST_R12:
		return (&hypctx->regs.r[12]);
	case VM_REG_GUEST_SP:
		return (&hypctx->regs.r_sp);
	case VM_REG_GUEST_LR:
		return (&hypctx->regs.r_lr);
	case VM_REG_GUEST_PC:
		return (&hypctx->regs.r_pc);
	case VM_REG_GUEST_CPSR:
		return (&hypctx->regs.r_cpsr);
	case VM_REG_GUEST_SP_SVC:
		return (&hypctx->sp_svc);
	case VM_REG_GUEST_LR_SVC:
		return (&hypctx->lr_svc);
	case VM_REG_GUEST_SP_ABT:
		return (&hypctx->sp_abt);
	case VM_REG_GUEST_LR_ABT:
		return (&hypctx->lr_abt);
	case VM_REG_GUEST_SP_UND:
		return (&hypctx->sp_und);
	case VM_REG_GUEST_LR_UND:
		return (&hypctx->lr_und);
	case VM_REG_GUEST_SP_IRQ:
		return (&hypctx->sp_irq);
	case VM_REG_GUEST_LR_IRQ:
		return (&hypctx->lr_irq);
	case VM_REG_GUEST_R8_FIQ:
		return (&hypctx->r8_fiq);
	case VM_REG_GUEST_R9_FIQ:
		return (&hypctx->r9_fiq);
	case VM_REG_GUEST_R10_FIQ:
		return (&hypctx->r10_fiq);
	case VM_REG_GUEST_R11_FIQ:
		return (&hypctx->r11_fiq);
	case VM_REG_GUEST_R12_FIQ:
		return (&hypctx->r12_fiq);
	case VM_REG_GUEST_SP_FIQ:
		return (&hypctx->sp_fiq);
	case VM_REG_GUEST_LR_FIQ:
		return (&hypctx->lr_fiq);
	default:
		break;
	}
	return (NULL);
}

static int
arm_getreg(void *arg, int vcpu, int reg, uint64_t *retval)
{
	uint32_t *regp;
	int running, hostcpu;
	struct hyp *hyp = arg;

	running = vcpu_is_running(hyp->vm, vcpu, &hostcpu);
	if (running && hostcpu != curcpu)
		panic("arm_getreg: %s%d is running", vm_name(hyp->vm), vcpu);

	if ((regp = hypctx_regptr(&hyp->ctx[vcpu], reg)) != NULL) {
		*retval = *regp;
		return (0);
	} else
		return (EINVAL);
}

static int
arm_setreg(void *arg, int vcpu, int reg, uint64_t val)
{
	uint32_t *regp;
	struct hyp *hyp = arg;
	int running, hostcpu;

	running = vcpu_is_running(hyp->vm, vcpu, &hostcpu);
	if (running && hostcpu != curcpu)
		panic("hyp_setreg: %s%d is running", vm_name(hyp->vm), vcpu);

	if ((regp = hypctx_regptr(&hyp->ctx[vcpu], reg)) != NULL) {
		*regp = val;
		return (0);
	} else
		return (EINVAL);
}

struct vmm_ops vmm_ops_arm = {
	arm_init,
	arm_cleanup,
	arm_restore,
	arm_vminit,
	arm_vmrun,
	arm_vmcleanup,
	lpae_vmmmap_set,
	lpae_vmmmap_get,
	arm_getreg,
	arm_setreg,
	NULL, /* vmi_get_cap_t */
	NULL /* vmi_set_cap_t */
};
