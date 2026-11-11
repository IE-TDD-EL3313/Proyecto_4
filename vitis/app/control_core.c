#include "control_core.h"
#include "reference_lut.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

float tdd_sine(uint32_t phase)
{
    uint32_t quadrant = phase >> 30;
    uint32_t x = phase & UINT32_C(0x3fffffff);
    if (quadrant & 1u) x = UINT32_C(0x40000000) - x;
    uint32_t index = x >> 24;
    float y = 1.0f;
    if (index < 64u) {
        float fraction = (float)(x & UINT32_C(0x00ffffff)) / 16777216.0f;
        y = tdd_quarter_sine[index] + fraction *
            (tdd_quarter_sine[index + 1u] - tdd_quarter_sine[index]);
    }
    return quadrant >= 2u ? -y : y;
}

bool tdd_to_q15(float volts, float vdc, float limit, int16_t *q, bool *saturated)
{
    if (!q || !saturated || !isfinite(volts) || !isfinite(vdc) || vdc <= 0.0f ||
        !isfinite(limit) || limit <= 0.0f || limit > 31129.0f/32768.0f) return false;
    float m = volts / vdc;
    if (!isfinite(m)) return false;
    *saturated = fabsf(m) > limit;
    if (m > limit) m = limit;
    if (m < -limit) m = -limit;
    int32_t code = (int32_t)roundf(m * 32768.0f);
    int32_t bound = (int32_t)floorf(limit * 32768.0f);
    if (code > bound) code = bound;
    if (code < -bound) code = -bound;
    *q = (int16_t)code;
    return true;
}

bool tdd_config_valid(const struct tdd_config *c)
{
    if (!c || c->fs_hz < 100u || (uint64_t)c->fs_hz*c->period_ticks != UINT64_C(100000000) ||
        c->age_min_ticks > c->age_max_ticks || c->age_max_ticks >= c->period_ticks ||
        c->warmup_ticks < c->period_ticks || c->warmup_ticks >= UINT32_C(0x80000000) ||
        c->raw_min >= c->raw_max || c->raw_max > 4095u ||
        c->deadtime_cycles < 20u || c->deadtime_cycles > 1023u) return false;
    const float values[] = {c->offset_code,c->gain_a_per_code,c->vdc,c->m_max,
        c->amplitude_a,c->b0,c->b1,c->b2,c->a1,c->a2};
    for (size_t i=0; i<sizeof values/sizeof values[0]; ++i)
        if (!isfinite(values[i])) return false;
    float geometric = 1.0f - 2.0f*(float)c->deadtime_cycles/(float)c->period_ticks;
    return c->gain_a_per_code != 0.0f && c->vdc > 0.0f && c->amplitude_a >= 0.0f &&
        c->amplitude_a <= 1.0f && c->m_max > 0.0f && c->m_max < geometric &&
        c->m_max <= 31129.0f/32768.0f && fabsf(c->a2) < 1.0f &&
        1.0f+c->a1+c->a2 > 0.0f && 1.0f-c->a1+c->a2 > 0.0f;
}

bool tdd_control_init(struct tdd_control *ctl, const struct tdd_config *cfg, uint32_t now)
{
    if (!ctl) return false;
    memset(ctl,0,sizeof *ctl);
    ctl->state = TDD_FAULT; ctl->fault = TDD_FAULT_CONFIG;
    if (!tdd_config_valid(cfg)) return false;
    ctl->cfg = *cfg;
    ctl->state = TDD_WARMUP; ctl->fault = TDD_FAULT_NONE;
    ctl->start_ticks = now;
    ctl->phase_step = (uint32_t)(((UINT64_C(50)<<32)+cfg->fs_hz/2u)/cfg->fs_hz);
    return true;
}

static bool fail(struct tdd_control *ctl, enum tdd_fault fault, struct tdd_output *out)
{
    ctl->state = TDD_FAULT; ctl->fault = fault;
    memset(out,0,sizeof *out);
    return false;
}

bool tdd_control_set_amplitude(struct tdd_control *ctl, float amplitude)
{
    if (!ctl || !isfinite(amplitude) || amplitude < 0.0f || amplitude > 1.0f ||
        ctl->state == TDD_FAULT) return false;
    ctl->cfg.amplitude_a = amplitude; /* phase deliberately preserved */
    return true;
}

bool tdd_control_step(struct tdd_control *ctl, const struct tdd_sample *s,
                      uint32_t now, struct tdd_output *out)
{
    if (!ctl || !out) return false;
    memset(out,0,sizeof *out);
    if (ctl->state == TDD_FAULT) return false;
    if (ctl->state == TDD_WARMUP && now-ctl->start_ticks >= ctl->cfg.warmup_ticks)
        return fail(ctl,TDD_FAULT_TIME,out);
    if (!s || (s->status & 12u)) return fail(ctl,TDD_FAULT_ADC,out);
    if (!(s->status & 1u)) {
        if (ctl->state == TDD_RUNNING || now-ctl->start_ticks >= ctl->cfg.warmup_ticks)
            return fail(ctl,TDD_FAULT_ADC,out);
        return false; /* bounded WARMUP waiting, gates remain blocked */
    }
    uint32_t age = now-s->ticks;
    if (age < ctl->cfg.age_min_ticks || age > ctl->cfg.age_max_ticks)
        return fail(ctl,TDD_FAULT_TIME,out);
    if (s->raw < ctl->cfg.raw_min || s->raw > ctl->cfg.raw_max)
        return fail(ctl,TDD_FAULT_CURRENT,out);
    if (ctl->state == TDD_RUNNING) {
        if (s->seq != ctl->last_seq+1u) return fail(ctl,TDD_FAULT_SEQUENCE,out);
        if (s->ticks-ctl->last_ticks != ctl->cfg.period_ticks)
            return fail(ctl,TDD_FAULT_TIME,out);
        ctl->phase += ctl->phase_step;
    } else {
        ctl->state = TDD_RUNNING;
        ctl->last_seq=s->seq; ctl->last_ticks=s->ticks;
        out->measured_a=((float)s->raw-ctl->cfg.offset_code)*ctl->cfg.gain_a_per_code;
        if (!isfinite(out->measured_a)) return fail(ctl,TDD_FAULT_NUMERIC,out);
        // Establish a baseline with zero pending duty; physical release is external.
        return true;
    }
    out->measured_a = ((float)s->raw-ctl->cfg.offset_code)*ctl->cfg.gain_a_per_code;
    out->reference_a = ctl->cfg.amplitude_a*tdd_sine(ctl->phase);
    float e = out->reference_a-out->measured_a;
    out->volts = ctl->cfg.b0*e + ctl->cfg.b1*ctl->e1 + ctl->cfg.b2*ctl->e2 -
                 ctl->cfg.a1*ctl->u1 - ctl->cfg.a2*ctl->u2;
    bool saturated=false;
    if (!isfinite(e) || !tdd_to_q15(out->volts,ctl->cfg.vdc,ctl->cfg.m_max,
                                   &out->duty_q15,&saturated))
        return fail(ctl,TDD_FAULT_NUMERIC,out);
    // Conservative candidate: stop on saturation; no unapproved anti-windup.
    if (saturated) return fail(ctl,TDD_FAULT_SATURATION,out);
    out->modulation=out->volts/ctl->cfg.vdc;
    ctl->e2=ctl->e1; ctl->e1=e; ctl->u2=ctl->u1; ctl->u1=out->volts;
    ctl->last_seq=s->seq; ctl->last_ticks=s->ticks;
    return true;
}
