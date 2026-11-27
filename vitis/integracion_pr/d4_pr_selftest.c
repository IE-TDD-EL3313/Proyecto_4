/* Prueba de equivalencia frente a valores de referencia de doble precisión,
 * calculados offline a partir de la ecuación discreta entregada por P3.
 * No es una comparación con un CSV exportado de PLECS.
 * Referencia de 0.2 A a 50 Hz; medida sintética de -40 a +40 mA.
 */
#include "d4_pr_selftest.h"
#include "d4_pr_p3.h"
#include <math.h>
#include <stdint.h>
#ifdef D4_TEST_HOST
#include <stdio.h>
#define xil_printf printf
#else
#include "xil_printf.h"
#endif

typedef struct {
    uint16_t sample;
    float reference_a;
    float resonant_v;
    float limited_v;
} d4_golden_t;

/* Coeficientes fuente: especificación funcional P3, 20/11/2026.
 * Referencias generadas con modelo independiente en doble precisión. */
static const d4_golden_t golden[] = {
    {0u,   0.000000000f,   0.049918972f,   0.849918972f},
    {1u,   0.012558104f,   0.152130609f,   1.003292687f},
    {2u,   0.025066647f,   0.258793073f,   1.160126007f},
    {10u,  0.117557050f,   1.502838349f,   4.453979358f},
    {25u,  0.200000000f,   5.590658974f,   8.990658974f},
    {50u,  0.000000000f,  -0.512863953f,  -0.712863953f},
    {75u, -0.200000000f, -14.820203912f, -18.620203912f},
    {99u, -0.012558104f, -0.513324610f,   0.035513311f},
    {100u, 0.000000000f,   0.735443348f,   1.335443348f},
    {250u, 0.000000000f,  -0.722693267f,  -1.322693267f},
    {500u, 0.000000000f,   0.350385965f,   0.150385965f},
    {999u,-0.012558104f, -2.600700247f,  -2.051862326f}
};

int d4_pr_selftest_run(void)
{
    d4_pr_p3_t p;
    d4_pr_p3_result_t r;
    uint32_t k, checkpoints = 0u;
    int errors = 0;
    const uint32_t golden_count =
        (uint32_t)(sizeof(golden) / sizeof(golden[0]));

    d4_pr_p3_init(&p);
    if (!d4_pr_p3_set_amplitude(&p, 0.2f)) return 1;
    d4_pr_p3_arm(&p);

    for (k = 0u; k < 1000u; ++k) {
        /* Medida sintética reproducible: -0.04, -0.03, ... +0.04 A. */
        float measured_a = 0.01f * (float)((int)(k % 9u) - 4);
        if (!d4_pr_p3_step(&p, measured_a, &r)) {
            ++errors;
            break;
        }
        if (fabsf(r.limited_v) > 20.4001f ||
            fabsf(r.modulation) > 0.85001f ||
            !isfinite(r.resonant_v)) {
            ++errors;
        }
        if (checkpoints < golden_count && k == golden[checkpoints].sample) {
            const d4_golden_t *expected = &golden[checkpoints];
            /* Tolerancias para cálculo acumulativo en float sin FPU. */
            if (fabsf(r.reference_a - expected->reference_a) > 0.0001f ||
                fabsf(r.resonant_v - expected->resonant_v) > 0.03f ||
                fabsf(r.limited_v - expected->limited_v) > 0.03f) {
                ++errors;
                xil_printf("D4 PR SELFTEST: discrepancia en k=%lu\r\n",
                           (unsigned long)k);
            }
            ++checkpoints;
        }
    }

    if (checkpoints != golden_count) ++errors;
    d4_pr_p3_disarm(&p);
    if (p.enabled || p.phase_index != 0u ||
        p.yr_prev1_v != 0.0f || p.yr_prev2_v != 0.0f) ++errors;

    if (errors == 0) {
        xil_printf("D4 PR SELFTEST: PASS (1000 muestras, %lu puntos)\r\n",
                   (unsigned long)checkpoints);
    } else {
        xil_printf("D4 PR SELFTEST: FAIL (%d errores)\r\n", errors);
    }
    return errors;
}
