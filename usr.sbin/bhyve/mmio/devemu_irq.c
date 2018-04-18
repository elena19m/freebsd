#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <machine/vmm.h>

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <vmmapi.h>

#include "devemu.h"
#include "devemu_irq.h"
#include "virtio_mmio.h"

/* IRQ count to disable IRQ */
#define IRQ_DISABLED	    0xff

static struct devemu_irq {
	uint32_t	use_count;	/* number of binds */
	uint32_t	active_count;	/* number of asserts */
	uint32_t	active;		/* irq active */
	pthread_mutex_t	lock;
} irqs[50];

void
devemu_irq_reserve(int irq)
{
	assert(irq >= 0 && irq < nitems(irqs));
	assert(irqs[irq].active == 0 || irqs[irq].active == IRQ_DISABLED);
	irqs[irq].active = IRQ_DISABLED;
}

void
devemu_irq_use(int irq)
{
	assert(irq >= 0 && irq < nitems(irqs));
	assert(irqs[irq].active != IRQ_DISABLED);
	irqs[irq].active++;
}

void
devemu_irq_init(struct vmctx *ctx)
{
	int i;

	for (i = 0; i < nitems(irqs); ++i) {
		irqs[i].use_count = 0;
		irqs[i].active_count = 0;
		irqs[i].active = 0;
		pthread_mutex_init(&irqs[i].lock, NULL);
	}
}

void
devemu_irq_assert(struct devemu_inst *di)
{
	struct devemu_irq *irq;
	uint32_t irq_status;

	assert(di->di_lintr.irq <= nitems(irqs));
	irq = &irqs[di->di_lintr.irq];

	pthread_mutex_lock(&irq->lock);
	irq->active_count++;

	pthread_mutex_lock(&di->di_lintr.lock);

	irq_status = devemu_get_cfgreg(di, VIRTIO_MMIO_INTERRUPT_STATUS);
	irq_status |= VIRTIO_MMIO_INT_VRING;
	devemu_set_cfgreg(di, VIRTIO_MMIO_INTERRUPT_STATUS, irq_status);

	if (irq->active_count == 1)
		vm_assert_irq(di->di_vmctx, di->di_lintr.irq);

	pthread_mutex_unlock(&di->di_lintr.lock);

	pthread_mutex_unlock(&irq->lock);
}

void
devemu_irq_deassert(struct devemu_inst *di)
{
	struct devemu_irq *irq;
	uint32_t irq_status;

	assert(di->di_lintr.irq <= nitems(irqs));
	irq = &irqs[di->di_lintr.irq];

	pthread_mutex_lock(&irq->lock);
	irq->active_count--;

	pthread_mutex_lock(&di->di_lintr.lock);

	irq_status = devemu_get_cfgreg(di, VIRTIO_MMIO_INTERRUPT_STATUS);
	irq_status &= ~VIRTIO_MMIO_INT_VRING;
	devemu_set_cfgreg(di, VIRTIO_MMIO_INTERRUPT_STATUS, irq_status);

#if 0
	/* MMIO devices do not require deassertions */
	if (irq->active_count == 0)
		vm_deassert_irq(di->di_vmctx, di->di_lintr.irq);
#endif

	pthread_mutex_unlock(&di->di_lintr.lock);

	pthread_mutex_unlock(&irq->lock);
}
