#ifndef TDD_MMIO_H
#define TDD_MMIO_H
#include <stdbool.h>
#include <stdint.h>
#include "../app/control_core.h"
/* BSP adapter must implement ordered, uncached accesses and write completion. */
struct tdd_bus {
    void *context;
    uint32_t (*read32)(void *context, uint32_t offset);
    void (*write32)(void *context, uint32_t offset, uint32_t value);
};
enum tdd_adc_result { TDD_ADC_OK, TDD_ADC_NOT_READY, TDD_ADC_INCOHERENT, TDD_ADC_ERROR };
enum tdd_adc_result tdd_adc_read(const struct tdd_bus *bus, struct tdd_sample *out);
/* Duty/disable, one-shot carrier arming and peak/precharge gate control. */
void tdd_pwm_disable(const struct tdd_bus *bus, uint32_t frequency_code);
bool tdd_pwm_write_duty(const struct tdd_bus *bus, int16_t duty);
uint32_t tdd_pwm_period(uint32_t frequency_code);
bool tdd_pwm_configure(const struct tdd_bus *bus, uint32_t frequency_code,
                       uint32_t deadtime, uint32_t phase);
bool tdd_pwm_arm(const struct tdd_bus *bus, uint32_t frequency_code);
void tdd_pwm_block(const struct tdd_bus *bus);
bool tdd_pwm_request_gates(const struct tdd_bus *bus);
bool tdd_pwm_ready(const struct tdd_bus *bus);
#endif
