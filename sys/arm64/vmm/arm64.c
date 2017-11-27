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
#include <vm/vm_param.h>

#include <machine/vm.h>
#include <machine/cpufunc.h>
#include <machine/cpu.h>
#include <machine/vmm.h>
#include <machine/vmm_dev.h>
#include <machine/atomic.h>
#include <machine/hypervisor.h>
#include <machine/pmap.h>

#include "mmu.h"
#include "arm64.h"
#include "vgic.h"
#include "vtimer.h"
#include "hyp.h"

#define	HANDLED		1
#define	UNHANDLED	0

MALLOC_DEFINE(M_HYP, "ARM VMM HYP", "ARM VMM HYP");

extern char hyp_init_vectors[];
extern char hyp_vectors[];
extern char hyp_code_start[];
extern char hyp_code_end[];
extern char hyp_stub_vectors[];

extern uint64_t hypmode_enabled;

char *stack;
pmap_t hyp_pmap;

static uint64_t vmid_generation = 0;
static struct mtx vmid_generation_mtx;

static void set_vttbr(struct hyp *hyp)
{
	if (hyp->vmid_generation != 0 &&
			((hyp->vmid_generation & ~VMID_GENERATION_MASK) !=
			(atomic_load_acq_64(&vmid_generation) & ~VMID_GENERATION_MASK)))
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
	hyp->vttbr = build_vttbr(hyp->vmid_generation,
			vtophys(hyp->stage2_map->pm_l0));
}

static int
arm_init(int ipinum)
{
	char *stack_top;
	size_t hyp_code_len;

	if (!hypmode_enabled) {
		printf("arm_init: processor didn't boot in EL2 (no support)\n");
		return (ENXIO);
	}

	mtx_init(&vmid_generation_mtx, "vmid_generation_mtx", NULL, MTX_DEF);

	/*
	 * Install the temporary vectors which will be responsible for
	 * initializing the VMM when we next trap into EL2.
	 */
	vmm_call_hyp((void *)vtophys(hyp_init_vectors));

	/*
	 * Create the necessary mappings for the hypervisor translation table
	 */
	hyp_pmap = malloc(sizeof(*hyp_pmap), M_HYP, M_WAITOK | M_ZERO);
	hypmap_init(hyp_pmap, PT_STAGE1);
	hyp_code_len = (size_t)hyp_code_end - (size_t)hyp_code_start;
	hypmap_map(hyp_pmap, (vm_offset_t)hyp_code_start, hyp_code_len, VM_PROT_EXECUTE);

	/* We need an identity mapping for when we activate the MMU */
	hypmap_map_identity(hyp_pmap, (vm_offset_t)hyp_code_start, hyp_code_len, VM_PROT_EXECUTE);

	/* Create and map the hypevisor stack */
	stack = malloc(PAGE_SIZE, M_HYP, M_WAITOK | M_ZERO);
	stack_top = stack + PAGE_SIZE;
	hypmap_map(hyp_pmap, (vm_offset_t)stack, PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE);

	/*
	 * Special init call to activate the MMU and change the exception
	 * vector.
	 * x0 - the new exception vector table
	 * x1 - the physical address of the hypervisor translation table
	 * x2 - stack top address
	 */
	vmm_call_hyp((void *)vtophys(hyp_vectors), vtophys(hyp_pmap->pm_l0), ktohyp(stack_top));

	/* Initialize VGIC infrastructure */
	//if (vgic_hyp_init())
	//	return (ENXIO);

	//vtimer_hyp_init();

	return 0;
}

static int
arm_cleanup(void)
{
	/*
	 * vmm_cleanup() will disable the MMU. For the next few instructions,
	 * before the hardware disables the MMU, one of the following is
	 * possible:
	 *
	 * a. The instruction addresses are fetched with the MMU disabled,
	 * and they must represent the actual physical addresses. This will work
	 * because we call the vmm_cleanup() function by its physical address.
	 *
	 * b. The instruction addresses are fetched using the old translation
	 * tables. This will work because we have an identity mapping in place
	 * in the translation tables and vmm_cleanup() is called by its physical
	 * address.
	 */
	vmm_call_hyp((void *)vtophys(vmm_cleanup), vtophys(hyp_stub_vectors));

	hypmap_cleanup(hyp_pmap);
	free(hyp_pmap, M_HYP);
	free(stack, M_HYP);

	mtx_destroy(&vmid_generation_mtx);

	return 0;
}

