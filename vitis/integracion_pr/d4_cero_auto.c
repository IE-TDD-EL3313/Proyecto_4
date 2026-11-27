#include "d4_cero_auto.h"
#include <stddef.h>

#define D4_CERO_CENTRO 1800
#define FASE_ASENTAMIENTO  0u
#define FASE_VENTANA_1     1u
#define FASE_SEPARACION    2u
#define FASE_VENTANA_2     3u

static void iniciar_ventana(volatile d4_cero_auto_t *c)
{
    c->minimo = UINT16_MAX;
    c->maximo = 0u;
    c->muestras = 0u;
    c->suma_codigos = 0u;
    c->suma_deltas = 0;
    c->suma_deltas_cuadrado = 0u;
}

void d4_cero_auto_iniciar(volatile d4_cero_auto_t *c)
{
    if (c == NULL) return;
    c->estado = D4_CERO_CAPTURANDO;
    c->codigo_cero = 0u;
    c->ultimo_codigo = 0u;
    c->sin_adc_seguidas = 0u;
    c->espera_restante = D4_CERO_ISR_ASENTAMIENTO;
    c->promedio_primera_ventana = 0u;
    c->fase = FASE_ASENTAMIENTO;
    iniciar_ventana(c);
}

static bool ventana_aceptable(const volatile d4_cero_auto_t *c)
{
    const int64_t n = (int64_t)D4_CERO_N_MUESTRAS;
    const int64_t suma = (int64_t)c->suma_deltas;
    const int64_t var_numerador =
        n * (int64_t)c->suma_deltas_cuadrado - suma * suma;
    const int64_t var_max =
        (int64_t)D4_CERO_MAX_DESV_CODIGOS *
        (int64_t)D4_CERO_MAX_DESV_CODIGOS * n * n;

    return ((uint32_t)(c->maximo - c->minimo) <= D4_CERO_MAX_RANGO_CODIGOS) &&
           var_numerador >= 0 && var_numerador <= var_max;
}

d4_cero_estado_t d4_cero_auto_paso_isr(volatile d4_cero_auto_t *c,
                                       bool adc_nuevo, uint16_t codigo)
{
    int32_t delta;
    uint16_t promedio;
    uint16_t diferencia;

    if (c == NULL) return D4_CERO_FALLO_ADC;
    if (c->estado != D4_CERO_CAPTURANDO) return c->estado;

    if (!adc_nuevo) {
        ++c->sin_adc_seguidas;
        if (c->sin_adc_seguidas >= D4_CERO_MAX_SIN_ADC)
            c->estado = D4_CERO_FALLO_ADC;
        return c->estado;
    }
    c->sin_adc_seguidas = 0u;
    c->ultimo_codigo = codigo;

    if (codigo < D4_CERO_CODIGO_MIN || codigo > D4_CERO_CODIGO_MAX) {
        c->estado = D4_CERO_FALLO_CODIGO;
        return c->estado;
    }

    if (c->fase == FASE_ASENTAMIENTO || c->fase == FASE_SEPARACION) {
        if (c->espera_restante > 0u) --c->espera_restante;
        if (c->espera_restante == 0u) {
            if (c->fase == FASE_ASENTAMIENTO)
                c->fase = FASE_VENTANA_1;
            else
                c->fase = FASE_VENTANA_2;
            iniciar_ventana(c);
        }
        return c->estado;
    }

    if (codigo < c->minimo) c->minimo = codigo;
    if (codigo > c->maximo) c->maximo = codigo;
    c->suma_codigos += codigo;
    delta = (int32_t)codigo - D4_CERO_CENTRO;
    c->suma_deltas += delta;
    c->suma_deltas_cuadrado += (uint32_t)(delta * delta);
    ++c->muestras;

    if (c->muestras != D4_CERO_N_MUESTRAS) return c->estado;
    if (!ventana_aceptable(c)) {
        c->estado = D4_CERO_FALLO_RUIDO;
        return c->estado;
    }

    promedio = (uint16_t)((c->suma_codigos + D4_CERO_N_MUESTRAS / 2u) /
                           D4_CERO_N_MUESTRAS);
    if (c->fase == FASE_VENTANA_1) {
        c->promedio_primera_ventana = promedio;
        c->fase = FASE_SEPARACION;
        c->espera_restante = D4_CERO_ISR_ENTRE_VENTANAS;
        return c->estado;
    }

    diferencia = (promedio >= c->promedio_primera_ventana)
        ? (uint16_t)(promedio - c->promedio_primera_ventana)
        : (uint16_t)(c->promedio_primera_ventana - promedio);
    if (diferencia > D4_CERO_MAX_DERIVA_CODIGOS) {
        c->estado = D4_CERO_FALLO_DERIVA;
    } else {
        /* Se utiliza el valor MAS RECIENTE si ambos promedios coinciden. */
        c->codigo_cero = promedio;
        c->estado = D4_CERO_LISTO;
    }
    return c->estado;
}
