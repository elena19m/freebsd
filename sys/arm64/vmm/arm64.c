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
#include <vm/vm_page.h>
#include <machine/vm.h>
#include <machine/pte.h>
#include <machine/cpufunc.h>
#include <machine/cpu.h>
#include <machine/vmm.h>
#include <machine/vmm_dev.h>

#include "mmu.h"
#include "arm64.h"
#include "hyp.h"
#include "vgic.h"
#include "vtimer.h"

#define	HANDLED		1
#define	UNHANDLED	0

MALLOC_DEFINE(M_HYP, "ARM VMM HYP", "ARM VMM HYP");

extern char hyp_init_vectors[];
extern char hyp_vectors[];
extern char hyp_code_start[];
extern char hyp_code_end[];
extern char hypervisor_stub_vect[];

extern uint64_t hypmode_enabled;

lpae_pd_entry_t *hyp_l1pd;
char *stack;

pmap_t el2_pmap;
vm_offset_t el2_va_top;

static uint64_t vmid_generation = 0;
static struct mtx vmid_generation_mtx;

static void set_vttbr(struct hyp *hyp) {
	/*
	 * TODO atomic_load_64 not implemented
	 */
	/*
	if (hyp->vmid_generation &&
	    ((hyp->vmid_generation & ~VMID_GENERATION_MASK) != 
	    (atomic_load_64(&vmid_generation) & ~VMID_GENERATION_MASK)))
		goto out;
		*/

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

static void
arm_create_el2_pmap()
{
	el2_pmap = malloc(sizeof(*el2_pmap), M_HYP, M_WAITOK | M_ZERO);
	mtx_init(&el2_pmap->pm_mtx, "el2_pmap_pm_mtx", NULL, MTX_DEF);
	pmap_pinit(el2_pmap);

	el2_va_top = 0;
}

static void
arm_map_into_el2_pmap(vm_offset_t va_start, vm_offset_t va_end)
{
	vm_page_t tmp_page;

	tmp_page = malloc(sizeof(*tmp_page), M_HYP, M_WAITOK | M_ZERO);
	tmp_page->oflags = VPO_UNMANAGED;
	tmp_page->md.pv_memattr = VM_MEMATTR_DEFAULT;

	/*
	 * Add the physical pages which correspond to the specified virtual
	 * addresses.The virtual addresses might span contiguous virtual pages,
	 * but they might not reside in contiguous physical pages.
	 */
	va_start = trunc_page(va_start);
	while (va_start < va_end) {
		tmp_page->phys_addr = vtophys(va_start);
		pmap_enter(el2_pmap, el2_va_top, tmp_page,
				VM_PROT_DEFAULT, PMAP_ENTER_WIRED, 0);
		va_start += PAGE_SIZE;
		el2_va_top += PAGE_SIZE;
	}

	free(tmp_page, M_HYP);
}

extern char hyp_stub_vectors[];
#include "hyp_assym.h"

static int
arm_init(int ipinum)
{
	char *stack_top;
	lpae_vm_paddr_t phys_hyp_l1pd;

	uint64_t current_vectors;

	printf("ARM_INIT:\n");

	arm_create_el2_pmap();
	arm_map_into_el2_pmap((vm_offset_t)hyp_code_start, (vm_offset_t)hyp_code_end);

	printf("*el2_pmap->pm_l0 = %016lx\n", (uint64_t)*el2_pmap->pm_l0);

	printf("\thyp_stub_vectors = %016lx (virtual)\n\n", (uint64_t)hyp_stub_vectors);
	printf("\thyp_stub_vectors = %016lx\n", vtophys(hyp_stub_vectors));
	printf("\thyp_init_vectors = %016lx\n", vtophys(hyp_init_vectors));
	printf("\thyp_vectors = %016lx\n", vtophys(hyp_vectors));

	printf("vmm_call_hyp(-1)\n");
	current_vectors = vmm_call_hyp((void *)-1);
	printf("\tcurrent_vectors = %016lx\n\n", current_vectors);

	if (!hypmode_enabled) {
		printf("arm_init: processor didn't boot in EL2 (no support)\n");
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
	    VM_PROT_READ | VM_PROT_WRITE);

	/*
	 * Create two mappings:
	 * - one identity - VA == PA
	 * - one normal mappings to HYP pagetable
	 */
	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)hyp_code_start,
	    (lpae_vm_paddr_t)vtophys(hyp_code_start),
	    PAGE_SIZE,
	    VM_PROT_READ | VM_PROT_WRITE);

	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)vtophys(hyp_code_start),
	    (lpae_vm_paddr_t)vtophys(hyp_code_start),
	    PAGE_SIZE,
	    VM_PROT_READ | VM_PROT_WRITE);

	/*
	 * Install the temporary vectors which will be responsible for
	 * initializing the VMM when we next trap into EL2.
	 */
	printf("vmm_call_hyp(hyp_init_vectors)\n");
	vmm_call_hyp((void *)vtophys(hyp_init_vectors));

	/*
	 * Special init call to activate the MMU
	 * and change the exception vector.
	 * - r0 - first parameter unused
	 * - r1 - stack pointer
	 * - r2 - lower 32 bits for the HTTBR
	 * - r3 - upper 32 bits for the HTTBR
	 */

	phys_hyp_l1pd = (lpae_vm_paddr_t)vtophys(hyp_l1pd);

	printf("vmm_call_hyp(-1)\n");
	current_vectors = vmm_call_hyp((void *)-1);
	printf("\tcurrent_vectors = %016lx\n\n", current_vectors);

	printf("vmm_call_hyp(hyp_vectors, stack_top)\n");
	vmm_call_hyp((void *)vtophys(hyp_vectors), vtophys(stack_top));

	printf("vmm_call_hyp(-1)\n");
	current_vectors = vmm_call_hyp((void *)-1);
	printf("\tcurrent_vectors = %016lx\n\n", current_vectors);

	struct hypctx hypctx;

	hypctx.regs.x[0] = 42;
	hypctx.regs.x[1] = 42;
	hypctx.regs.x[2] = 42;
	hypctx.regs.x[3] = 42;
	hypctx.regs.x[4] = 42;
	hypctx.hcr_el2 = 42;
	hypctx.hacr_el2 = 42;
	hypctx.cptr_el2 = 42;

	printf("\thypctx.regs.x[0] = %lu\n", hypctx.regs.x[0]);
	printf("\thypctx.regs.x[1] = %lu\n", hypctx.regs.x[1]);
	printf("\thypctx.regs.x[2] = %lu\n", hypctx.regs.x[2]);
	printf("\thypctx.regs.x[3] = %lu\n", hypctx.regs.x[3]);
	printf("\thypctx.regs.x[4] = %lu\n", hypctx.regs.x[4]);
	printf("\thypctx.hcr_el2 = %lu\n", hypctx.hcr_el2);
	printf("\thypctx.hacr_el2 = %u\n", hypctx.hacr_el2);
	printf("\thypctx.cptr_el2 = %u\n", hypctx.cptr_el2);

	printf("vmm_call_hyp(hyp_enter_guest, &hypctx)\n");
	current_vectors = vmm_call_hyp((void *)vtophys(vmm_enter_guest),
			vtophys(&hypctx));

	printf("\thypctx.regs.x[0] = %lu\n", hypctx.regs.x[0]);
	printf("\thypctx.regs.x[1] = %lu\n", hypctx.regs.x[1]);
	printf("\thypctx.regs.x[2] = %lu\n", hypctx.regs.x[2]);
	printf("\thypctx.regs.x[3] = %lu\n", hypctx.regs.x[3]);
	printf("\thypctx.regs.x[4] = %lu\n", hypctx.regs.x[4]);
	printf("\thypctx.hcr_el2 = %lu\n", hypctx.hcr_el2);
	printf("\thypctx.hacr_el2 = %u\n", hypctx.hacr_el2);
	printf("\thypctx.cptr_el2 = %u\n", hypctx.cptr_el2);

	printf("vmm_call_hyp(vmm_cleanup, hyp_stub_vectors)\n");
	vmm_call_hyp((void *)vtophys(vmm_cleanup),
			(void *)vtophys(hyp_stub_vectors));

	printf("vmm_call_hyp(-1)\n");
	current_vectors = vmm_call_hyp((void *)-1);
	printf("\tcurrent_vectors = %016lx\n", current_vectors);

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
	/*
	vmm_call_hyp((void *)vtophys(vmm_cleanup),
			(void *)vtophys(hyp_stub_vectors));
	*/

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
#if 0
		hypctx->hcr = HCR_GUEST_MASK & ~HCR_TSW & ~HCR_TAC;
		/*
		 * TODO - cpu_ident not implemented.
		 */
		/*
		hypctx->midr = cpu_ident();
		*/
		hypctx->midr = 0;
		/*
		 * TODO - cp15_mpidr_get() not implemented.
		 */
		/*
		hypctx->mpidr = (cp15_mpidr_get() & MPIDR_SMP_MASK) |
		    MPIDR_AFF1_LEVEL(i) |
		    MPIDR_AFF0_LEVEL(i);
		    */
		hypctx->mpidr = 0;
		/*
		 * TODO - regs.r_cpsr does not exists on arm64.
		 */
		//hypctx->regs.r_cpsr = PSR_SVC32_MODE | PSR_A | PSR_I | PSR_F;
