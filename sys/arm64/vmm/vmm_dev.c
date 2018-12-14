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
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/libkern.h>
#include <sys/ioccom.h>
#include <sys/mman.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <machine/vmparam.h>
#include <machine/vmm.h>
#include <machine/vmm_dev.h>

struct vmmdev_softc {
	struct vm	*vm;		/* vm instance cookie */
	struct cdev	*cdev;
	SLIST_ENTRY(vmmdev_softc) link;
	int		flags;
};
#define	VSC_LINKED		0x01

static SLIST_HEAD(, vmmdev_softc) head;

static struct mtx vmmdev_mtx;

static MALLOC_DEFINE(M_VMMDEV, "vmmdev", "vmmdev");

SYSCTL_DECL(_hw_vmm);

static struct vmmdev_softc *
vmmdev_lookup(const char *name)
{
	struct vmmdev_softc *sc;

#ifdef notyet	/* XXX kernel is not compiled with invariants */
	mtx_assert(&vmmdev_mtx, MA_OWNED);
#endif

	SLIST_FOREACH(sc, &head, link) {
		if (strcmp(name, vm_name(sc->vm)) == 0)
			break;
	}

	return (sc);
}

static struct vmmdev_softc *
vmmdev_lookup2(struct cdev *cdev)
{

	return (cdev->si_drv1);
}

static int
vmmdev_rw(struct cdev *cdev, struct uio *uio, int flags)
{
	int error = 0;

	return (error);
}

static int
vmmdev_ioctl(struct cdev *cdev, u_long cmd, caddr_t data, int fflag,
	     struct thread *td)
{
	int error, vcpu, state_changed;
	struct vmmdev_softc *sc;
	struct vm_run *vmrun;
	struct vm_memory_segment *seg;
	struct vm_register *vmreg;
	struct vm_activate_cpu *vac;
	struct vm_attach_vgic *vav;
	struct vm_irq *vi;

	sc = vmmdev_lookup2(cdev);
	if (sc == NULL)
		return (ENXIO);

	error = 0;
	vcpu = -1;
	state_changed = 0;

	/*
	 * Some VMM ioctls can operate only on vcpus that are not running.
	 */
	switch (cmd) {
	case VM_RUN:
	case VM_GET_REGISTER:
	case VM_SET_REGISTER:
		/*
		 * XXX fragile, handle with care
		 * Assumes that the first field of the ioctl data is the vcpu.
		 */
		vcpu = *(int *)data;
		if (vcpu < 0 || vcpu >= VM_MAXCPU) {
			error = EINVAL;
			goto done;
		}

		error = vcpu_set_state(sc->vm, vcpu, VCPU_FROZEN, true);
		if (error)
			goto done;

		state_changed = 1;
		break;

	case VM_MAP_MEMORY:
	case VM_ATTACH_VGIC:
		/*
		 * ioctls that operate on the entire virtual machine must
		 * prevent all vcpus from running.
		 */
		error = 0;
		for (vcpu = 0; vcpu < VM_MAXCPU; vcpu++) {
			error = vcpu_set_state(sc->vm, vcpu, VCPU_FROZEN, true);
			if (error)
				break;
		}

		if (error) {
			vcpu--;
			while (vcpu >= 0) {
				vcpu_set_state(sc->vm, vcpu, VCPU_IDLE, false);
				vcpu--;
			}
			goto done;
		}

		state_changed = 2;
		break;
	case VM_ASSERT_IRQ:
		vi =(struct vm_irq *)data;
		error = vm_assert_irq(sc->vm, vi->irq);
		break;
	case VM_DEASSERT_IRQ:
		vi = (struct vm_irq *)data;
		error = vm_deassert_irq(sc->vm, vi->irq);
		break;
	default:
		break;
	}

	switch(cmd) {
	case VM_RUN:
		vmrun = (struct vm_run *)data;
		error = vm_run(sc->vm, vmrun);
		break;
	case VM_MAP_MEMORY:
		seg = (struct vm_memory_segment *)data;
		error = vm_malloc(sc->vm, seg->gpa, seg->len);
		break;
	case VM_GET_MEMORY_SEG:
		seg = (struct vm_memory_segment *)data;
		seg->len = 0;
		(void)vm_gpabase2memseg(sc->vm, seg->gpa, seg);
		error = 0;
		break;
	case VM_GET_REGISTER:
		vmreg = (struct vm_register *)data;
		error = vm_get_register(sc->vm, vmreg->cpuid, vmreg->regnum,
					&vmreg->regval);
		break;
	case VM_SET_REGISTER:
		vmreg = (struct vm_register *)data;
		error = vm_set_register(sc->vm, vmreg->cpuid, vmreg->regnum,
					vmreg->regval);
		break;
	case VM_ACTIVATE_CPU:
		vac = (struct vm_activate_cpu *)data;
		error = vm_activate_cpu(sc->vm, vac->vcpuid);
		break;
	case VM_ATTACH_VGIC:
		vav = (struct vm_attach_vgic *)data;
		error = vm_attach_vgic(sc->vm, vav->dist_start, vav->dist_size,
				vav->redist_start, vav->redist_size);
		break;
	default:
		error = ENOTTY;
		break;
	}

