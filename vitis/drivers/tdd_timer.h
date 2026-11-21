#ifndef TDD_TIMER_H
#define TDD_TIMER_H
#include "tdd_mmio.h"
bool tdd_timer_prepare(const struct tdd_bus *bus, uint32_t fs_hz);
bool tdd_timer_prepare_ticks(const struct tdd_bus *bus, uint32_t period_ticks);
void tdd_timer_stop(const struct tdd_bus *bus);
void tdd_timer_start(const struct tdd_bus *bus); /* only after INTC handler and sync arm */
bool tdd_timer_ack(const struct tdd_bus *bus);
#endif
