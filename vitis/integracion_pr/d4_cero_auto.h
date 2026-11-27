#ifndef D4_CERO_AUTO_H
#define D4_CERO_AUTO_H

#include <stdbool.h>
#include <stdint.h>

/* Calibracion EXCLUSIVAMENTE con 0 A fisicos comprobados y puente H sin
 * potencia. Frecuencia esperada de llamada: 5 kHz (200 us por ISR).
 * Esperar a que se estabilice el ACS712 y comparar dos ventanas separadas.
 * No hay autocorreccion del offset una vez iniciada la operacion normal.
 */
#define D4_CERO_N_MUESTRAS                 1000u
#define D4_CERO_ISR_ASENTAMIENTO          15000u /* 3.0 s a 5 kHz */
#define D4_CERO_ISR_ENTRE_VENTANAS         5000u /* 1.0 s a 5 kHz */
#define D4_CERO_MAX_DERIVA_CODIGOS            4u /* 30 mA nominal */
#define D4_CERO_CODIGO_MIN                 1650u
#define D4_CERO_CODIGO_MAX                 2000u
#define D4_CERO_MAX_RANGO_CODIGOS           60u
#define D4_CERO_MAX_DESV_CODIGOS            12u
#define D4_CERO_MAX_SIN_ADC                 10u

typedef enum {
    D4_CERO_CAPTURANDO = 0,
    D4_CERO_LISTO = 1,
    D4_CERO_FALLO_CODIGO = 2,
    D4_CERO_FALLO_RUIDO = 3,
    D4_CERO_FALLO_ADC = 4,
    D4_CERO_FALLO_DERIVA = 5
} d4_cero_estado_t;

typedef struct {
    d4_cero_estado_t estado;
    uint16_t codigo_cero;
    uint16_t minimo;
    uint16_t maximo;
    uint16_t ultimo_codigo;
    uint32_t muestras;
    uint32_t sin_adc_seguidas;
    uint32_t suma_codigos;
    int32_t suma_deltas;
    uint32_t suma_deltas_cuadrado;
    /* Campos adicionales para estabilizacion y verificacion del autocero. */
    uint32_t espera_restante;
    uint16_t promedio_primera_ventana;
    uint8_t fase;
} d4_cero_auto_t;

void d4_cero_auto_iniciar(volatile d4_cero_auto_t *c);
/* Una llamada por ISR con unicamente el codigo fresco del XADC.
 * D4_CERO_LISTO solo se publica si ambas ventanas son estables y coherentes.
 * Los errores se enclavan. El llamador existente detiene el PWM al fallar. */
d4_cero_estado_t d4_cero_auto_paso_isr(volatile d4_cero_auto_t *c,
                                       bool adc_nuevo, uint16_t codigo);
#endif
