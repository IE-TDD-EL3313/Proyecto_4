#include "d4_corriente_sensor.h"
#include "d4_cero_auto.h"
#include <stddef.h>

/* El cero cambia SOLO cuando concluyen las 1000 muestras con 0 A.
 * Operacion sobre entero de 16 bits, sin esperas ni llamadas de UART. */
static volatile uint16_t g_codigo_cero = D4_SENSOR_CODIGO_CERO;

bool d4_corriente_sensor_fijar_cero(uint16_t codigo_cero)
{
    if (codigo_cero < D4_CERO_CODIGO_MIN ||
        codigo_cero > D4_CERO_CODIGO_MAX) return false;
    g_codigo_cero = codigo_cero;
    return true;
}

uint16_t d4_corriente_sensor_obtener_cero(void)
{
    return g_codigo_cero;
}

bool d4_corriente_sensor_codigo_a_ma(uint16_t codigo, int32_t *corriente_ma)
{
    int32_t diferencia;
    int32_t numerador;
    if (corriente_ma == NULL ||
        codigo < D4_SENSOR_CODIGO_MIN_PLAUSIBLE ||
        codigo > D4_SENSOR_CODIGO_MAX_PLAUSIBLE) return false;

    diferencia = (int32_t)codigo - (int32_t)g_codigo_cero;
    numerador = diferencia * 15;  /* 7.5 mA por codigo */
    if (numerador >= 0) ++numerador;
    else --numerador;
    *corriente_ma = numerador / 2; /* division entera C99, simetrica */
    return true;
}
