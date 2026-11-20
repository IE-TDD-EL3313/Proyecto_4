#ifndef TDD_ADC_VERIFY_H
#define TDD_ADC_VERIFY_H
#include "capture.h"
#include "../drivers/tdd_mmio.h"
#include "../drivers/tdd_timer.h"
enum tdd_adc_verify_state { TDD_VERIFY_SYNC, TDD_VERIFY_DATA, TDD_VERIFY_RUNNING, TDD_VERIFY_FAULT };
enum tdd_adc_verify_fault { TDD_VERIFY_OK, TDD_VERIFY_CONFIG, TDD_VERIFY_ADC,
    TDD_VERIFY_GAP, TDD_VERIFY_STALE, TDD_VERIFY_GATES, TDD_VERIFY_START_TIMEOUT };
struct tdd_adc_verify {
    struct tdd_capture capture;
    struct tdd_bus adc, pwm, timer;
    enum tdd_adc_verify_state state;
    enum tdd_adc_verify_fault fault;
    uint32_t period, sync_ticks, last_seq, last_ticks;
    uint32_t interrupts, age_min, age_max, max_service_ticks;
    atomic_uint samples;
    bool prepared;
};
/* Caller resets ADC/XADC and waits READY before prepare. INTC must be connected
   before starting Timer. This application never requests gate enable. */
bool tdd_adc_verify_prepare(struct tdd_adc_verify *v, struct tdd_bus adc,
    struct tdd_bus pwm, struct tdd_bus timer, uint32_t frequency_code);
void tdd_adc_verify_irq(struct tdd_adc_verify *v);
#endif
