#ifndef D4_CONTROLADOR_REAL_H
#define D4_CONTROLADOR_REAL_H

#include <stdbool.h>
#include <stdint.h>
#include "d4_pr_p3.h"

/* PR calculado con ADC valido. La aplicacion decide SI escribir el PWM.
 * Este modulo NO escribe registros AXI ni lee EOC: permite ensayos unitarios.
 * Inicialmente usar salida_autorizada=false, aunque el puente H este conectado.
 */
/* SOLO para ensayo de referencia de 0.2 A pico. NO usar en 0.5 A / 1.0 A.
 * Filtrar una sola muestra de ruido por encima de 0.8 A.
 * Una condicion persistente no se ignora: conserva parada del puente.
 * Este filtro software NO sustituye proteccion de corriente por hardware. */
#define D4_REAL_FILTRO_PICO_MA               800
#define D4_REAL_CORRIENTE_MAXIMA_MA         1500
#define D4_REAL_CONSECUTIVAS_FILTRO_FALLO     4u /* 0.8 ms a 5 kHz */
#define D4_REAL_CONSECUTIVAS_MAXIMA_FALLO    2u /* 0.4 ms a 5 kHz */

/* Guardia de modo observacion. No equivale a proteccion fisica.
 * En neutro, 10 lecturas >100 mA en cualquier bloque de 100 ISR
 * sugieren corriente no ordenada. Una lectura aislada no basta. */
#define D4_REAL_NEUTRO_UMBRAL_MA          100
#define D4_REAL_NEUTRO_VENTANA_ISR         100u  /* 20 ms */
#define D4_REAL_NEUTRO_MUESTRAS_ALTAS       10u
#define D4_REAL_NEUTRO_ESTABLE_ISR       5000u  /* 1 s sin desvio */
/* Corte conservador al actuar sobre referencia de 200 mA pico.
 * Debe validarse con la planta antes de habilitar el firmware. */
#define D4_REAL_ACTUANDO_CORTE_MA         400

typedef enum {
    D4_REAL_OK = 0,
    D4_REAL_SIN_ADC = 1,
    D4_REAL_CODIGO_INVALIDO = 2,
    D4_REAL_FALLO_PR = 3,
    D4_REAL_SOBRECORRIENTE = 4,
    D4_REAL_ENCLAVADO = 5,
    D4_REAL_CORRIENTE_NO_ORDENADA = 6,
    D4_REAL_FALLO_PWM = 7
} d4_real_estado_t;

typedef struct {
    uint32_t muestras_validas;
    uint32_t muestras_sin_adc;
    uint32_t muestras_invalidas;
    uint32_t calculos_pr;
    uint32_t fallos_pr;
    uint32_t saturaciones;
    uint32_t sobrecorrientes;             /* fallos confirmados */
    uint32_t picos_ignorados;             /* lecturas aisladas fuera de banda */
    uint32_t lecturas_sospechosas;        /* total de lecturas fuera de banda */
    uint32_t consecutivas_sospechosas;    /* lecturas seguidas > 800 mA */
    uint32_t consecutivas_sobremaxima;    /* lecturas seguidas > 1500 mA */
    int32_t ultima_corriente_filtrada_ma; /* entrada realmente usada por PR */
    uint32_t fallo_enclavado;     /* 0=normal; otro valor=d4_real_estado_t */
    uint16_t ultimo_codigo;
    int32_t ultima_corriente_ma;
    int16_t ultimo_duty_mil;
    uint32_t neutro_ventana;
    uint32_t neutro_fuera_ventana;
    uint32_t neutro_estables_consecutivas;
    uint32_t alarmas_neutro;
    /* Excursiones acumuladas desde el arranque en modo sin actuacion.
     * Solo cuentan lecturas ADC validas, una vez por ISR de 5 kHz.
     * No se reinician cada 100 muestras; se congelan ante falla.
     * No participan en ninguna decision de proteccion. */
    uint32_t excursiones_pos_total;
    uint32_t excursiones_neg_total;
    int32_t excursion_pos_max_ma; /* 0 si nunca supero +100 mA */
    int32_t excursion_neg_min_ma; /* 0 si nunca fue menor a -100 mA */
    /* Diagnostico de la ventana de 100 muestras previa al fallo 6.
     * Se conserva congelado si la guardia enclava una alarma.
     * No agrega lecturas del ADC ni buffers extensos. */
    uint16_t guardia_adc_min;
    uint16_t guardia_adc_max;
    int32_t guardia_ma_min;
    int32_t guardia_ma_max;
    /* Promedio ADC del ultimo bloque de 100 muestras (20 ms).
     * Nunca reemplaza el cero de las dos ventanas originales. */
    uint32_t guardia_suma_codigo;
    uint16_t guardia_media_codigo;
    uint16_t guardia_bloques_revisados; /* indicador: al menos un bloque */
    uint16_t guardia_positivas; /* > +100 mA en la ventana */
    uint16_t guardia_negativas; /* < -100 mA en la ventana */
    uint32_t guardia_muestra_disparo; /* indice total de muestra valida */
    bool salida_autorizada;
} d4_real_control_t;

typedef struct {
    d4_real_estado_t estado;
    bool adc_nuevo;
    bool sensor_valido;
    bool pr_calculado;
    bool escribir_pwm;            /* solo true con autorizacion explicita */
    uint16_t codigo_adc;
    int32_t corriente_ma;           /* dato bruto del ADC en mA */
    int32_t corriente_filtrada_ma;  /* dato usado para el PR */
    bool pico_ignorado;             /* dato bruto sustituido en PR */
    int16_t duty_mil;             /* signed -850..+850, NO Q1.15 */
    d4_pr_p3_result_t pr;
} d4_real_resultado_t;

void d4_real_init(d4_real_control_t *c, bool salida_autorizada);

/* Llamar una vez por cada interrupcion de 200us, DESPUES de
 * d4_pwm_sync_poll_isr() y d4_pwm_sync_codigo_fresco_isr().
 * En caso de fallo enclavado, el llamador DEBE ejecutar d4_pwm_sync_stop()
 * y no volver a habilitar los drivers sin un rearme seguro. */
d4_real_estado_t d4_real_paso_isr(d4_real_control_t *c,
                                  d4_pr_p3_t *pr,
                                  bool hay_adc_nuevo,
                                  uint16_t codigo,
                                  d4_real_resultado_t *r);

#endif
