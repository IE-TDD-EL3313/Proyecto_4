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
/* These functions never enable PWM. C4 gate blocking is a separate BSP contract. */
void tdd_pwm_disable(const struct tdd_bus *bus, uint32_t frequency_code);
bool tdd_pwm_write_duty(const struct tdd_bus *bus, int16_t duty);
#endif
