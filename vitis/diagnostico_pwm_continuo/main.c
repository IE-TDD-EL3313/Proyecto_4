/*
 * EL3313 - Proyecto 4
 * Diagnostico continuo PWM-XADC
 *
 * Nexys 4 Rev. B - MicroBlaze V 100 MHz
 *
 * PWM: 10 kHz
 * Timer: 5 kHz
 * UART: 115200 baudios
 *
 * SIN etapa de potencia.
 * SIN control PR en lazo cerrado.
 */

#include "xil_exception.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xil_types.h"
#include "xtmrctr.h"
#include "xtmrctr_l.h"
#include "xintc.h"
#include "xparameters.h"
#include "xuartlite_l.h"
#include "d4_pwm_sync.h"

#include <stdint.h>
#include <stdbool.h>

#define TIMER_BASE       ((UINTPTR)0x41C00000U)
#define UART_BASE        ((UINTPTR)0x40600000U)

#define TIMER_PERIODICO  0U
#define CLK_HZ           100000000U
#define TIMER_HZ         5000U

#define CICLOS_PERIODO   (CLK_HZ / TIMER_HZ)
#define TIMER_RELOAD     (0xFFFFFFFFU - (CICLOS_PERIODO - 2U))

typedef enum {
    STATE_INIT = 0,
    STATE_READY,
    STATE_RUNNING,
    STATE_STOPPED,
    STATE_FAULT
} system_state_t;

static XTmrCtr timer;
static XIntc intc;

static volatile system_state_t state = STATE_INIT;
static volatile uint32_t irq_count = 0U;
static volatile bool fault_pending = false;

static bool timer_initialized = false;
static bool intc_initialized = false;

/* Limpia un evento pendiente del AXI Timer.
 * Llamar solo con el temporizador detenido y la IRQ deshabilitada.
 */
static void timer_clear_pending(void)
{
    uint32_t csr;

    csr = XTmrCtr_ReadReg(
        TIMER_BASE,
        TIMER_PERIODICO,
        XTC_TCSR_OFFSET
    );

    if ((csr & XTC_CSR_INT_OCCURED_MASK) != 0U) {

        XTmrCtr_WriteReg(
            TIMER_BASE,
            TIMER_PERIODICO,
            XTC_TCSR_OFFSET,
            (csr & ~(XTC_CSR_ENABLE_TMR_MASK |
                     XTC_CSR_INT_OCCURED_MASK)) |
            XTC_CSR_INT_OCCURED_MASK
        );
    }
}

/* ============================================================
 * INTERRUPCION PERIODICA
 * ============================================================ */

static void timer_callback(void *context, u8 timer_number)
{
    (void)context;

    if (timer_number != TIMER_PERIODICO)
        return;

    if (state != STATE_RUNNING)
        return;

    d4_pwm_sync_poll_isr();

    ++irq_count;

    if (d4_pwm_sync_fault() != 0U ||
        !d4_pwm_sync_is_started()) {

        d4_pwm_sync_stop();
        fault_pending = true;
        state = STATE_FAULT;
    }
}

/* ============================================================
 * UART
 * ============================================================ */

static bool uart_read_char(char *out)
{
    if (out == 0)
        return false;

    if (XUartLite_IsReceiveEmpty(UART_BASE))
        return false;

    *out = (char)XUartLite_RecvByte(UART_BASE);

    return true;
}

/* ============================================================
 * ESTADISTICAS
 * ============================================================ */

