/*
 * D4 / P3: regulador PR discreto (50 Hz, Fs = 5000 Hz).
 * Coeficientes y ley de control INTACTOS respecto a especificacion P3,
 * 20/11/2026. Ningun anti-windup nuevo esta activado: cambiar el PR exige
 * volver a validar con el modelo PLECS de Potencia.
 *
 * Nota: si la actuacion del PR esta deshabilitada en main, la planta no
 * responde a su salida; una saturacion sostenida puede ser normal en una
 * prueba EN SOMBRA por desajuste de fase o amplitud. No asumir que sea
 * inestabilidad del PR en lazo cerrado.
 */

#include "d4_pr_p3.h"
#include <math.h>
#include <stddef.h>

/* 50 Hz a 5000 Hz -> exactamente 100 muestras por periodo. */
static const float quarter_sine[26] = {
    0.000000000f, 0.062790520f, 0.125333234f, 0.187381315f,
    0.248689887f, 0.309016994f, 0.368124553f, 0.425779292f,
    0.481753674f, 0.535826795f, 0.587785252f, 0.637423990f,
    0.684547106f, 0.728968627f, 0.770513243f, 0.809016994f,
    0.844327926f, 0.876306680f, 0.904827052f, 0.929776486f,
    0.951056516f, 0.968583161f, 0.982287251f, 0.992114701f,
    0.998026728f, 1.000000000f
};

static float sin_100(uint8_t phase)
{
    if (phase <= 25u) return quarter_sine[phase];
    if (phase <= 50u) return quarter_sine[50u - phase];
    if (phase <= 75u) return -quarter_sine[phase - 50u];
    return -quarter_sine[100u - phase];
}

static float clip(float x, float limit)
{
    if (x > limit) return limit;
    if (x < -limit) return -limit;
    return x;
}

int16_t d4_pr_p3_encode_q15(float m)
{
    /* Proteccion defensiva para invocaciones directas; step ya lo comprueba. */
    if (!isfinite(m)) return 0;

    /* +/-0.85 -> +/-27853 en Q1.15; no es el duty_mil del IP PWM. */
    const float bounded = clip(m, D4_P3_MMAX);
    const float scaled = bounded * 32768.0f;
    const int32_t rounded = (int32_t)(scaled + ((scaled >= 0.0f) ? 0.5f : -0.5f));
    return (int16_t)rounded;
}

void d4_pr_p3_init(d4_pr_p3_t *p)
{
    if (p == NULL) return;
    *p = (d4_pr_p3_t){0};
    p->reference_amplitude_a = 0.2f;
}

bool d4_pr_p3_set_amplitude(d4_pr_p3_t *p, float peak_a)
{
    if (p == NULL || !isfinite(peak_a)) return false;
    if (fabsf(peak_a - 0.2f) > 1e-6f &&
        fabsf(peak_a - 0.5f) > 1e-6f &&
        fabsf(peak_a - 1.0f) > 1e-6f) return false;

    /* Mantener los estados: permite ensayos de escalon 0.2->0.5->1.0 A.
       Cambiar desde main requiere coordinacion con la ISR. */
    p->reference_amplitude_a = peak_a;
    return true;
}

void d4_pr_p3_disarm(d4_pr_p3_t *p)
{
    if (p == NULL) return;
    const float amp = p->reference_amplitude_a;
    *p = (d4_pr_p3_t){0};
    p->reference_amplitude_a = amp;
}

void d4_pr_p3_arm(d4_pr_p3_t *p)
{
    if (p == NULL) return;
    d4_pr_p3_disarm(p);
    p->enabled = true;
}

bool d4_pr_p3_step(d4_pr_p3_t *p, float measured_a,
                   d4_pr_p3_result_t *r)
{
    if (p == NULL || r == NULL || !p->enabled || !isfinite(measured_a))
        return false;
    if (!isfinite(p->reference_amplitude_a)) return false;
    if (p->phase_index >= D4_P3_SAMPLES_PER_CYCLE) return false;

    const float ref = p->reference_amplitude_a * sin_100(p->phase_index);
    const float error = ref - measured_a;
    if (!isfinite(ref) || !isfinite(error)) return false;

    /* Ecuacion del PR de P3: NO cambiar los coeficientes sin revalidacion. */
    const float yr = 1.9835983398270212f * p->yr_prev1_v
                   - 0.9875202570484892f * p->yr_prev2_v
                   + 1.2479742951510910f * (error - p->error_prev2_a);
    const float raw = 20.0f * error + yr;
    if (!isfinite(yr) || !isfinite(raw)) return false;

    /* Solo saturacion de salida. No constituye un anti-windup completo. */
    const float limited = clip(raw, D4_P3_UMAX_V);
    const float modulation = limited / D4_P3_VDC_V;
    if (!isfinite(modulation)) return false;

    r->reference_a = ref;
    r->measured_a = measured_a;
    r->error_a = error;
    r->resonant_v = yr;
    r->raw_v = raw;
    r->limited_v = limited;
    r->modulation = modulation;
    r->m_q15 = d4_pr_p3_encode_q15(modulation);
    r->saturated = (raw != limited);

    /* Actualizar estados SOLAMENTE despues de validar todos los calculos. */
    p->error_prev2_a = p->error_prev1_a;
    p->error_prev1_a = error;
    p->yr_prev2_v = p->yr_prev1_v;
    p->yr_prev1_v = yr;

    /* Reemplaza modulo 100 por wrap explicito; misma fase y menos trabajo. */
    ++p->phase_index;
    if (p->phase_index == D4_P3_SAMPLES_PER_CYCLE) {
        p->phase_index = 0u;
    }
    return true;
}