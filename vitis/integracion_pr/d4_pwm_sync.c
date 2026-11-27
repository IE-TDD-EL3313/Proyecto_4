/* Ensayo PWM -> XADC con Timer 5 kHz, sin etapa de potencia.
 * Verifica disponibilidad de conversiones nuevas; NO verifica fase exacta ni
 * numero real de triggers por periodo PWM (a 10 kHz hay 2 entre ISR de 5kHz).
 */
#include "d4_pwm_sync.h"
#include "xadc_p4.h"
#include "xil_io.h"
#include "xparameters.h"
#include <stdbool.h>
#include <stdint.h>

#ifndef XPAR_PWM_AXI_0_BASEADDR
# define D4_PWM_BASE ((UINTPTR)0x44A00000u) /* XSA actual del Proyecto 4 */
#else
# define D4_PWM_BASE ((UINTPTR)XPAR_PWM_AXI_0_BASEADDR)
#endif

#define D4_PWM_CTRL       0x00u
#define D4_PWM_DUTY       0x04u
#define D4_PWM_DEADTIME   0x08u
#define D4_PWM_ENABLE     0x01u
#define D4_PWM_RUNNING    0x08u
#define D4_PWM_10KHZ      0u   /* freq_sel=0: 10 kHz */
#define D4_DEADTIME_CYCLES 100u /* 100 MHz => 1 us */
#define D4_ADC_TIMEOUT_POLLS 500u /* 100 ms sin muestra -> detener PWM */

static xadc_p4_t g_adc;
static volatile uint32_t g_poll_count = 0u;
static volatile uint32_t g_new_count = 0u;
static volatile uint32_t g_no_new_count = 0u;
static volatile uint32_t g_wrong_channel = 0u;
static volatile uint32_t g_last_code = 0u;
static volatile uint32_t g_fault = 0u;
static uint32_t g_no_new_streak = 0u;
static volatile bool g_started = false;
/* Esta marca solo describe la consulta XADC de la ISR mas reciente. */
static volatile bool g_codigo_fresco_esta_isr = false;

void d4_pwm_sync_stop(void)
{
    Xil_Out32(D4_PWM_BASE + D4_PWM_CTRL, 0u);
    Xil_Out32(D4_PWM_BASE + D4_PWM_DUTY, 0u);
    g_started = false;
    g_codigo_fresco_esta_isr = false;
}

bool d4_pwm_sync_escribir_duty_isr(int16_t duty_mil)
{
    uint32_t ctrl;
    if (!g_started || duty_mil < -850 || duty_mil > 850) {
        g_fault = 4u;
        d4_pwm_sync_stop();
        return false;
    }
    ctrl = Xil_In32(D4_PWM_BASE + D4_PWM_CTRL);
    if ((ctrl & D4_PWM_ENABLE) == 0u) {
        g_fault = 4u;
        d4_pwm_sync_stop();
        return false;
    }
    Xil_Out32(D4_PWM_BASE + D4_PWM_DUTY,
              (uint32_t)(uint16_t)duty_mil);
    if ((int16_t)(Xil_In32(D4_PWM_BASE + D4_PWM_DUTY) & 0xffffu)
        != duty_mil) {
        g_fault = 4u;
        d4_pwm_sync_stop();
        return false;
    }
    return true;
}

int d4_pwm_sync_start(void)
{
    int rc;
    d4_pwm_sync_stop();
    g_poll_count = g_new_count = g_no_new_count = 0u;
    g_wrong_channel = g_last_code = g_fault = g_no_new_streak = 0u;
    g_codigo_fresco_esta_isr = false;

    rc = xadc_p4_iniciar(&g_adc, xadc_p4_base_por_defecto());
    if (rc != XADC_P4_OK) {
        g_fault = 1u;
        return -1;
    }

    /* PWM apagado: establecer referencia neutra y 1 us por rama. */
    Xil_Out32(D4_PWM_BASE + D4_PWM_DUTY, 0u);
    Xil_Out32(D4_PWM_BASE + D4_PWM_DEADTIME, D4_DEADTIME_CYCLES);
    if (((Xil_In32(D4_PWM_BASE + D4_PWM_DUTY) & 0xffffu) != 0u) ||
        ((Xil_In32(D4_PWM_BASE + D4_PWM_DEADTIME) & 0xffffu) != D4_DEADTIME_CYCLES)) {
        g_fault = 2u;
        d4_pwm_sync_stop();
        return -2;
    }

    /* El PWM produce salidas digitales, pero no deben estar conectadas. */
    Xil_Out32(D4_PWM_BASE + D4_PWM_CTRL, D4_PWM_ENABLE | (D4_PWM_10KHZ << 1));
    if ((Xil_In32(D4_PWM_BASE + D4_PWM_CTRL) & D4_PWM_ENABLE) == 0u) {
        g_fault = 2u;
        d4_pwm_sync_stop();
        return -2;
    }
    g_started = true;
    return 0;
}

void d4_pwm_sync_poll_isr(void)
{
    uint16_t code = 0u;
    uint32_t before;
    bool has_new;
    g_codigo_fresco_esta_isr = false;
    if (!g_started) return;

    before = g_adc.canal_erroneo;
    has_new = xadc_p4_leer_si_hay_nueva(&g_adc, &code);
    ++g_poll_count;

    /* El driver recibido avisa canal incorrecto pero devuelve true:
     * rechazar explicitamente ese dato y no usarlo para el controlador. */
    if (g_adc.canal_erroneo != before) {
        ++g_wrong_channel;
        ++g_no_new_count;
        ++g_no_new_streak;
    } else if (has_new) {
        ++g_new_count;
        g_last_code = code;
        g_codigo_fresco_esta_isr = true;
        g_no_new_streak = 0u;
    } else {
        ++g_no_new_count;
        ++g_no_new_streak;
    }

    if (g_no_new_streak >= D4_ADC_TIMEOUT_POLLS) {
        g_fault = 3u;
        d4_pwm_sync_stop();
    }
}

bool d4_pwm_sync_codigo_fresco_isr(uint16_t *codigo)
{
    /* Solo llamar inmediatamente despues de d4_pwm_sync_poll_isr().
     * No se lee SR una segunda vez, asi no se pierde un EOC.
     * Un codigo fuera del rango del sensor se evaluara por separado.
     */
    if (codigo == 0 || !g_codigo_fresco_esta_isr) return false;
    *codigo = (uint16_t)g_last_code;
    return true;
}

void d4_pwm_sync_stats(d4_pwm_sync_stats_t *out)
{
    uint32_t control;
    if (out == 0) return;
    control = Xil_In32(D4_PWM_BASE + D4_PWM_CTRL);
    out->polls = g_poll_count;
    out->nuevas = g_new_count;
    out->sin_nueva = g_no_new_count;
    out->canal_erroneo = g_wrong_channel;
    out->ultimo_codigo = g_last_code;
    out->fallas = g_fault;
    out->running = (control & D4_PWM_RUNNING) ? 1u : 0u;
    out->enable = (control & D4_PWM_ENABLE) ? 1u : 0u;
}
