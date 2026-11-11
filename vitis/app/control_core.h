#ifndef TDD_CONTROL_CORE_H
#define TDD_CONTROL_CORE_H
#include <stdbool.h>
#include <stdint.h>

enum tdd_state { TDD_WARMUP, TDD_RUNNING, TDD_FAULT };
enum tdd_fault { TDD_FAULT_NONE, TDD_FAULT_CONFIG, TDD_FAULT_ADC,
    TDD_FAULT_SEQUENCE, TDD_FAULT_TIME, TDD_FAULT_CURRENT, TDD_FAULT_NUMERIC,
    TDD_FAULT_SATURATION };
struct tdd_sample { uint32_t seq, ticks, status; uint16_t raw; };
struct tdd_config {
    uint32_t fs_hz, period_ticks, age_min_ticks, age_max_ticks, warmup_ticks;
    uint16_t raw_min, raw_max, deadtime_cycles;
    float offset_code, gain_a_per_code, vdc, m_max, amplitude_a;
    float b0, b1, b2, a1, a2;
};
struct tdd_control {
    struct tdd_config cfg;
    enum tdd_state state;
    enum tdd_fault fault;
    uint32_t start_ticks, last_seq, last_ticks, phase, phase_step;
    float e1, e2, u1, u2;
};
struct tdd_output { float measured_a, reference_a, volts, modulation; int16_t duty_q15; };
bool tdd_config_valid(const struct tdd_config *cfg);
bool tdd_control_init(struct tdd_control *ctl, const struct tdd_config *cfg, uint32_t now);
bool tdd_control_step(struct tdd_control *ctl, const struct tdd_sample *sample,
                      uint32_t now, struct tdd_output *out);
/* Call at a sample boundary, from the ISR owner (main must publish a request). */
bool tdd_control_set_amplitude(struct tdd_control *ctl, float amplitude);
float tdd_sine(uint32_t phase);
bool tdd_to_q15(float volts, float vdc, float limit, int16_t *q, bool *saturated);
#endif
