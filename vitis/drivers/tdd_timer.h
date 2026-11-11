#ifndef TDD_TIMER_H
#define TDD_TIMER_H
#include "tdd_mmio.h"
bool tdd_timer_prepare(const struct tdd_bus *bus, uint32_t fs_hz);
void tdd_timer_start(const struct tdd_bus *bus); /* only after INTC handler and sync arm */
bool tdd_timer_ack(const struct tdd_bus *bus);
#endif
