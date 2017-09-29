#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <machine/vmm.h>

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <vmmapi.h>

#include "mmio_emul.h"
#include "mmio_irq.h"

/* IRQ count to disable IRQ */
#define IRQ_DISABLED	    0xff

static struct mmio_irq {
	uint32_t	use_count;	/* number of binds */
	uint32_t	active_count;	/* number of asserts */
	uint32_t	active;		/* irq active */
	pthread_mutex_t	lock;
} irqs[50];

void
mmio_irq_reserve(int irq)
{
	assert(irq >= 0 && irq < nitems(irqs));
	assert(irqs[irq].active == 0 || irqs[irq].active == IRQ_DISABLED);
	irqs[irq].active = IRQ_DISABLED;
}

void
mmio_irq_use(int irq)
{
	assert(irq >= 0 && irq < nitems(irqs));
	assert(irqs[irq].active != IRQ_DISABLED);
	irqs[irq].active++;
}

void
mmio_irq_init(struct vmctx *ctx)
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
mmio_irq_assert(struct mmio_devinst *mi)
{
	struct mmio_irq *irq;

	assert(mi->mi_lintr.irq <= nitems(irqs));
	irq = &irqs[mi->mi_lintr.irq];
	
	pthread_mutex_lock(&irq->lock);
	irq->active_count++;

	if (irq->active_count == 1)
		vm_assert_irq(mi->mi_vmctx);

	pthread_mutex_unlock(&irq->lock);
}

void
mmio_irq_deassert(struct mmio_devinst *mi)
{
	struct mmio_irq *irq;

	assert(mi->mi_lintr.irq <= nitems(irqs));
	irq = &irqs[mi->mi_lintr.irq];

	pthread_mutex_lock(&irq->lock);
	irq->active_count--;
	
	if (irq->active_count == 0)
		vm_deassert_irq(mi->mi_vmctx);

	pthread_mutex_unlock(&irq->lock);
}
