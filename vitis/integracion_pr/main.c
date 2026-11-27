/*
 * D4 - Controlador PR discreto en MicroBlaze V / Nexys 4 Rev. B.
 * XSA: build/nexys4_pr/nexys4_control.xsa (Vivado 2026.1).
 *
 * Timer 0: ISR periodica a 5 kHz, Timer 1: contador libre.
 * Corriente mediante ACS712 + XADC, autocalibracion a 0 A.
 * PR P3 preparado para armado explicito, pero BLOQUEADO EN ESTA ENTREGA.
 * NUEVO: supervisar corriente no ordenada en duty=0 y corte al actuar.
 *
 * NUEVO: escribir 'c' en PuTTY tras terminar el cero automatico.
 * Captura sincronizada de ADC, referencia, corriente PR y tension PR a 1 kHz.
 * La ISR NO imprime ni hace lecturas ADC adicionales.
 */

#include "xil_exception.h"
#include "xil_printf.h"
#include "xstatus.h"
#include "xtmrctr.h"
#include "xintc.h"
#include "xparameters.h"
#include "xil_io.h"
#include "xuartlite_l.h"
#include <stdint.h>
#include <stdbool.h>

#include "d4_pr_p3.h"
#include "d4_pr_selftest.h"
#include "d4_xadc_probe.h"
#include "d4_xadc_arranque.h"
#include "d4_pwm_sync.h"
#include "d4_corriente_sensor.h"
#include "d4_controlador_real.h"
#include "d4_cero_auto.h"
#include "d4_captura_onda.h"

#define D4_TIMER_BASE       ((UINTPTR)0x41C00000U)
#define D4_UART_BASE        ((UINTPTR)0x40600000U)
#define D4_PERIODIC_TIMER   0U
#define D4_TIMESTAMP_TIMER  1U
#define D4_CLK_HZ           100000000U
#define D4_CONTROL_HZ       5000U
#define D4_PERIOD_CYCLES    (D4_CLK_HZ / D4_CONTROL_HZ)
#define D4_RELOAD           (0xFFFFFFFFU - (D4_PERIOD_CYCLES - 2U))
#define D4_REPORT_SAMPLES   5000U

/* BLOQUEO DOBLE. ESTA ENTREGA SE COMPILA SIN ACTUACION DE POTENCIA.
 * NO cambiar ninguna de estas macros hasta confirmar la causa de la
 * corriente a 50 Hz con DUTY=0, compuertas, interlock y proteccion HW. */
#define D4_CONTROL_PERMITIR_PWM 0u
#define D4_BANCO_PUENTE_VALIDADO 0u
#if D4_CONTROL_PERMITIR_PWM && !D4_BANCO_PUENTE_VALIDADO
#error "Actuacion de potencia exige validacion fisica independiente"
#endif

static d4_real_control_t g_real;
static volatile uint32_t g_real_pwm_detendido_por_falla = 0u;
/* Comandos escritos en main, consumidos SOLO en ISR para evitar carreras. */
static volatile uint32_t g_parada_pedida = 0u;
static volatile uint32_t g_parada_manual = 0u;
#if D4_CONTROL_PERMITIR_PWM && D4_BANCO_PUENTE_VALIDADO
static volatile uint32_t g_armado_pedido = 0u;
#endif

static XTmrCtr g_timer;
static XIntc g_intc;
static d4_pr_p3_t g_pr;

static volatile uint32_t g_pr_total = 0U;
static volatile uint32_t g_pr_fail_total = 0U;
static uint32_t g_pr_sat_window = 0U;

static volatile int32_t g_pr_last_ref_ma = 0;
static volatile int32_t g_pr_last_voltage_mv = 0;
static volatile int32_t g_pr_last_mod_q15 = 0;

static volatile uint32_t g_irq_count = 0U;
static volatile d4_cero_auto_t g_cero;
static volatile d4_captura_onda_t g_captura;
static uint32_t g_prev_timestamp = 0U;
static uint8_t g_have_timestamp = 0U;

static uint32_t g_window_count = 0U;
/* En una ventana de 5000 ISR de 200 us la suma tipica es 100 000 000
 * ciclos. uint32_t basta para la ventana normal y evita div 64-bit en RV32.
 * Saturamos si existe un intervalo anomalamente grande. */
