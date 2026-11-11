#ifndef TDD_CONTROL_RUNTIME_H
#define TDD_CONTROL_RUNTIME_H
#include "control_core.h"
#include "capture.h"
#include "../drivers/tdd_timer.h"
struct tdd_platform {
    struct tdd_bus adc, pwm, timer;
    void *context;
    void (*block_gates)(void *context);
    bool (*release_at_valley)(void *context);
    void (*mark_isr)(void *context, bool high);
};
struct tdd_runtime {
    struct tdd_control control;
    struct tdd_capture capture;
    struct tdd_platform io;
    uint32_t latch_age_ticks, write_budget_ticks, margin_ticks;
    uint32_t interrupts, max_instrumented_ticks;
    bool prepared, released;
};
/* C4 confirmation is required; there is intentionally no default board adapter. */
bool tdd_runtime_prepare(struct tdd_runtime *rt, const struct tdd_platform *io,
    const struct tdd_config *cfg, uint32_t latch_age, uint32_t write_budget,
    uint32_t margin, bool c4_confirmed);
/* Connect THIS handler to INTC. Do not wrap XTmrCtr_InterruptHandler around it. */
void tdd_runtime_irq(struct tdd_runtime *rt);
#endif
