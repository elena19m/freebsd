#ifndef __DEVEMU_IRQ_H__
#define __DEVEMU_IRQ_H__

struct devemu_inst;

void devemu_irq_init(struct vmctx *ctx);
void devemu_irq_reserve(int irq);
void devemu_irq_use(int irq);
void devemu_irq_assert(struct devemu_inst *di);
void devemu_irq_deassert(struct devemu_inst *di);

#endif