static uint32_t g_window_sum = 0U;
static uint32_t g_window_min = UINT32_MAX;
static uint32_t g_window_max = 0U;
static uint32_t g_window_max_handler = 0U;
static uint32_t g_window_long_intervals = 0U;

static volatile uint32_t g_report_seq = 0U;
static volatile uint32_t g_report_irq_count = 0U;
static volatile uint32_t g_report_min = 0U;
static volatile uint32_t g_report_max = 0U;
static volatile uint32_t g_report_avg = 0U;
static volatile uint32_t g_report_handler_max = 0U;
static volatile uint32_t g_report_long_intervals = 0U;
static volatile uint32_t g_report_pr_total = 0U;
static volatile uint32_t g_report_pr_fail_total = 0U;
static volatile uint32_t g_report_pr_sat = 0U;
static volatile int32_t g_report_pr_ref_ma = 0;
static volatile int32_t g_report_pr_voltage_mv = 0;
static volatile int32_t g_report_pr_mod_q15 = 0;

static volatile uint32_t g_sensor_p2_ultimo_codigo = 0u;
static volatile int32_t g_sensor_p2_ultima_corriente_ma = 0;
static volatile uint32_t g_sensor_p2_aceptadas = 0u;
static volatile uint32_t g_sensor_p2_rechazadas = 0u;

/* =========================================================
 * CALLBACK DEL TIMER: ADC -> PR -> PWM (observacion) + captura
 * ========================================================= */
static void timer_callback(void *context, u8 timer_number)
{
    (void)context;

    if (timer_number == D4_PERIODIC_TIMER) {
        uint16_t codigo_adc = 0u;
        bool muestra_nueva;
        d4_real_resultado_t r;

        if (g_parada_pedida != 0u) {
            g_parada_pedida = 0u;
            g_parada_manual = 1u;
            g_real.salida_autorizada = false;
            d4_pr_p3_disarm(&g_pr);
            d4_pwm_sync_stop();
        }
        /* El paro manual se enclava hasta reiniciar la placa. */
        if (g_parada_manual != 0u) {
            ++g_irq_count;
            return;
        }

        /* Exactamente una consulta XADC por ISR a 5 kHz. */
        d4_pwm_sync_poll_isr();
        muestra_nueva = d4_pwm_sync_codigo_fresco_isr(&codigo_adc);

        /* Primera etapa: cero automatico (con potencia desenergizada). */
        if (g_cero.estado == D4_CERO_CAPTURANDO) {
            d4_cero_estado_t estado =
                d4_cero_auto_paso_isr(&g_cero, muestra_nueva, codigo_adc);

            if (estado == D4_CERO_LISTO) {
                if (d4_corriente_sensor_fijar_cero(g_cero.codigo_cero)) {
                    d4_pr_p3_arm(&g_pr);
                    d4_real_init(&g_real, false);
                } else {
                    g_cero.estado = D4_CERO_FALLO_CODIGO;
                    d4_pwm_sync_stop();
                }
            } else if (estado != D4_CERO_CAPTURANDO) {
                d4_pwm_sync_stop();
            }
            ++g_irq_count;
            return;
        }

        if (g_cero.estado != D4_CERO_LISTO) {
            ++g_irq_count;
            return;
        }

        /* Segunda etapa: PR con corriente corregida. */
        (void)d4_real_paso_isr(&g_real, &g_pr,
                               muestra_nueva, codigo_adc, &r);

        if (r.adc_nuevo) {
            g_sensor_p2_ultimo_codigo = (uint32_t)r.codigo_adc;
            if (r.sensor_valido) {
                g_sensor_p2_ultima_corriente_ma = r.corriente_ma;
                ++g_sensor_p2_aceptadas;
            } else {
                ++g_sensor_p2_rechazadas;
            }
        }

        if (r.pr_calculado) {
            ++g_pr_total;
            if (r.pr.saturated) ++g_pr_sat_window;

            g_pr_last_ref_ma =
                (int32_t)(r.pr.reference_a * 1000.0f);
            g_pr_last_voltage_mv =
                (int32_t)(r.pr.limited_v * 1000.0f);
            g_pr_last_mod_q15 = (int32_t)r.pr.m_q15;
        } else if (r.estado == D4_REAL_FALLO_PR) {
            ++g_pr_fail_total;
        }

        /* Prioridad a apagar el generador si el controlador enclava fallo. */
        if (g_real.fallo_enclavado != 0u &&
            g_real_pwm_detendido_por_falla == 0u) {
            d4_pwm_sync_stop();
            g_real_pwm_detendido_por_falla = 1u;
        }

#if D4_CONTROL_PERMITIR_PWM && D4_BANCO_PUENTE_VALIDADO
        /* Solo se puede armar tras 1 segundo CONSECUTIVO de neutro
         * estable, sin fallos y con ADC valido. El PR se reinicia en
         * fase cero; NO se aplica la salida vieja calculada en sombra. */
        if (g_armado_pedido != 0u) {
            g_armado_pedido = 0u;
            if (g_real.fallo_enclavado == 0u &&
                !g_real.salida_autorizada &&
                r.estado == D4_REAL_OK &&
                g_real.neutro_estables_consecutivas >=
                    D4_REAL_NEUTRO_ESTABLE_ISR) {
                d4_pr_p3_arm(&g_pr);
                d4_real_init(&g_real, true);
            }
        }
#endif

        /* ADC y PR registrados en el MISMO paso ISR (decimacion 5:1).
         * Sin conversiones adicionales y sin impresion dentro de la ISR. */
        d4_captura_onda_paso_isr(&g_captura, &r,
                                (g_real.fallo_enclavado != 0u));

#if D4_CONTROL_PERMITIR_PWM && D4_BANCO_PUENTE_VALIDADO
        if (r.escribir_pwm && g_real.fallo_enclavado == 0u &&
            g_parada_manual == 0u) {
            if (!d4_pwm_sync_escribir_duty_isr(r.duty_mil)) {
                g_real.fallo_enclavado = D4_REAL_FALLO_PWM;
                g_real.salida_autorizada = false;
                d4_pr_p3_disarm(&g_pr);
                d4_pwm_sync_stop();
                g_real_pwm_detendido_por_falla = 1u;
            }
        }
#endif
        ++g_irq_count;
    }
}

