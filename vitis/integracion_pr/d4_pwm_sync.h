#ifndef D4_PWM_SYNC_H
#define D4_PWM_SYNC_H

#include <stdint.h>
#include <stdbool.h>

/* Prueba sobre Nexys 4 Rev. B, SIN conexion a drivers, puente H o fuente de
 * potencia. En modo seguro el PR se calcula en sombra: no se cierra el lazo.
 */
typedef struct {
    uint32_t polls;            /* iteraciones de ISR en las que se consulto el XADC */
    uint32_t nuevas;           /* EOC de VAUX3 aceptados */
    uint32_t sin_nueva;        /* ISR sin EOC nuevo */
    uint32_t canal_erroneo;    /* EOC correspondiente a otro canal, rechazado */
    uint32_t ultimo_codigo;    /* solo diagnostico, NO convertir a amperios */
    uint32_t fallas;           /* 0=ninguna, 1=init, 2=AXI, 3=timeout, 4=duty */
    uint32_t running;          /* bit de estado del PWM */
    uint32_t enable;           /* bit de habilitacion del PWM */
} d4_pwm_sync_stats_t;

/* Debe invocarse una sola vez ANTES de iniciar el AXI Timer y habilitar IRQ.
 * Deja duty_ref=0, deadtime=100 ciclos, frecuencia=10kHz, habilita PWM.
 * Retorna 0 si pudo iniciarse, <0 si fallo y dejo el PWM apagado.
 */
int d4_pwm_sync_start(void);

/* UNA llamada no bloqueante por interrupcion de 200us. NO imprime ni espera. */
void d4_pwm_sync_poll_isr(void);

/* Recupera SOLO el codigo aceptado en la consulta de esta misma ISR.
 * No vuelve a leer el registro SR ni inicia otra conversion.
 * Si no hubo EOC o el canal era incorrecto, devuelve false.
 * Uso: llamar inmediatamente despues de d4_pwm_sync_poll_isr().
 */
bool d4_pwm_sync_codigo_fresco_isr(uint16_t *codigo);

/* Copia contadores aproximados, leibles desde main() sin bloquear la ISR.
 * Campos individuales atomicos de 32 bits; instantanea no totalmente atomica.
 */
void d4_pwm_sync_stats(d4_pwm_sync_stats_t *out);

/* Escribe DUTY solo con PWM iniciado y modulación en -850..850.
 * Retorna false, detiene PWM y registra falla si el hardware no acepta.
 * EXCLUSIVAMENTE para uso en ISR con salida PR autorizada. */
bool d4_pwm_sync_escribir_duty_isr(int16_t duty_mil);

/* Deshabilita PWM, pone duty=0 y no toca el Timer/PR. */
void d4_pwm_sync_stop(void);

#endif
