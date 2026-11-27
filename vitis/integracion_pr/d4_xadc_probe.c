/*
 * D4 — Primer diagnostico del XADC del system_wrapper.xsa (Vivado 2026.1).
 * IP: xadc_wiz_0, base 0x44A10000, canal VAUX3.
 * SOLO lectura AXI: no inicia conversiones ni habilita PWM ni compuertas.
 * NO demuestra que un dato sea reciente: convst_in depende de pwm_trigger_o.
 * NO convierte a amperios: requiere offset/ganancia del acondicionamiento P2.
 */
#include "d4_xadc_probe.h"
#include "xil_io.h"
#include "xil_printf.h"
#include <stdint.h>

#define D4_XADC_BASE       ((UINTPTR)0x44A10000U)
#define D4_XADC_SR         0x004U
#define D4_XADC_VAUX3      0x24CU
#define D4_XADC_CFG0       0x300U
#define D4_XADC_CFG1       0x304U
#define D4_XADC_CFG2       0x308U

uint16_t d4_xadc_probe_last_raw12(void)
{
    /* XADC entrega 12 bits justificados a izquierda en registro de 16. */
    return (uint16_t)((Xil_In32(D4_XADC_BASE + D4_XADC_VAUX3) >> 4) & 0x0FFFU);
}

uint32_t d4_xadc_probe_raw12_to_mv(uint16_t raw12)
{
    /* Escala ideal 0 a 1 V; sin calibracion analogica. */
    return ((((uint32_t)raw12 & 0x0FFFU) * 1000U) + 2048U) / 4096U;
}

void d4_xadc_probe_print(void)
{
    uint32_t sr = Xil_In32(D4_XADC_BASE + D4_XADC_SR);
    uint32_t cfg0 = Xil_In32(D4_XADC_BASE + D4_XADC_CFG0) & 0xFFFFU;
    uint32_t cfg1 = Xil_In32(D4_XADC_BASE + D4_XADC_CFG1) & 0xFFFFU;
    uint32_t cfg2 = Xil_In32(D4_XADC_BASE + D4_XADC_CFG2) & 0xFFFFU;
    uint16_t raw = d4_xadc_probe_last_raw12();
    uint32_t mv = d4_xadc_probe_raw12_to_mv(raw);

    xil_printf("\r\nD4 XADC - SOLO DIAGNOSTICO AXI (NO ADC VALIDADO)\r\n");
    xil_printf("Base=0x44A10000; VAUX3=+0x24C; SR=0x%08lx\r\n",
               (unsigned long)sr);
    xil_printf("CFG0=0x%04lx (XSA 0x0216) | CFG1=0x%04lx (XSA 0x31A0)\r\n",
               (unsigned long)cfg0, (unsigned long)cfg1);
    xil_printf("CFG2=0x%04lx (XSA 0x0400)\r\n", (unsigned long)cfg2);
    xil_printf("ULTIMO registro VAUX3: codigo12=%u, aprox=%lu mV\r\n",
               (unsigned int)raw, (unsigned long)mv);
    xil_printf("IMPORTANTE: no se verifica EOC ni muestra nueva. No usar para PR.\r\n");
    xil_printf("Sin pwm_trigger_o activo, el valor puede ser obsoleto.\r\n");
}