/* =========================================================
 * HANDLER Y MEDICION TEMPORAL
 * ========================================================= */
static void d4_timer_exception(void *context)
{
    uint32_t timestamp =
        XTmrCtr_GetValue(&g_timer, D4_TIMESTAMP_TIMER);

    if (g_have_timestamp != 0U) {
        uint32_t period = timestamp - g_prev_timestamp;
        if (period < g_window_min) g_window_min = period;
        if (period > g_window_max) g_window_max = period;
        if (period >= 2U * D4_PERIOD_CYCLES) ++g_window_long_intervals;
        if (period > UINT32_MAX - g_window_sum)
            g_window_sum = UINT32_MAX;
        else
            g_window_sum += period;
        ++g_window_count;
    } else {
        g_have_timestamp = 1U;
    }

    g_prev_timestamp = timestamp;
    XTmrCtr_InterruptHandler(context);

    {
        uint32_t handler_cycles =
            XTmrCtr_GetValue(&g_timer, D4_TIMESTAMP_TIMER) - timestamp;
        if (handler_cycles > g_window_max_handler)
            g_window_max_handler = handler_cycles;
    }

    if (g_window_count >= D4_REPORT_SAMPLES) {
        g_report_irq_count = g_irq_count;
        g_report_min = g_window_min;
        g_report_max = g_window_max;
        g_report_avg = g_window_sum / g_window_count;
        g_report_handler_max = g_window_max_handler;
        g_report_long_intervals = g_window_long_intervals;
        g_report_pr_total = g_pr_total;
        g_report_pr_fail_total = g_pr_fail_total;
        g_report_pr_sat = g_pr_sat_window;
        g_report_pr_ref_ma = g_pr_last_ref_ma;
        g_report_pr_voltage_mv = g_pr_last_voltage_mv;
        g_report_pr_mod_q15 = g_pr_last_mod_q15;
        ++g_report_seq;

        g_window_count = 0U;
        g_window_sum = 0U;
        g_window_min = UINT32_MAX;
        g_window_max = 0U;
        g_window_max_handler = 0U;
        g_window_long_intervals = 0U;
        g_pr_sat_window = 0U;
    }
}

/* =========================================================
 * REPORTE CSV DE CAPTURA (SOLO EN MAIN; NUNCA DESDE ISR)
 * ========================================================= */
