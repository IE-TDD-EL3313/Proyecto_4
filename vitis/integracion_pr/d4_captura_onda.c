#include "d4_captura_onda.h"
#include <stddef.h>

/* Escalamiento entero conservador para CSV, sin floats en las impresiones.
 * Se clampa el valor bruto a +/- 2 MV antes de convertir a mV y se
 * documenta ese limite en el README. No afecta la salida del PR.
 */
static int32_t voltios_a_mv_saturados(float v)
{
    float mv;
    if (v >= 2000000.0f) return 2000000000;
    if (v <= -2000000.0f) return -2000000000;
    mv = v * 1000.0f;
    return (int32_t)(mv + (mv >= 0.0f ? 0.5f : -0.5f));
}

static int16_t valor_a_i16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

void d4_captura_onda_init(volatile d4_captura_onda_t *c)
{
    if (c == NULL) return;
    c->n = 0u;
    c->saltos_restantes = 0u;
    c->estado = D4_CAPTURA_LIBRE;
}

bool d4_captura_onda_solicitar(volatile d4_captura_onda_t *c)
{
    if (c == NULL || c->estado != D4_CAPTURA_LIBRE) return false;
    c->n = 0u;
    c->saltos_restantes = 0u;
    c->estado = D4_CAPTURA_TOMANDO;
    return true;
}

void d4_captura_onda_paso_isr(volatile d4_captura_onda_t *c,
                              const d4_real_resultado_t *r,
                              bool proteccion_activa)
{
    volatile d4_captura_pr_punto_t *p;
    if (c == NULL || c->estado != D4_CAPTURA_TOMANDO) return;

    if (proteccion_activa) {
        c->estado = D4_CAPTURA_ERROR_PROTECCION;
        return;
    }
    if (r == NULL || !r->adc_nuevo || !r->sensor_valido) {
        c->estado = D4_CAPTURA_ERROR_ADC;
        return;
    }
    if (!r->pr_calculado) {
        c->estado = D4_CAPTURA_ERROR_PR;
        return;
    }
    if (c->saltos_restantes != 0u) {
        --c->saltos_restantes;
        return;
    }

    p = &c->puntos[c->n];
    p->u_raw_mv = voltios_a_mv_saturados(r->pr.raw_v);
    p->codigo_adc = r->codigo_adc;
    p->corriente_pr_ma = valor_a_i16(r->corriente_filtrada_ma);
    p->referencia_ma = valor_a_i16((int32_t)
                        (r->pr.reference_a * 1000.0f +
                         (r->pr.reference_a >= 0.0f ? 0.5f : -0.5f)));
    p->u_limit_mv = valor_a_i16(voltios_a_mv_saturados(r->pr.limited_v));
    p->duty_mil = r->duty_mil;
    p->flags = (uint8_t)((r->pr.saturated ? D4_CAPTURA_FLAG_SATURADO : 0u) |
                          (r->pico_ignorado ? D4_CAPTURA_FLAG_PICO : 0u));
    p->reservado = 0u;
    ++c->n;

    if (c->n == D4_CAPTURA_N_MUESTRAS) {
        c->estado = D4_CAPTURA_LISTA;
    } else {
        c->saltos_restantes = (uint8_t)(D4_CAPTURA_DECIMACION - 1u);
    }
}

bool d4_captura_onda_terminada(const volatile d4_captura_onda_t *c)
{
    return c != NULL && c->estado >= D4_CAPTURA_LISTA;
}

void d4_captura_onda_liberar(volatile d4_captura_onda_t *c)
{
    if (c != NULL && c->estado >= D4_CAPTURA_LISTA)
        c->estado = D4_CAPTURA_LIBRE;
}
