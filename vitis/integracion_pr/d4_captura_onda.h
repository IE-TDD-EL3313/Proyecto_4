#ifndef D4_CAPTURA_ONDA_H
#define D4_CAPTURA_ONDA_H

#include <stdbool.h>
#include <stdint.h>
#include "d4_controlador_real.h"

/* Diagnostico PR: 100 muestras cada 1000 us, 5 periodos de 50 Hz.
 * Una sola conversion ADC por ISR de 200 us; no leer XADC aqui.
 * Todas las magnitudes de una fila provienen del MISMO paso de control.
 */
#define D4_CAPTURA_N_MUESTRAS  100u
#define D4_CAPTURA_DECIMACION    5u
#define D4_CAPTURA_PERIODO_US 1000u

#define D4_CAPTURA_FLAG_SATURADO  0x01u
#define D4_CAPTURA_FLAG_PICO      0x02u

typedef enum {
    D4_CAPTURA_LIBRE = 0,
    D4_CAPTURA_TOMANDO = 1,
    D4_CAPTURA_LISTA = 2,
    D4_CAPTURA_ERROR_ADC = 3,
    D4_CAPTURA_ERROR_PROTECCION = 4,
    D4_CAPTURA_ERROR_PR = 5
} d4_captura_estado_t;

/* Almacenamiento compacto: 16 bytes por muestra en ABI habitual (32-bit).
 * No guardar 'corriente bruta': se reconstruye con el mismo codigo y offset.
 */
typedef struct {
    int32_t u_raw_mv;
    uint16_t codigo_adc;
    int16_t corriente_pr_ma;
    int16_t referencia_ma;
    int16_t u_limit_mv;
    int16_t duty_mil;
    uint8_t flags;
    uint8_t reservado;
} d4_captura_pr_punto_t;

typedef struct {
    d4_captura_pr_punto_t puntos[D4_CAPTURA_N_MUESTRAS];
    uint16_t n;
    uint8_t saltos_restantes;
    d4_captura_estado_t estado;
} d4_captura_onda_t;

void d4_captura_onda_init(volatile d4_captura_onda_t *c);
bool d4_captura_onda_solicitar(volatile d4_captura_onda_t *c);
void d4_captura_onda_paso_isr(volatile d4_captura_onda_t *c,
                              const d4_real_resultado_t *r,
                              bool proteccion_activa);
bool d4_captura_onda_terminada(const volatile d4_captura_onda_t *c);
void d4_captura_onda_liberar(volatile d4_captura_onda_t *c);

#endif