static void print_status(void)
{
    d4_pwm_sync_stats_t stats;

    d4_pwm_sync_stats(&stats);

    xil_printf("\r\n========== ESTADO ==========\r\n");
    xil_printf("Estado: %d\r\n", (int)state);

    xil_printf("ISR: %lu\r\n",
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

    xil_printf("Falla: %lu\r\n",
               (unsigned long)stats.fallas);

    xil_printf("PWM enable: %lu\r\n",
               (unsigned long)stats.enable);

    xil_printf("PWM running: %lu\r\n",
               (unsigned long)stats.running);

    xil_printf("============================\r\n");
}

/* ============================================================
 * PARADA
 * ============================================================ */

static void stop_system(void)
{
    /* Evitar concurrencia entre main y la ISR. */
    Xil_ExceptionDisable();

    /* Deshabilitar las salidas PWM. */
    d4_pwm_sync_stop();

    if (intc_initialized) {
        XIntc_Disable(
            &intc,
            XPAR_FABRIC_AXI_TIMER_0_INTR
        );
    }

    if (timer_initialized) {
        XTmrCtr_Stop(&timer, TIMER_PERIODICO);
    timer_clear_pending();
    }

    if (intc_initialized) {
        XIntc_Acknowledge(
            &intc,
            XPAR_FABRIC_AXI_TIMER_0_INTR
        );
    }

    if (state != STATE_FAULT)
        state = STATE_STOPPED;
}

/* ============================================================
 * INICIALIZACION DE HARDWARE
 * ============================================================ */

static int init_hardware(void)
{
    int status;

    state = STATE_INIT;

    Xil_ExceptionDisable();

    if (d4_pwm_sync_init() != 0)
        return -1;

    if (!timer_initialized) {

        status = XTmrCtr_Initialize(&timer, TIMER_BASE);

        if (status != XST_SUCCESS)
            return -2;

        XTmrCtr_SetHandler(
            &timer,
            timer_callback,
            &timer
        );

        XTmrCtr_SetOptions(
            &timer,
            TIMER_PERIODICO,
            XTC_INT_MODE_OPTION |
            XTC_AUTO_RELOAD_OPTION
        );

        XTmrCtr_SetResetValue(
            &timer,
            TIMER_PERIODICO,
            TIMER_RELOAD
        );

        timer_initialized = true;
    }

    if (!intc_initialized) {

        status = XIntc_Initialize(
            &intc,
            XPAR_XINTC_0_BASEADDR
        );

        if (status != XST_SUCCESS)
            return -3;

        status = XIntc_Connect(
            &intc,
            XPAR_FABRIC_AXI_TIMER_0_INTR,
            (XInterruptHandler)XTmrCtr_InterruptHandler,
            (void *)&timer
        );

        if (status != XST_SUCCESS)
            return -4;

        Xil_ExceptionInit();

        Xil_ExceptionRegisterHandler(
            XIL_EXCEPTION_ID_INT,
            (Xil_ExceptionHandler)XIntc_InterruptHandler,
            (void *)&intc
        );

        status = XIntc_Start(
            &intc,
            XIN_REAL_MODE
        );

        if (status != XST_SUCCESS)
            return -5;

        /* Mantener la IRQ deshabilitada hasta recibir E. */
        XIntc_Disable(
            &intc,
            XPAR_FABRIC_AXI_TIMER_0_INTR
        );

        intc_initialized = true;
    }

    irq_count = 0U;
    fault_pending = false;

    state = STATE_READY;

    return 0;
}

/* ============================================================
 * HABILITACION
 * ============================================================ */

static void enable_system(void)
{
    if (state != STATE_READY) {
        xil_printf("ERROR: sistema no esta en READY\r\n");
        return;
    }

    if (d4_pwm_sync_fault() != 0U) {
        state = STATE_FAULT;
        xil_printf("ERROR: falla registrada\r\n");
        return;
    }

    if (!timer_initialized || !intc_initialized) {
        state = STATE_FAULT;
        d4_pwm_sync_stop();
        xil_printf("ERROR: perifericos no inicializados\r\n");
        return;
    }

    /* Preparar arranque sin interrupciones activas. */
    Xil_ExceptionDisable();

    XIntc_Disable(
        &intc,
        XPAR_FABRIC_AXI_TIMER_0_INTR
    );

    XTmrCtr_Stop(&timer, TIMER_PERIODICO);
        timer_clear_pending();

    XIntc_Acknowledge(
        &intc,
        XPAR_FABRIC_AXI_TIMER_0_INTR
    );

    /* Habilitar PWM antes de iniciar el temporizador. */
    if (d4_pwm_sync_enable() != 0) {

        d4_pwm_sync_stop();

        state = STATE_FAULT;

        xil_printf("ERROR: PWM no habilitado\r\n");
        return;
    }

    state = STATE_RUNNING;

    XIntc_Enable(
        &intc,
        XPAR_FABRIC_AXI_TIMER_0_INTR
    );

    XTmrCtr_Start(&timer, TIMER_PERIODICO);

    Xil_ExceptionEnable();

    xil_printf("PWM habilitado. Operacion continua.\r\n");
}

/* ============================================================
 * PROGRAMA PRINCIPAL
 * ============================================================ */

int main(void)
{
    char command;

    xil_printf("\r\n================================\r\n");
    xil_printf("EL3313 - PROYECTO 4\r\n");
    xil_printf("PWM-XADC CONTINUO\r\n");
    xil_printf("Nexys 4 Rev. B\r\n");
    xil_printf("PWM 10 kHz | Timer 5 kHz\r\n");
    xil_printf("SIN etapa de potencia\r\n");
    xil_printf("================================\r\n");

    if (init_hardware() != 0) {

        d4_pwm_sync_stop();

        state = STATE_FAULT;

        xil_printf("ERROR: inicializacion fallida\r\n");
    } else {

        xil_printf("Sistema READY\r\n");
    }

    xil_printf("\r\nCOMANDOS:\r\n");
    xil_printf("E: habilitar PWM\r\n");
    xil_printf("S: detener PWM\r\n");
    xil_printf("I: estadisticas\r\n");
    xil_printf("R: preparar sistema\r\n");

    while (1) {

        if (fault_pending) {

            fault_pending = false;

            stop_system();

            xil_printf("\r\nFALLA DETECTADA\r\n");
            print_status();
        }

        if (!uart_read_char(&command))
            continue;

        switch (command) {

        case 'E':
        case 'e':

            enable_system();
            break;

        case 'S':
        case 's':

            if (state == STATE_FAULT || fault_pending) {

                xil_printf(
                    "ERROR: sistema bloqueado por falla\r\n"
                );

            } else if (state == STATE_RUNNING ||
                       state == STATE_READY ||
                       state == STATE_STOPPED) {

                stop_system();

                xil_printf("PWM detenido\r\n");

            } else {

                xil_printf(
                    "ERROR: sistema no disponible\r\n"
                );
            }

            break;

        case 'I':
        case 'i':

            print_status();
            break;

        case 'R':
        case 'r':

            if (state == STATE_STOPPED) {

                if (init_hardware() == 0) {

                    xil_printf("Sistema READY\r\n");

                } else {

                    d4_pwm_sync_stop();

                    state = STATE_FAULT;

                    xil_printf("ERROR: rearme fallido\r\n");
                }

            } else {

                xil_printf("Rearme no permitido\r\n");
            }

            break;

        case '\r':
        case '\n':
            break;

        default:
            xil_printf("Comando desconocido\r\n");
            break;
        }
    }

    return 0;
}