	if (state_changed == 1) {
		vcpu_set_state(sc->vm, vcpu, VCPU_IDLE, false);
	} else if (state_changed == 2) {
		for (vcpu = 0; vcpu < VM_MAXCPU; vcpu++)
			vcpu_set_state(sc->vm, vcpu, VCPU_IDLE, false);
	}

done:
	/* Make sure that no handler returns a bogus value like ERESTART */
	KASSERT(error >= 0, ("vmmdev_ioctl: invalid error return %d", error));
	return (error);
}

static int
vmmdev_mmap(struct cdev *cdev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
	int error;
	struct vmmdev_softc *sc;

	error = -1;
	mtx_lock(&vmmdev_mtx);

	sc = vmmdev_lookup2(cdev);
	if (sc != NULL && !(nprot & PROT_EXEC)) {
		*paddr = (vm_paddr_t)vm_gpa2hpa(sc->vm, (vm_paddr_t)offset, PAGE_SIZE);
		if (*paddr != (vm_paddr_t)-1)
			error = 0;
	}

	mtx_unlock(&vmmdev_mtx);

	return (error);
}

static void
vmmdev_destroy(void *arg)
{

	struct vmmdev_softc *sc = arg;

	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);

	if (sc->vm != NULL)
		vm_destroy(sc->vm);

	if ((sc->flags & VSC_LINKED) != 0) {
		mtx_lock(&vmmdev_mtx);
		SLIST_REMOVE(&head, sc, vmmdev_softc, link);
		mtx_unlock(&vmmdev_mtx);
	}

	free(sc, M_VMMDEV);
}

static int
sysctl_vmm_destroy(SYSCTL_HANDLER_ARGS)
{
	int error;
	char buf[VM_MAX_NAMELEN];
	struct vmmdev_softc *sc;
	struct cdev *cdev;

	strlcpy(buf, "beavis", sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	mtx_lock(&vmmdev_mtx);
	sc = vmmdev_lookup(buf);
	if (sc == NULL || sc->cdev == NULL) {
		mtx_unlock(&vmmdev_mtx);
		return (EINVAL);
	}

	/*
	 * The 'cdev' will be destroyed asynchronously when 'si_threadcount'
	 * goes down to 0 so we should not do it again in the callback.
	 */
	cdev = sc->cdev;
	sc->cdev = NULL;
	mtx_unlock(&vmmdev_mtx);

	/*
	 * Schedule the 'cdev' to be destroyed:
	 *
	 * - any new operations on this 'cdev' will return an error (ENXIO).
	 *
	 * - when the 'si_threadcount' dwindles down to zero the 'cdev' will
	 *   be destroyed and the callback will be invoked in a taskqueue
	 *   context.
	 */
	destroy_dev_sched_cb(cdev, vmmdev_destroy, sc);

	return (0);
}
SYSCTL_PROC(_hw_vmm, OID_AUTO, destroy, CTLTYPE_STRING | CTLFLAG_RW,
	    NULL, 0, sysctl_vmm_destroy, "A", NULL);

static struct cdevsw vmmdevsw = {
	.d_name		= "vmmdev",
	.d_version	= D_VERSION,
	.d_ioctl	= vmmdev_ioctl,
	.d_mmap		= vmmdev_mmap,
	.d_read		= vmmdev_rw,
	.d_write	= vmmdev_rw,
};

static int
sysctl_vmm_create(SYSCTL_HANDLER_ARGS)
{
	int error;
	struct vm *vm;
	struct cdev *cdev;
	struct vmmdev_softc *sc, *sc2;
	char buf[VM_MAX_NAMELEN];

	strlcpy(buf, "beavis", sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	mtx_lock(&vmmdev_mtx);
	sc = vmmdev_lookup(buf);
	mtx_unlock(&vmmdev_mtx);
	if (sc != NULL)
		return (EEXIST);

	error = vm_create(buf, &vm);
	if (error != 0)
		return (error);

	sc = malloc(sizeof(struct vmmdev_softc), M_VMMDEV, M_WAITOK | M_ZERO);
	sc->vm = vm;

	/*
	 * Lookup the name again just in case somebody sneaked in when we
	 * dropped the lock.
	 */
	mtx_lock(&vmmdev_mtx);
	sc2 = vmmdev_lookup(buf);
	if (sc2 == NULL) {
		SLIST_INSERT_HEAD(&head, sc, link);
		sc->flags |= VSC_LINKED;
	}
	mtx_unlock(&vmmdev_mtx);

	if (sc2 != NULL) {
		vmmdev_destroy(sc);
		return (EEXIST);
	}

	error = make_dev_p(MAKEDEV_CHECKNAME, &cdev, &vmmdevsw, NULL,
			   UID_ROOT, GID_WHEEL, 0600, "vmm/%s", buf);
	if (error != 0) {
		vmmdev_destroy(sc);
		return (error);
	}

	mtx_lock(&vmmdev_mtx);
	sc->cdev = cdev;
	sc->cdev->si_drv1 = sc;
	mtx_unlock(&vmmdev_mtx);

	return (0);
}
SYSCTL_PROC(_hw_vmm, OID_AUTO, create, CTLTYPE_STRING | CTLFLAG_RW,
	    NULL, 0, sysctl_vmm_create, "A", NULL);

void
vmmdev_init(void)
{
	mtx_init(&vmmdev_mtx, "vmm device mutex", NULL, MTX_DEF);
}

int
vmmdev_cleanup(void)
{
	int error;

	if (SLIST_EMPTY(&head))
		error = 0;
	else
		error = EBUSY;

	return (error);
}
