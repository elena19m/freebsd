#ifndef _MMIO_IRQ_H_
#define _MMIO_IRQ_H_

struct mmio_devinst;

void mmio_irq_init(struct vmctx *ctx);
void mmio_irq_reserve(int irq);
void mmio_irq_use(int irq);
void mmio_irq_assert(struct mmio_devinst *mi);
void mmio_irq_deassert(struct mmio_devinst *mi);

#endif /* _MMIO_IRQ_H_ */
