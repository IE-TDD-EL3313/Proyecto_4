/*
 * Diagnostico independiente PWM -> XADC
 * EL3313 - Proyecto 4 - Nexys 4 Rev. B
 *
 * Sin sensor calibrado ni etapa de potencia.
 * Requiere bitstream con pwm_output_guard.
 */

#include "xil_exception.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtmrctr.h"
#include "xintc.h"
#include "xparameters.h"
#include "d4_pwm_sync.h"

#include <stdint.h>

#define TIMER_BASE       ((UINTPTR)0x41C00000U)
#define TIMER_PERIODICO  0U
#define CLK_HZ           100000000U
#define TIMER_HZ         5000U
#define TOTAL_ISR        25000U

#define CICLOS_PERIODO   (CLK_HZ / TIMER_HZ)
#define TIMER_RELOAD     (0xFFFFFFFFU - (CICLOS_PERIODO - 2U))

static XTmrCtr timer;
static XIntc intc;

static volatile uint32_t irq_count = 0U;
static volatile uint32_t terminado = 0U;

static void timer_callback(void *context, u8 timer_number)
{
    (void)context;

    if (timer_number != TIMER_PERIODICO || terminado != 0U)
        return;

    d4_pwm_sync_poll_isr();

    ++irq_count;

    if (irq_count >= TOTAL_ISR) {
        d4_pwm_sync_stop();
        terminado = 1U;
    }
}

int main(void)
{
    int status;
    d4_pwm_sync_stats_t stats;

    xil_printf("\r\nDIAGNOSTICO PWM-XADC CONTINUO\r\n");
    xil_printf("Nexys 4 Rev. B | MicroBlaze V 100 MHz\r\n");
    xil_printf("PWM interno: 10 kHz | Timer: 5 kHz\r\n");
    xil_printf("Duracion nominal: 5 segundos\r\n");
    xil_printf("Sin control PR ni calibracion de corriente\r\n");

    if (d4_pwm_sync_start() != 0) {
        xil_printf("ERROR: inicializacion PWM-XADC\r\n");
        return 1;
    }

    status = XTmrCtr_Initialize(&timer, TIMER_BASE);
    if (status != XST_SUCCESS) {
        d4_pwm_sync_stop();
        xil_printf("ERROR: Timer init=%d\r\n", status);
        return 1;
    }

    XTmrCtr_SetHandler(&timer, timer_callback, &timer);

    XTmrCtr_SetOptions(
        &timer,
        TIMER_PERIODICO,
        XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION
    );

    XTmrCtr_SetResetValue(
        &timer,
        TIMER_PERIODICO,
        TIMER_RELOAD
    );

    status = XIntc_Initialize(&intc, XPAR_XINTC_0_BASEADDR);
    if (status != XST_SUCCESS) {
        d4_pwm_sync_stop();
        xil_printf("ERROR: INTC init=%d\r\n", status);
        return 1;
    }

    status = XIntc_Connect(
        &intc,
        XPAR_FABRIC_AXI_TIMER_0_INTR,
        (XInterruptHandler)XTmrCtr_InterruptHandler,
        (void *)&timer
    );

    if (status != XST_SUCCESS) {
        d4_pwm_sync_stop();
        xil_printf("ERROR: INTC connect=%d\r\n", status);
        return 1;
    }

    Xil_ExceptionInit();

    Xil_ExceptionRegisterHandler(
        XIL_EXCEPTION_ID_INT,
        (Xil_ExceptionHandler)XIntc_InterruptHandler,
        (void *)&intc
    );

    status = XIntc_Start(&intc, XIN_REAL_MODE);
    if (status != XST_SUCCESS) {
        d4_pwm_sync_stop();
        xil_printf("ERROR: INTC start=%d\r\n", status);
        return 1;
    }

    XIntc_Enable(&intc, XPAR_FABRIC_AXI_TIMER_0_INTR);

    XTmrCtr_Start(&timer, TIMER_PERIODICO);
    Xil_ExceptionEnable();

    while (terminado == 0U) {
        /* La adquisicion ocurre exclusivamente en la ISR. */
    }

    Xil_ExceptionDisable();
    XTmrCtr_Stop(&timer, TIMER_PERIODICO);
    d4_pwm_sync_stop();

    d4_pwm_sync_stats(&stats);

    xil_printf("\r\n========== RESULTADOS ==========\r\n");
    xil_printf("ISR ejecutadas: %lu\r\n",
               (unsigned long)irq_count);
    xil_printf("Consultas XADC: %lu\r\n",
               (unsigned long)stats.polls);
    xil_printf("Muestras nuevas: %lu\r\n",
               (unsigned long)stats.nuevas);
    xil_printf("Sin muestra: %lu\r\n",
               (unsigned long)stats.sin_nueva);
    xil_printf("Canal erroneo: %lu\r\n",
               (unsigned long)stats.canal_erroneo);
    xil_printf("Ultimo codigo: %lu\r\n",
               (unsigned long)stats.ultimo_codigo);
    xil_printf("Fallas: %lu\r\n",
               (unsigned long)stats.fallas);
    xil_printf("PWM enable final: %lu\r\n",
               (unsigned long)stats.enable);
    xil_printf("PWM running final: %lu\r\n",
               (unsigned long)stats.running);
    xil_printf("================================\r\n");

    while (1) {
        /* Diagnostico finalizado; PWM detenido. */
    }

    return 0;
}