static void d4_captura_imprimir_csv(void)
{
    uint32_t i;
    uint32_t n = (uint32_t)g_captura.n;
    uint32_t estado = (uint32_t)g_captura.estado;
    uint32_t cero = (uint32_t)d4_corriente_sensor_obtener_cero();

    /* Publicar datos tras un estado terminal: ISR no vuelve a escribirlos. */
    xil_printf("\r\nD4_CSV_BEGIN estado=%lu muestras=%lu offset=%lu"
               " fs_registro=1000Hz fs_control=5000Hz\r\n",
               (unsigned long)estado,
               (unsigned long)n,
               (unsigned long)cero);
    xil_printf("indice,tiempo_us,codigo_adc,corriente_bruta_ma,"
               "corriente_pr_ma,referencia_ma,error_ma,u_raw_mv,"
               "u_limit_mv,duty_mil,saturado,pico_ignorado\r\n");

    for (i = 0u; i < n; ++i) {
        const volatile d4_captura_pr_punto_t *p = &g_captura.puntos[i];
        int32_t bruto_ma = 0;
        int32_t corriente_pr = (int32_t)p->corriente_pr_ma;
        int32_t ref_ma = (int32_t)p->referencia_ma;
        /* El ISR ya valido este codigo: obtener la misma conversion. */
        (void)d4_corriente_sensor_codigo_a_ma(p->codigo_adc, &bruto_ma);
        xil_printf("%lu,%lu,%lu,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lu,%lu\r\n",
                   (unsigned long)i,
                   (unsigned long)(i * D4_CAPTURA_PERIODO_US),
                   (unsigned long)p->codigo_adc,
                   (long)bruto_ma,
                   (long)corriente_pr,
                   (long)ref_ma,
                   (long)(ref_ma - corriente_pr),
                   (long)p->u_raw_mv,
                   (long)p->u_limit_mv,
                   (long)p->duty_mil,
                   (unsigned long)((p->flags & D4_CAPTURA_FLAG_SATURADO) != 0u),
                   (unsigned long)((p->flags & D4_CAPTURA_FLAG_PICO) != 0u));
    }

    xil_printf("D4_CSV_END estado=%lu muestras=%lu\r\n",
               (unsigned long)estado, (unsigned long)n);
    if (estado == D4_CAPTURA_ERROR_ADC) {
        xil_printf("CAPTURA INCOMPLETA: ADC ausente/invalido.\r\n");
    } else if (estado == D4_CAPTURA_ERROR_PROTECCION) {
        xil_printf("CAPTURA INCOMPLETA: proteccion activada; "
                   "NO rearmar sin diagnostico.\r\n");
    } else if (estado == D4_CAPTURA_ERROR_PR) {
        xil_printf("CAPTURA INCOMPLETA: PR no calculado.\r\n");
    } else {
        xil_printf("CAPTURA COMPLETA: 100 puntos; "
                   "t=0..99 ms; cinco ciclos nominales de 50 Hz.\r\n");
    }
    d4_captura_onda_liberar(&g_captura);
     /* Diagnostico de registros AXI del PWM */
    uint32_t ctrl = Xil_In32((UINTPTR)0x44A00000u);
    uint32_t duty = Xil_In32((UINTPTR)0x44A00004u);
    uint32_t dead = Xil_In32((UINTPTR)0x44A00008u);

    xil_printf("PWM AXI: CTRL=0x%08lx, DUTY=%d, DEAD=%lu\r\n",
               (unsigned long)ctrl,
               (int)(int16_t)(duty & 0xFFFFu),
               (unsigned long)(dead & 0xFFFFu));
}

/* =========================================================
 * PROGRAMA PRINCIPAL
 * ========================================================= */
