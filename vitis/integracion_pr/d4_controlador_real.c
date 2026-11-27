#include "d4_controlador_real.h"
#include "d4_corriente_sensor.h"
#include <stddef.h>

static int16_t mil_de_modulacion(float m)
{
    /* El PR entrega m en +/-0.85. PWM personalizado recibe m*1000.
     * Evitar confundirlo con el campo m_q15 que tambien calcula P3. */
    float x = m * 1000.0f;
    if (x > 850.0f) x = 850.0f;
    if (x < -850.0f) x = -850.0f;
    return (int16_t)(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

void d4_real_init(d4_real_control_t *c, bool salida_autorizada)
{
    if (c == NULL) return;
    *c = (d4_real_control_t){0};
    c->salida_autorizada = salida_autorizada;
}

static d4_real_estado_t enclavar(d4_real_control_t *c,
                                 d4_pr_p3_t *pr,
                                 d4_real_estado_t error,
                                 d4_real_resultado_t *r)
{
    c->fallo_enclavado = (uint32_t)error;
    if (pr != NULL) d4_pr_p3_disarm(pr);
    r->estado = error;
    return error;
}

d4_real_estado_t d4_real_paso_isr(d4_real_control_t *c,
                                  d4_pr_p3_t *pr,
                                  bool hay_adc_nuevo,
                                  uint16_t codigo,
                                  d4_real_resultado_t *r)
{
    int32_t ma;
    int32_t corriente_para_pr;
    if (c == NULL || pr == NULL || r == NULL) return D4_REAL_FALLO_PR;
    *r = (d4_real_resultado_t){0};

    if (c->fallo_enclavado != 0u) {
        r->estado = D4_REAL_ENCLAVADO;
        return r->estado;
    }
    if (!hay_adc_nuevo) {
        /* No contar muestras separadas por una lectura ausente como
         * consecutivas; el control mantiene su politica de fallo ADC. */
        c->consecutivas_sospechosas = 0u;
        c->consecutivas_sobremaxima = 0u;
        ++c->muestras_sin_adc;
        c->neutro_estables_consecutivas = 0u;
        r->estado = D4_REAL_SIN_ADC;
        /* Una sola muestra perdida deshabilita la etapa si ACTUAMOS.
         * En modo observacion solo salta el calculo; nunca usa un dato viejo. */
        if (c->salida_autorizada) return enclavar(c, pr, r->estado, r);
        return r->estado;
    }
    r->adc_nuevo = true;
    r->codigo_adc = codigo;
    c->ultimo_codigo = codigo;

    if (!d4_corriente_sensor_codigo_a_ma(codigo, &ma)) {
        c->consecutivas_sospechosas = 0u;
        c->consecutivas_sobremaxima = 0u;
        ++c->muestras_invalidas;
        c->neutro_estables_consecutivas = 0u;
        r->estado = D4_REAL_CODIGO_INVALIDO;
        if (c->salida_autorizada) return enclavar(c, pr, r->estado, r);
        return r->estado;
    }
    r->sensor_valido = true;
    r->corriente_ma = ma;
    c->ultima_corriente_ma = ma;
    ++c->muestras_validas;

    /* Si la etapa de potencia llega a ser autorizada, no ignorar
     * picos por encima de 400 mA: detener el PWM sin esperar varias ISR.
     * Esta accion software sigue necesitando proteccion HW independiente. */
    if (c->salida_autorizada &&
        (ma > D4_REAL_ACTUANDO_CORTE_MA ||
         ma < -D4_REAL_ACTUANDO_CORTE_MA)) {
        ++c->sobrecorrientes;
        return enclavar(c, pr, D4_REAL_SOBRECORRIENTE, r);
    }

    /* Supervisar el modo neutro: si el DUTY no se esta escribiendo,
     * no deberia existir una corriente sinusoidal considerable.
     * Umbral de alarma conservador, no afirmacion de causa fisica. */
    if (!c->salida_autorizada) {
        /* Extremos del BLOQUE de 100 muestras que se esta verificando.
         * El bloque queda congelado si se enclava el fallo 6. */
        if (c->neutro_ventana == 0u) {
            c->guardia_adc_min = codigo;
            c->guardia_adc_max = codigo;
            c->guardia_ma_min = ma;
            c->guardia_ma_max = ma;
            c->guardia_suma_codigo = 0u;
            c->guardia_positivas = 0u;
            c->guardia_negativas = 0u;
        } else {
            if (codigo < c->guardia_adc_min) c->guardia_adc_min = codigo;
            if (codigo > c->guardia_adc_max) c->guardia_adc_max = codigo;
            if (ma < c->guardia_ma_min) c->guardia_ma_min = ma;
            if (ma > c->guardia_ma_max) c->guardia_ma_max = ma;
        }

        /* 4095 * 100 < UINT32_MAX; suma segura sin division en cada ISR. */
        c->guardia_suma_codigo += (uint32_t)codigo;

        if (ma > D4_REAL_NEUTRO_UMBRAL_MA) {
            /* Diagnostico historico independiente de la guardia:
             * estas variables NO afectan los umbrales ni el paro. */
            if (c->excursiones_pos_total != UINT32_MAX)
                ++c->excursiones_pos_total;
            if (ma > c->excursion_pos_max_ma)
                c->excursion_pos_max_ma = ma;
            ++c->neutro_fuera_ventana;
            ++c->guardia_positivas;
            c->neutro_estables_consecutivas = 0u;
        } else if (ma < -D4_REAL_NEUTRO_UMBRAL_MA) {
            if (c->excursiones_neg_total != UINT32_MAX)
                ++c->excursiones_neg_total;
            if (ma < c->excursion_neg_min_ma)
                c->excursion_neg_min_ma = ma;
            ++c->neutro_fuera_ventana;
            ++c->guardia_negativas;
            c->neutro_estables_consecutivas = 0u;
        } else if (c->neutro_estables_consecutivas <
                   D4_REAL_NEUTRO_ESTABLE_ISR) {
            ++c->neutro_estables_consecutivas;
        }
        ++c->neutro_ventana;
        if (c->neutro_ventana >= D4_REAL_NEUTRO_VENTANA_ISR) {
            /* Se calcula antes de enclavar: la alarma congela la media. */
            c->guardia_media_codigo = (uint16_t)(
                (c->guardia_suma_codigo + D4_REAL_NEUTRO_VENTANA_ISR / 2u) /
                D4_REAL_NEUTRO_VENTANA_ISR);
            c->guardia_bloques_revisados = 1u;
            if (c->neutro_fuera_ventana >=
                    D4_REAL_NEUTRO_MUESTRAS_ALTAS) {
                c->guardia_muestra_disparo = c->muestras_validas;
                ++c->alarmas_neutro;
                return enclavar(c, pr, D4_REAL_CORRIENTE_NO_ORDENADA, r);
            }
            c->neutro_ventana = 0u;
            c->neutro_fuera_ventana = 0u;
        }
    }

    /* FILTRO DE PICOS PARA ENSAYO DE 0.2 A:
     * Se preserva el codigo ADC y corriente bruta para diagnostico/CSV.
     * Un pico aislado > +/-800 mA no alimenta al PR; usa el ultimo dato
     * dentro de banda. En cambio, se desconecta el PWM si se confirma
     * una sobrecorriente o una desviacion sostenida.
     * No modifica el reloj de ISR, la lectura XADC ni el limite HW. */
    corriente_para_pr = ma;
    if (ma > D4_REAL_FILTRO_PICO_MA ||
        ma < -D4_REAL_FILTRO_PICO_MA) {
        ++c->lecturas_sospechosas;
        ++c->consecutivas_sospechosas;

        if (ma > D4_REAL_CORRIENTE_MAXIMA_MA ||
            ma < -D4_REAL_CORRIENTE_MAXIMA_MA) {
            ++c->consecutivas_sobremaxima;
        } else {
            c->consecutivas_sobremaxima = 0u;
        }

        if (c->consecutivas_sobremaxima >=
                D4_REAL_CONSECUTIVAS_MAXIMA_FALLO ||
            c->consecutivas_sospechosas >=
                D4_REAL_CONSECUTIVAS_FILTRO_FALLO) {
            ++c->sobrecorrientes;
            return enclavar(c, pr, D4_REAL_SOBRECORRIENTE, r);
        }

        /* Sustituir solo en el PR; NO modificar la medicion cruda. */
        corriente_para_pr = c->ultima_corriente_filtrada_ma;
        r->pico_ignorado = true;
        ++c->picos_ignorados;
    } else {
        c->consecutivas_sospechosas = 0u;
        c->consecutivas_sobremaxima = 0u;
        c->ultima_corriente_filtrada_ma = ma;
    }
    r->corriente_filtrada_ma = corriente_para_pr;

    if (!d4_pr_p3_step(pr, (float)corriente_para_pr / 1000.0f, &r->pr)) {
        ++c->fallos_pr;
        return enclavar(c, pr, D4_REAL_FALLO_PR, r);
    }
    ++c->calculos_pr;
    if (r->pr.saturated) ++c->saturaciones;
    r->pr_calculado = true;
    r->duty_mil = mil_de_modulacion(r->pr.modulation);
    c->ultimo_duty_mil = r->duty_mil;
    r->escribir_pwm = c->salida_autorizada;
    r->estado = D4_REAL_OK;
    return r->estado;
}
