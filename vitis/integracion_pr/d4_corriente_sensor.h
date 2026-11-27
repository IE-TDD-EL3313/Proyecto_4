#ifndef D4_CORRIENTE_SENSOR_H
#define D4_CORRIENTE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

/* Ganancia probada por D4 con 0 y 0.63 A: 7.5 mA/codigo.
 * El numero 1779 es el offset de RESPALDO, sustituido tras un autocero valido.
 * Los limites plausibles de codigo NO equivalen a proteccion de sobrecorriente.
 */
#define D4_SENSOR_CODIGO_CERO 1779u
#define D4_SENSOR_CODIGO_MIN_PLAUSIBLE 1080u
#define D4_SENSOR_CODIGO_MAX_PLAUSIBLE 2490u

/* Solo aceptar como cero valores validados por el modulo de autocero. */
bool d4_corriente_sensor_fijar_cero(uint16_t codigo_cero);
uint16_t d4_corriente_sensor_obtener_cero(void);

/* Conversion entera usada dentro de ISR: i[mA]=(ADC-cero)*15/2. */
bool d4_corriente_sensor_codigo_a_ma(uint16_t codigo, int32_t *corriente_ma);

#endif