static void *
arm_vminit(struct vm *vm)
{
	struct hyp *hyp;
	struct hypctx *hypctx;
	int i;

	hyp = malloc(sizeof(struct hyp), M_HYP, M_WAITOK | M_ZERO);
	hyp->vm = vm;
	hyp->vgic_attached = false;

	hyp->stage2_map = malloc(sizeof(*hyp->stage2_map), M_HYP, M_WAITOK | M_ZERO);
	hypmap_init(hyp->stage2_map, PT_STAGE2);
	set_vttbr(hyp);

	mtx_init(&hyp->vgic_distributor.distributor_lock, "Distributor Lock", "", MTX_SPIN);

	for (i = 0; i < VM_MAXCPU; i++) {
		hypctx = &hyp->ctx[i];
		hypctx->vcpu = i;
		hypctx->hyp = hyp;

		/*
		 * Set the Hypervisor Configuration Register:
		 *
		 * HCR_RW: use AArch64 for EL1
		 * HCR_HCD: disable the HVC instruction from EL1 ** HVC ENABLED FOR NOW **
		 * HCR_TSC: trap SMC (Secure Monitor Call) from EL1
		 * HCR_SWIO: turn set/way invalidate into set/way clean and
		 * invalidate
		 * HCR_FB: broadcast maintenance operations
		 * HCR_BSU_IS: barrier instructions apply to the inner shareable
		 * domain
		 * HCR_AMO: route physical SError interrupts to EL2
		 * HCR_IMO: route physical IRQ interrupts to EL2
		 * HCR_FMO: route physical FIQ interrupts to EL2
		 * HCR_VM: use stage 2 translation
		 */
		hypctx->hcr_el2 = HCR_RW | HCR_TSC | HCR_BSU_IS | \
				HCR_SWIO | HCR_FB | HCR_VM| HCR_AMO | HCR_IMO | HCR_FMO;

		/* The guest will detect a uniprocessor system */
		hypctx->vmpidr_el2 = get_mpidr();
		hypctx->vmpidr_el2 |= VMPIDR_EL2_U;
		hypctx->vmpidr_el2 &= ~VMPIDR_EL2_MT;

		/* Use the same CPU identification information as the host */
		hypctx->vpidr_el2 = READ_SPECIALREG(midr_el1);

		/*
		 * Don't trap accesses to CPACR_EL1, trace, SVE, Advanced SIMD
		 * and floating point functionality to EL2.
		 */
		hypctx->cptr_el2 = CPTR_RES1;

		/*
		 * Disable interrupts in the guest. The guest OS will re-enable
		 * them.
		 */
		hypctx->spsr_el2 = PSR_D | PSR_A | PSR_I | PSR_F;
		/* Use the EL1 stack when taking exceptions to EL1 */
	       	hypctx->spsr_el2 |= PSR_M_EL1h;

		/* The guest starts with the MMU disabled */
		hypctx->sctlr_el1 = SCTLR_RES1;
		hypctx->sctlr_el1 &= ~SCTLR_M;

		/* Use the same memory attributes as the host */
		hypctx->mair_el1 = READ_SPECIALREG(mair_el1);

		/* Don't trap accesses to SVE, Advanced SIMD and FP to EL1 */
		hypctx->cpacr_el1 = CPACR_FPEN_TRAP_NONE;

		//vtimer_cpu_init(hypctx);
	}

	hypmap_map(hyp_pmap, (vm_offset_t)hyp, sizeof(struct hyp),
			VM_PROT_READ | VM_PROT_WRITE);

	//vtimer_init(hyp);

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
		case 34:
			return VM_REG_ELR_EL2;
		default:
			break;
	}

	return VM_REG_LAST;
}

