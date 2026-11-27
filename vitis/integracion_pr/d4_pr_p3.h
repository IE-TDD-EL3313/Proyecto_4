#ifndef D4_PR_P3_H
#define D4_PR_P3_H

#include <stdbool.h>
#include <stdint.h>

/* Coeficientes entregados por P3 el 20/11/2026.
 * PR no ideal: Tustin prewarped 50 Hz; Ts = 200 us, Fs = 5 kHz.
 * Al cambiar Ts se deben recalcular los coeficientes con P3.
 */
#define D4_P3_FS_HZ       5000u
#define D4_P3_TS_S        0.0002f
#define D4_P3_VDC_V       24.0f
#define D4_P3_UMAX_V      20.4f
#define D4_P3_MMAX        0.85f
#define D4_P3_SAMPLES_PER_CYCLE 100u

typedef struct {
    float error_prev1_a;
    float error_prev2_a;
    float yr_prev1_v;
    float yr_prev2_v;
    uint8_t phase_index; /* 0..99, corresponde al siguiente paso */
    float reference_amplitude_a; /* 0.2, 0.5 o 1.0 A pico */
    bool enabled;
} d4_pr_p3_t;

typedef struct {
    float reference_a;
    float measured_a;
    float error_a;
    float resonant_v;
    float raw_v;
    float limited_v;
    float modulation;
    int16_t m_q15;
    bool saturated;
} d4_pr_p3_result_t;

/* init deja el regulador detenido y limpia estados y fase. */
void d4_pr_p3_init(d4_pr_p3_t *controller);
/* Cambiar amplitud desde main; acceso compartido con ISR requiere sincronización. */
bool d4_pr_p3_set_amplitude(d4_pr_p3_t *controller, float peak_a);
/* Comenzar en fase cero y estados cero. Solo con Timer/IRQ protegidos. */
void d4_pr_p3_arm(d4_pr_p3_t *controller);
/* Detener y poner en cero los estados. Solo con Timer/IRQ protegidos. */
void d4_pr_p3_disarm(d4_pr_p3_t *controller);
/* Calcula una actualización solo con medición NUEVA válida en amperios. */
bool d4_pr_p3_step(d4_pr_p3_t *controller, float measured_a,
                   d4_pr_p3_result_t *result);
/* Codificación propuesta Q1.15 firmado, sujeto a confirmación con D3. */
int16_t d4_pr_p3_encode_q15(float modulation);

#endif