#endif
		hypctx->regs.spsr = 0;
		vtimer_cpu_init(hypctx);
	}

	lpae_vmmmap_set(NULL,
	    (lpae_vm_vaddr_t)hyp,
	    (lpae_vm_paddr_t)vtophys(hyp),
	    sizeof(struct hyp),
	    VM_PROT_READ | VM_PROT_WRITE);

	vtimer_init(hyp);

	return (hyp);
}

static enum vm_reg_name
get_vm_reg_name(uint32_t reg_nr, uint32_t mode __attribute__((unused)))
{
	switch(reg_nr) {
		case 0:
			return VM_REG_GUEST_X0;
		case 1:
			return VM_REG_GUEST_X1;
		case 2:
			return VM_REG_GUEST_X2;
		case 3:
			return VM_REG_GUEST_X3;
		case 4:
			return VM_REG_GUEST_X4;
		case 5:
			return VM_REG_GUEST_X5;
		case 6:
			return VM_REG_GUEST_X6;
		case 7:
			return VM_REG_GUEST_X7;
		case 8:
			return VM_REG_GUEST_X8;
		case 9:
			return VM_REG_GUEST_X9;
		case 10:
			return VM_REG_GUEST_X10;
		case 11:
			return VM_REG_GUEST_X11;
		case 12:
			return VM_REG_GUEST_X12;
		case 13:
			return VM_REG_GUEST_X13;
		case 14:
			return VM_REG_GUEST_X14;
		case 15:
			return VM_REG_GUEST_X15;
		case 16:
			return VM_REG_GUEST_X16;
		case 17:
			return VM_REG_GUEST_X17;
		case 18:
			return VM_REG_GUEST_X18;
		case 19:
			return VM_REG_GUEST_X19;
		case 20:
			return VM_REG_GUEST_X20;
		case 21:
			return VM_REG_GUEST_X21;
		case 22:
			return VM_REG_GUEST_X22;
		case 23:
			return VM_REG_GUEST_X23;
		case 24:
			return VM_REG_GUEST_X24;
		case 25:
			return VM_REG_GUEST_X25;
		case 26:
			return VM_REG_GUEST_X26;
		case 27:
			return VM_REG_GUEST_X27;
		case 28:
			return VM_REG_GUEST_X28;
		case 29:
			return VM_REG_GUEST_X29;
		case 30:
			return VM_REG_GUEST_LR;
		case 31:
			return VM_REG_GUEST_SP;
		case 32:
			return VM_REG_GUEST_ELR;
		case 33:
			return VM_REG_GUEST_SPSR;
		default:
			break;
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

#if 0
	hypctx->regs.r_pc = (uint32_t) pc;
	hypctx->regs.elr = (uint32_t) pc;
#endif

	do {
		handled = UNHANDLED;

		regs = intr_disable();

		vgic_flush_hwstate(hypctx);
		vtimer_flush_hwstate(hypctx);

		//rc = vmm_call_hyp((void *)hyp_enter_guest, hypctx);
		rc = 0;

		/*
		 * TODO
		 *
		 * Use the correct register names.
		 */
#if 0
		vmexit->pc = hypctx->regs.r_pc;
		vmexit->pc = hypctx->regs.elr;

		vmexit->u.hyp.exception_nr = rc;
		vmexit->inst_length = HSR_IL(hypctx->exit_info.hsr) ? 4 : 2;

		vmexit->u.hyp.hsr = hypctx->exit_info.hsr;
		vmexit->u.hyp.hifar = hypctx->exit_info.hifar;
		vmexit->u.hyp.hdfar = hypctx->exit_info.hdfar;
		vmexit->u.hyp.hpfar = hypctx->exit_info.hpfar;
		vmexit->u.hyp.mode = hypctx->regs.r_cpsr & PSR_MODE;
		vmexit->u.hyp.mode = hypctx->regs.spsr;
#endif

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
	    VM_PROT_NONE);

	lpae_vmcleanup(&(hyp->l1pd[0]));
	free(hyp, M_HYP);
}

/*
 * Return register value. Registers have different sizes and an explicit cast
 * must be made to ensure proper conversion.
 */
static void *
hypctx_regptr(struct hypctx *hypctx, int reg)
{
	switch (reg) {
	case VM_REG_GUEST_X0:
		return (&hypctx->regs.x[0]);
	case VM_REG_GUEST_X1:
		return (&hypctx->regs.x[1]);
	case VM_REG_GUEST_X2:
		return (&hypctx->regs.x[2]);
	case VM_REG_GUEST_X3:
		return (&hypctx->regs.x[3]);
	case VM_REG_GUEST_X4:
		return (&hypctx->regs.x[4]);
	case VM_REG_GUEST_X5:
		return (&hypctx->regs.x[5]);
	case VM_REG_GUEST_X6:
		return (&hypctx->regs.x[6]);
	case VM_REG_GUEST_X7:
		return (&hypctx->regs.x[7]);
	case VM_REG_GUEST_X8:
		return (&hypctx->regs.x[8]);
	case VM_REG_GUEST_X9:
		return (&hypctx->regs.x[9]);
	case VM_REG_GUEST_X10:
		return (&hypctx->regs.x[10]);
	case VM_REG_GUEST_X11:
		return (&hypctx->regs.x[11]);
	case VM_REG_GUEST_X12:
		return (&hypctx->regs.x[12]);
	case VM_REG_GUEST_X13:
		return (&hypctx->regs.x[13]);
	case VM_REG_GUEST_X14:
		return (&hypctx->regs.x[14]);
	case VM_REG_GUEST_X15:
		return (&hypctx->regs.x[15]);
	case VM_REG_GUEST_X16:
		return (&hypctx->regs.x[16]);
	case VM_REG_GUEST_X17:
		return (&hypctx->regs.x[17]);
	case VM_REG_GUEST_X18:
		return (&hypctx->regs.x[18]);
	case VM_REG_GUEST_X19:
		return (&hypctx->regs.x[19]);
	case VM_REG_GUEST_X20:
		return (&hypctx->regs.x[20]);
	case VM_REG_GUEST_X21:
		return (&hypctx->regs.x[21]);
	case VM_REG_GUEST_X22:
		return (&hypctx->regs.x[22]);
	case VM_REG_GUEST_X23:
		return (&hypctx->regs.x[23]);
	case VM_REG_GUEST_X24:
		return (&hypctx->regs.x[24]);
	case VM_REG_GUEST_X25:
		return (&hypctx->regs.x[25]);
	case VM_REG_GUEST_X26:
		return (&hypctx->regs.x[26]);
	case VM_REG_GUEST_X27:
		return (&hypctx->regs.x[27]);
	case VM_REG_GUEST_X28:
		return (&hypctx->regs.x[28]);
	case VM_REG_GUEST_X29:
		return (&hypctx->regs.x[29]);
	case VM_REG_GUEST_LR:
		return (&hypctx->regs.lr);
	case VM_REG_GUEST_SP:
		return (&hypctx->regs.sp);
	case VM_REG_GUEST_ELR:
		return (&hypctx->regs.elr);
	case VM_REG_GUEST_SPSR:
		return (&hypctx->regs.spsr);
	default:
		break;
	}
	return (NULL);
}

static int
arm_getreg(void *arg, int vcpu, int reg, uint64_t *retval)
{
	void *regp;
	int running, hostcpu;
	struct hyp *hyp = arg;

	running = vcpu_is_running(hyp->vm, vcpu, &hostcpu);
	if (running && hostcpu != curcpu)
		panic("arm_getreg: %s%d is running", vm_name(hyp->vm), vcpu);

	if ((regp = hypctx_regptr(&hyp->ctx[vcpu], reg)) != NULL) {
		if (reg == VM_REG_GUEST_SPSR)
			*retval = *(uint32_t *)regp;
		else
			*retval = *(uint64_t *)regp;
		return (0);
	} else
		return (EINVAL);
}

static int
arm_setreg(void *arg, int vcpu, int reg, uint64_t val)
{
	void *regp;
	struct hyp *hyp = arg;
	int running, hostcpu;

	running = vcpu_is_running(hyp->vm, vcpu, &hostcpu);
	if (running && hostcpu != curcpu)
		panic("hyp_setreg: %s%d is running", vm_name(hyp->vm), vcpu);

	if ((regp = hypctx_regptr(&hyp->ctx[vcpu], reg)) != NULL) {
		if (reg == VM_REG_GUEST_SPSR)
			*(uint32_t *)regp = (uint32_t)val;
		else
			*(uint64_t *)regp = val;
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
	NULL, 		/* vmi_get_cap_t */
	NULL 		/* vmi_set_cap_t */
};
