/* D4 - Prueba NO BLOQUEANTE DEL LAZO: esta prueba se llama UNA vez
 * en main() ANTES de iniciar el AXI Timer y habilitar interrupciones.
 * Su espera limitada NO debe trasladarse a la ISR periódica.
 * No habilita PWM ni interpreta voltaje como corriente.
 */
#include "d4_xadc_arranque.h"
#include "xadc_p4.h"
#include "xil_printf.h"
#include <stdint.h>

#define D4_XADC_MAX_INTENTOS 200000u

int d4_xadc_prueba_arranque(void)
{
    xadc_p4_t adc;
    uint16_t codigo = 0u;
    int estado;

    xil_printf("\r\nD4 XADC: prueba de conversion por software (sin PWM)\r\n");
    estado = xadc_p4_iniciar(&adc, xadc_p4_base_por_defecto());
    if (estado != XADC_P4_OK) {
        xil_printf("D4 XADC: FAIL inicializacion/configuracion, rc=%d\r\n", estado);
        return estado;
    }
    xil_printf("D4 XADC: configuracion VAUX3/eventos/unipolar OK\r\n");

    /* Una sola conversión a demanda. El bucle de espera tiene límite.
     * Es admisible en la puesta en marcha, NO en la interrupción. */
    estado = xadc_p4_convertir_y_esperar(&adc, &codigo, D4_XADC_MAX_INTENTOS);
    if (estado != XADC_P4_OK) {
        xil_printf("D4 XADC: FAIL sin EOC nuevo, rc=%d\r\n", estado);
        return estado;
    }

    /* El driver D3 contabiliza canales erróneos, pero no los rechaza;
     * este ensayo sí los rechaza explícitamente. */
    if (adc.canal_erroneo != 0u) {
        xil_printf("D4 XADC: FAIL canal distinto de VAUX3 (%lu)\r\n",
                   (unsigned long)adc.canal_erroneo);
        return XADC_P4_ERR_CONFIG;
    }

    xil_printf("D4 XADC: PASS EOC nuevo, VAUX3 codigo=%u, entrada~%lu mV\r\n",
               (unsigned int)codigo,
               (unsigned long)(xadc_p4_codigo_a_uv(codigo) / 1000u));
    xil_printf("D4 XADC: sin sensor calibrado, NO interpretar como amperios\r\n");
    return XADC_P4_OK;
}