static int handle_el1_sync_exception(struct hyp *hyp, int vcpu, struct vm_exit *vmexit)
{
	int handled;
	uint32_t esr_ec, esr_iss, esr_sas;
	//struct hypctx *hypctx = &hyp->ctx[vcpu];

	handled = UNHANDLED;

	esr_ec = ESR_ELx_EXCEPTION(vmexit->u.hyp.esr_el2);
	esr_iss = vmexit->u.hyp.esr_el2 & ESR_ELx_ISS_MASK;

	switch(esr_ec) {
		case EXCP_UNKNOWN:
			printf("%s:%d Unknown exception\n",__func__, __LINE__);
			break;

		/* TODO: Not implemented yet */
#if 0
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
#endif
		case EXCP_HVC:
			printf("%s:%d HVC called from guest, esr_el2: 0x%08x - unsupported\n",
			    __func__, __LINE__, vmexit->u.hyp.esr_el2);
			break;
		case EXCP_DATA_ABORT_L:
			/* Check if instruction syndrome is valid */
			if (((esr_iss & ISS_DATA_ISV) >> ISS_DATA_ISV_SHIFT) == 1) {
				if ((esr_iss & ISS_DATA_DFSC_MASK) == ISS_DATA_DFSC_TF_L1) {
					vmexit->exitcode = VM_EXITCODE_INST_EMUL;
					vmexit->u.inst_emul.gpa = (vmexit->u.hyp.hpfar_el2 >> 4) << 12;

					esr_sas = (esr_iss & ISS_DATA_SAS_MASK) >> ISS_DATA_SAS_SHIFT;
					vmexit->u.inst_emul.vie.access_size = 1 << esr_sas;

					vmexit->u.inst_emul.vie.sign_extend = ((esr_iss & ISS_DATA_SSE) >> ISS_DATA_SSE_SHIFT);
					vmexit->u.inst_emul.vie.dir = ((esr_iss & ISS_DATA_WnR) >>  ISS_DATA_WnR_SHIFT);
					vmexit->u.inst_emul.vie.reg = get_vm_reg_name(
							(esr_iss & ISS_DATA_SRT_MASK) >> ISS_DATA_SRT_SHIFT, 0);
				} else {
					printf("%s:%d DATA ABORT from guest at address 0x%016lx with esr 0x%08x with a stage-2 fault != translation\n",
					    __func__, __LINE__, vmexit->u.hyp.hpfar_el2, vmexit->u.hyp.esr_el2);
				}
			} else {
				printf("%s:%d DATA ABORT from guest at address 0x%016lx with esr 0x%08x, hpfar: 0x%016lx without a stage-2 fault translation\n",
				    __func__, __LINE__, vmexit->u.hyp.hpfar_el2, vmexit->u.hyp.esr_el2, vmexit->u.hyp.hpfar_el2);
			}
			break;

		/* TODO: not implemented yet. */
#if 0
		case HSR_EC_DABT_HYP:
			printf("%s:%d DABT taken from HYP mode at %x with HSR: %x\n",
			    __func__, __LINE__, vmexit->u.hyp.hdfar, vmexit->u.hyp.hsr);
			break;
#endif
		default:
			printf("%s:%d Unknown ESR_EC code: 0x%x\n",__func__, __LINE__, esr_ec);
			break;
	}

	return handled;
}

static int
handle_world_switch(struct hyp *hyp, int vcpu, struct vm_exit *vmexit)
{
	int handled;

	handled = UNHANDLED;

	vmexit->exitcode = VM_EXITCODE_BOGUS;
	switch (vmexit->u.hyp.exception_nr) {
	case EXCP_TYPE_EL1_SYNC:
		vmexit->exitcode = VM_EXITCODE_HYP;
		handled = handle_el1_sync_exception(hyp, vcpu, vmexit);
		break;
	case EXCP_TYPE_EL1_IRQ:
	case EXCP_TYPE_EL1_FIQ:
	case EXCP_TYPE_EL1_ERROR:
	default:
		//printf("%s unhandled exception: %d\n",__func__, vmexit->u.hyp.exception_nr);
		//vmexit->exitcode = VM_EXITCODE_HYP;
		//printf("%s exception: %d\n", __func__, vmexit->u.hyp.exception_nr);
		break;
	}

	return (handled);
}

