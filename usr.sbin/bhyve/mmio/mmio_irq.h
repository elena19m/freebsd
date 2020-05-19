#ifndef __MMIO_IRQ_H__
#define __MMIO_IRQ_H__

struct mmio_devinst;

void mmio_irq_init(struct vmctx *ctx);
void mmio_irq_reserve(int irq);
void mmio_irq_use(int irq);
void mmio_irq_assert(struct mmio_devinst *di);
void mmio_irq_deassert(struct mmio_devinst *di);

#endif