int main(void)
{
    int status;
    uint32_t previous_report_seq = 0U;

    xil_printf("\r\nD4: CONTROLADOR PR CON CORRIENTE REAL\r\n");
    xil_printf("Timer0: 5 kHz, objetivo 200 us; Timer1: 100 MHz libre\r\n");
    xil_printf("Periodo objetivo: %lu ciclos; recarga 0x%08lx\r\n",
               (unsigned long)D4_PERIOD_CYCLES,
               (unsigned long)D4_RELOAD);
    xil_printf("PR P3: referencia de 0.2 A pico a 50 Hz.\r\n");
    xil_printf("Modo: PR calculado; potencia BLOQUEADA en este build.\r\n");
#if D4_CONTROL_PERMITIR_PWM && D4_BANCO_PUENTE_VALIDADO
    xil_printf("UART: c=CSV, x=PARADA ENCLAVADA, a=solicitar armado.\r\n");
#else
    xil_printf("UART: c=CSV, x=PARADA ENCLAVADA, a=armar (bloqueado).\r\n");
#endif
    xil_printf("CAPTURA: al terminar cero automatico, "
               "escribir 'c' en PuTTY para CSV sincronizado ADC + PR a 1 kHz.\r\n");

    if (d4_pr_selftest_run() != 0) {
        xil_printf("PR no paso prueba numerica.\r\n");
        return 1;
    }

    d4_pr_p3_init(&g_pr);
    if (!d4_pr_p3_set_amplitude(&g_pr, 0.2f)) {
        xil_printf("ERROR: amplitud PR invalida.\r\n");
        return 1;
    }

    d4_pr_p3_disarm(&g_pr);
    d4_real_init(&g_real, false);
    d4_cero_auto_iniciar(&g_cero);
    d4_captura_onda_init(&g_captura);

    d4_xadc_probe_print();
    /* No efectuar aqui una conversion disparada por software. El flujo real
     * usa exclusivamente los eventos del PWM; una conversion previa puede
     * dejar un EOC residual que el autocero interpretaria como su primera
     * muestra sincronizada. La prueba independiente de XADC se conserva en
     * d4_xadc_arranque.c para diagnostico, pero no forma parte del arranque
     * integrado. */
    xil_printf("D4 XADC: muestreo sincronizado exclusivamente por PWM\r\n");

    if (d4_pwm_sync_start() != 0) {
        xil_printf("ERROR: PWM/XADC no iniciado\r\n");
        return 1;
    }

    status = XTmrCtr_Initialize(&g_timer, D4_TIMER_BASE);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR al inicializar XTmrCtr: %d\r\n", status);
        return 1;
    }

    XTmrCtr_SetHandler(&g_timer, timer_callback, &g_timer);

    XTmrCtr_SetOptions(&g_timer, D4_TIMESTAMP_TIMER,
                       XTC_AUTO_RELOAD_OPTION);
    XTmrCtr_SetResetValue(&g_timer, D4_TIMESTAMP_TIMER, 0U);
    XTmrCtr_Start(&g_timer, D4_TIMESTAMP_TIMER);

    XTmrCtr_SetOptions(&g_timer, D4_PERIODIC_TIMER,
                       XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);
    XTmrCtr_SetResetValue(&g_timer, D4_PERIODIC_TIMER, D4_RELOAD);

    status = XIntc_Initialize(&g_intc, XPAR_XINTC_0_BASEADDR);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR al inicializar AXI INTC: %d\r\n", status);
        return 1;
    }

    status = XIntc_Connect(
        &g_intc,
        XPAR_FABRIC_AXI_TIMER_0_INTR,
        (XInterruptHandler)d4_timer_exception,
        (void *)&g_timer
    );
    if (status != XST_SUCCESS) {
        xil_printf("ERROR al conectar interrupcion Timer: %d\r\n", status);
        return 1;
    }

    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(
        XIL_EXCEPTION_ID_INT,
        (Xil_ExceptionHandler)XIntc_InterruptHandler,
        (void *)&g_intc
    );

    status = XIntc_Start(&g_intc, XIN_REAL_MODE);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR al iniciar AXI INTC: %d\r\n", status);
        return 1;
    }

    XIntc_Enable(&g_intc, XPAR_FABRIC_AXI_TIMER_0_INTR);

    XTmrCtr_Start(&g_timer, D4_PERIODIC_TIMER);
    Xil_ExceptionEnable();

    /* =====================================================
     * BUCLE PRINCIPAL: UART DE COMANDOS Y REPORTES
     * ===================================================== */
    for (;;) {
        uint32_t seq;

        /* Puerto UARTLite RX no bloqueante: 'c' inicia una captura.
         * NO accede a registros XADC ni escribe al PWM. */
        if (!XUartLite_IsReceiveEmpty(D4_UART_BASE)) {
            uint8_t tecla = XUartLite_RecvByte(D4_UART_BASE);
            if (tecla == (uint8_t)'x' || tecla == (uint8_t)'X') {
                g_parada_pedida = 1u;
                xil_printf("PARADA SOLICITADA: la ISR bloqueara PWM y PR. "
                           "Reinicio fisico requerido.\r\n");
            } else if (tecla == (uint8_t)'a' ||
                       tecla == (uint8_t)'A') {
#if D4_CONTROL_PERMITIR_PWM && D4_BANCO_PUENTE_VALIDADO
                if (g_parada_manual != 0u ||
                    g_real_pwm_detendido_por_falla != 0u ||
                    g_cero.estado != D4_CERO_LISTO ||
                    g_real.fallo_enclavado != 0u ||
                    g_real.neutro_estables_consecutivas <
                        D4_REAL_NEUTRO_ESTABLE_ISR) {
                    xil_printf("ARMADO DENEGADO: neutro no estable o "
                               "calibracion/fallo/parada.\r\n");
                } else {
                    g_armado_pedido = 1u;
                    xil_printf("ARMADO SOLICITADO: la ISR decide "
                               "si se cumplen condiciones.\r\n");
                }
#else
                xil_printf("ARMADO BLOQUEADO EN COMPILACION: "
                           "antes validar compuertas, corriente no "
                           "ordenada, interlock y corte hardware.\r\n");
#endif
            } else if (tecla == (uint8_t)'c' ||
                       tecla == (uint8_t)'C') {
                if (g_cero.estado != D4_CERO_LISTO) {
                    xil_printf("CAPTURA NO INICIADA: "
                               "calibracion a 0 A no completada.\r\n");
                } else if (g_real.fallo_enclavado != 0u ||
                           g_real_pwm_detendido_por_falla != 0u) {
                    xil_printf("CAPTURA NO INICIADA: "
                               "fallo/proteccion activa.\r\n");
                } else if (d4_captura_onda_solicitar(&g_captura)) {
                    xil_printf("CAPTURA ARMADA: "
                               "100 puntos, 1 kHz, 100 ms nominales.\r\n");
                } else {
                    xil_printf("CAPTURA OCUPADA: "
                               "esperar reporte actual.\r\n");
                }
            }
        }

        if (d4_captura_onda_terminada(&g_captura)) {
            d4_captura_imprimir_csv();
        }

        seq = g_report_seq;
        if (seq != previous_report_seq) {
            uint32_t irq, min_cycles, max_cycles, avg_cycles;
            uint32_t max_handler, long_intervals;
            uint32_t pr_total, pr_fail_total, pr_sat;
            d4_pwm_sync_stats_t s;

            /* Snapshot consistente de las estadisticas publicadas por la ISR.
             * Se conservan los contadores; solo se acorta la salida por UART
             * para reducir el espacio de codigo y cadenas en la BRAM. */
            do {
                seq = g_report_seq;
                irq = g_report_irq_count;
                min_cycles = g_report_min;
                max_cycles = g_report_max;
                avg_cycles = g_report_avg;
                max_handler = g_report_handler_max;
                long_intervals = g_report_long_intervals;
                pr_total = g_report_pr_total;
                pr_fail_total = g_report_pr_fail_total;
                pr_sat = g_report_pr_sat;
            } while (seq != g_report_seq);

            previous_report_seq = seq;
            xil_printf("\r\nIRQ=%lu, T=%lu ciclos, min=%lu, max=%lu, "
                       "handler=%lu, largos=%lu\r\n",
                       (unsigned long)irq,
                       (unsigned long)avg_cycles,
                       (unsigned long)min_cycles,
                       (unsigned long)max_cycles,
                       (unsigned long)max_handler,
                       (unsigned long)long_intervals);
            xil_printf("PR: pasos=%lu, fallos=%lu, sat/ventana=%lu\r\n",
                       (unsigned long)pr_total,
                       (unsigned long)pr_fail_total,
                       (unsigned long)pr_sat);

            d4_pwm_sync_stats(&s);
            xil_printf("SYNC: enable=%lu, running=%lu, fallas=%lu, "
                       "nuevas=%lu, sin_nueva=%lu\r\n",
                       (unsigned long)s.enable,
                       (unsigned long)s.running,
                       (unsigned long)s.fallas,
                       (unsigned long)s.nuevas,
                       (unsigned long)s.sin_nueva);
            xil_printf("SENSOR: codigo=%lu, corriente=%ld mA, "
                       "aceptadas=%lu, rechazadas=%lu\r\n",
                       (unsigned long)g_sensor_p2_ultimo_codigo,
                       (long)g_sensor_p2_ultima_corriente_ma,
                       (unsigned long)g_sensor_p2_aceptadas,
                       (unsigned long)g_sensor_p2_rechazadas);
            xil_printf("CTRL REAL: fallo=%lu, sobrecorrientes=%lu, "
                       "PR=%lu, sinADC=%lu, invalidas=%lu, actuar=%lu\r\n",
                       (unsigned long)g_real.fallo_enclavado,
                       (unsigned long)g_real.sobrecorrientes,
                       (unsigned long)g_real.calculos_pr,
                       (unsigned long)g_real.muestras_sin_adc,
                       (unsigned long)g_real.muestras_invalidas,
                       (unsigned long)(g_real.salida_autorizada ? 1u : 0u));
            xil_printf("CERO AUTO: estado=%lu, muestras=%lu/%lu, "
                       "offset=%lu, min=%lu, max=%lu\r\n",
                       (unsigned long)g_cero.estado,
                       (unsigned long)g_cero.muestras,
                       (unsigned long)D4_CERO_N_MUESTRAS,
                       (unsigned long)d4_corriente_sensor_obtener_cero(),
                       (unsigned long)g_cero.minimo,
                       (unsigned long)g_cero.maximo);
            if (g_cero.estado == D4_CERO_LISTO ||
                g_cero.estado == D4_CERO_FALLO_DERIVA) {
                uint32_t v1 = g_cero.promedio_primera_ventana;
                uint32_t v2 = (g_cero.suma_codigos +
                               D4_CERO_N_MUESTRAS / 2u) /
                               D4_CERO_N_MUESTRAS;
                xil_printf("CERO DIAG: V1=%lu, V2=%lu, DELTA=%ld codigos\r\n",
                           (unsigned long)v1,
                           (unsigned long)v2,
                           (long)((int32_t)v2 - (int32_t)v1));
            }
            xil_printf("FILTRO: ignorados=%lu, sospechosas=%lu, "
                       "consecutivas=%lu, sobremaxima=%lu\r\n",
                       (unsigned long)g_real.picos_ignorados,
                       (unsigned long)g_real.lecturas_sospechosas,
                       (unsigned long)g_real.consecutivas_sospechosas,
                       (unsigned long)g_real.consecutivas_sobremaxima);
            if (g_real.guardia_bloques_revisados != 0u) {
                xil_printf("GBASE: offset=%lu, media20=%lu, delta=%ld cod, neutro=%lu\r\n",
                           (unsigned long)d4_corriente_sensor_obtener_cero(),
                           (unsigned long)g_real.guardia_media_codigo,
                           (long)((int32_t)g_real.guardia_media_codigo -
                                  (int32_t)d4_corriente_sensor_obtener_cero()),
                           (unsigned long)(g_real.alarmas_neutro == 0u));
            }
            xil_printf("GUARDIA: neutro_estables=%lu/%lu, "
                       "altas_ventana=%lu, alarmas=%lu, paro_manual=%lu\r\n",
                       (unsigned long)g_real.neutro_estables_consecutivas,
                       (unsigned long)D4_REAL_NEUTRO_ESTABLE_ISR,
                       (unsigned long)g_real.neutro_fuera_ventana,
                       (unsigned long)g_real.alarmas_neutro,
                       (unsigned long)g_parada_manual);
            xil_printf("GEXC: +%lu (max=%ldmA), -%lu (min=%ldmA)\r\n",
                       (unsigned long)g_real.excursiones_pos_total,
                       (long)g_real.excursion_pos_max_ma,
                       (unsigned long)g_real.excursiones_neg_total,
                       (long)g_real.excursion_neg_min_ma);
            if (g_real.alarmas_neutro != 0u) {
                xil_printf("GDIAG: ADC=%lu..%lu, I=%ld..%ldmA, +%lu, -%lu, n=%lu\r\n",
                           (unsigned long)g_real.guardia_adc_min,
                           (unsigned long)g_real.guardia_adc_max,
                           (long)g_real.guardia_ma_min,
                           (long)g_real.guardia_ma_max,
                           (unsigned long)g_real.guardia_positivas,
                           (unsigned long)g_real.guardia_negativas,
                           (unsigned long)g_real.guardia_muestra_disparo);
            }
            xil_printf("PROTECCION: PWM detenido por falla=%lu\r\n",
                       (unsigned long)g_real_pwm_detendido_por_falla);
        }
    }
}