static int
arm_vmrun(void *arg, int vcpu, register_t pc, pmap_t pmap,
	void *rendezvous_cookie, void *suspend_cookie)
{
	uint64_t excp_type;
	int handled;
	register_t daif;
	struct hyp *hyp;
	struct hypctx *hypctx;
	struct vm *vm;
	struct vm_exit *vmexit;

	hyp = arg;
	vm = hyp->vm;
	vmexit = vm_exitinfo(vm, vcpu);

	hypctx = &hyp->ctx[vcpu];
	hypctx->elr_el2 = (uint64_t)pc;
	do {
		handled = UNHANDLED;

		//vgic_flush_hwstate(hypctx);
		//vtimer_flush_hwstate(hypctx);

		daif = intr_disable();
		excp_type = vmm_call_hyp((void *)ktohyp(vmm_enter_guest),
				ktohyp(hypctx));
		intr_restore(daif);

		vmexit->pc = hypctx->elr_el2;
		vmexit->u.hyp.exception_nr = excp_type;
		vmexit->u.hyp.esr_el2 = hypctx->exit_info.esr_el2;
		vmexit->u.hyp.far_el2 = hypctx->exit_info.far_el2;
		vmexit->u.hyp.hpfar_el2 = hypctx->exit_info.hpfar_el2;

		/*
		switch (vmexit->u.hyp.exception_nr) {
		case 0:
			printf("EXCP_TYPE_EL1_SYNC\n");
			break;
		case 1:
			printf("EXCP_TYPE_EL1_IRQ\n");
			break;
		case 2:
			printf("EXCP_TYPE_EL1_FIQ\n");
			break;
		case 3:
			printf("EXCP_TYPE_EL1_ERROR\n");
			break;
		case 4:
			printf("EXCP_TYPE_EL2_SYNC\n");
			break;
		case 5:
			printf("EXCP_TYPE_EL2_IRQ\n");
			break;
		case 6:
			printf("EXCP_TYPE_EL2_FIQ\n");
			break;
		case 7:
			printf("EXCP_TYPE_EL2_ERROR\n");
			break;
		}

		printf("\n");
		printf("esr_el2 = 0x%08x\n", hypctx->exit_info.esr_el2);
		printf("far_el2 = 0x%016lx\n", hypctx->exit_info.far_el2);
		printf("hpfar_el2 = 0x%016lx\n", hypctx->exit_info.hpfar_el2);

		printf("\n");
		printf("far_el1 = 0x%016lx\n", hypctx->far_el1);
		printf("par_el1 = 0x%016lx\n", hypctx->par_el1);

		printf("\n");
		printf("spsr_el2 = 0x%08x\n", hypctx->spsr_el2);
		printf("sctlr_el1 = 0x%08x\n", hypctx->sctlr_el1);
		*/

		vmexit->inst_length = 4;
		handled = handle_world_switch(hyp, vcpu, vmexit);

		//vtimer_sync_hwstate(hypctx);
		//vgic_sync_hwstate(hypctx);

		if (excp_type == EXCP_TYPE_EL1_IRQ) {
			/* Ignore IRQs for now */
			handled = HANDLED;
		} else {
			/* Resume guest execution from the next instruction */
			hypctx->elr_el2 += vmexit->inst_length;
		}

	} while (handled == HANDLED);

	return 0;
}

static void
arm_vmcleanup(void *arg)
{
	struct hyp *hyp = arg;

	/* Unmap the VM hyp struct from the hyp mode translation table */
	hypmap_map(hyp_pmap, (vm_offset_t)hyp, sizeof(struct hyp), VM_PROT_NONE);
	hypmap_cleanup(hyp->stage2_map);
	free(hyp->stage2_map, M_HYP);
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
	case VM_REG_ELR_EL2:
		return (&hypctx->elr_el2);
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

static
void arm_restore(void)
{
	;
}

struct vmm_ops vmm_ops_arm = {
	arm_init,
	arm_cleanup,
	arm_restore,
	arm_vminit,
	arm_vmrun,
	arm_vmcleanup,
	hypmap_set,
	hypmap_get,
	arm_getreg,
	arm_setreg,
	NULL, 		/* vmi_get_cap_t */
	NULL 		/* vmi_set_cap_t */
};
