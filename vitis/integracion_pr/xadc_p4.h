/*
 * xadc_p4.h - Driver de adquisición de corriente con el XADC Wizard
 * Proyecto 4, EL3313 Taller de Diseño Digital - Rol D3
 *
 * Acceso directo a los registros AXI4-Lite del XADC Wizard v3.3 (PG091),
 * configurado en modo por eventos con el canal VAUX3 (vivado/xadc/xadc_p4.tcl).
 *
 * Uso típico en la ISR del Timer (D4):
 *
 *     uint16_t codigo;
 *     if (xadc_p4_leer_si_hay_nueva(&xadc, &codigo)) {
 *         int32_t i_ma = xadc_p4_codigo_a_ma(codigo, &cal);
 *         ... controlador PR ...
 *     } else {
 *         ... no hay muestra nueva: conservar la acción anterior ...
 *     }
 *
 * Por qué no se usa el driver XSysMon de AMD: la versión 8.0 de ese driver
 * define EOC en el bit 6 y BUSY en el bit 8 del registro SR, mientras que el
 * HDL del XADC Wizard v3.3 los pone en los bits 5 y 7. Este driver usa los
 * bits del HDL del IP, que son los del documento de diseño.
 */
#ifndef XADC_P4_H
#define XADC_P4_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Registros (offsets desde la dirección base del XADC Wizard)
 * ------------------------------------------------------------------------- */
#define XADC_P4_SRR       0x000u  /* Reinicio por software (no se usa)         */
#define XADC_P4_SR        0x004u  /* Estado; leerlo limpia EOC y EOS           */
#define XADC_P4_CONVSTR   0x00Cu  /* Bit 0: inicio de conversión por software  */
#define XADC_P4_GIER      0x05Cu  /* Habilitación global de interrupciones     */
#define XADC_P4_IPIER     0x068u  /* Interrupciones individuales               */
#define XADC_P4_VAUX3     0x24Cu  /* Resultado de VAUX3, código en bits [15:4] */
#define XADC_P4_CFR0      0x300u  /* Registro de configuración 40h             */
#define XADC_P4_CFR1      0x304u  /* Registro de configuración 41h             */
#define XADC_P4_CFR2      0x308u  /* Registro de configuración 42h             */

/* Bits del registro SR (HDL del XADC Wizard v3.3) */
#define XADC_P4_SR_CANAL  0x0000001Fu  /* [4:0] canal de la última conversión */
#define XADC_P4_SR_EOC    0x00000020u  /* 5: fin de conversión                */
#define XADC_P4_SR_EOS    0x00000040u  /* 6: fin de secuencia                 */
#define XADC_P4_SR_BUSY   0x00000080u  /* 7: conversión en curso              */

/* Configuración esperada */
#define XADC_P4_CANAL_VAUX3   19u          /* dirección DRP 13h               */
#define XADC_P4_CFR0_CANAL    0x001Fu
#define XADC_P4_CFR0_EC       0x0200u      /* 1 = modo por eventos            */
#define XADC_P4_CFR0_BU       0x0400u      /* 1 = bipolar (debe ser 0)        */
#define XADC_P4_CFR1_SEQ      0xF000u      /* 3 = canal único                 */
#define XADC_P4_CFR1_SEQ_UNICO 0x3000u

/* Escala del convertidor: 12 bits sobre 1 V (unipolar) */
#define XADC_P4_CODIGO_MAX    4095u
#define XADC_P4_CODIGOS       4096u

/* Dirección base. Si no se define antes de compilar, se toma de
 * xparameters.h en xadc_p4.c (ver XADC_P4_BASE_POR_DEFECTO). */
#define XADC_P4_BASE_DOC      0x44A10000u  /* dirección del documento de diseño */

/* ---------------------------------------------------------------------------
 * Códigos de retorno
 * ------------------------------------------------------------------------- */
#define XADC_P4_OK              0
#define XADC_P4_ERR_PARAM      -1  /* puntero nulo o dato inválido          */
#define XADC_P4_ERR_CONFIG     -2  /* el IP no tiene la configuración esperada */
#define XADC_P4_ERR_TIEMPO     -3  /* la conversión no terminó a tiempo     */

/* ---------------------------------------------------------------------------
 * Tipos
 * ------------------------------------------------------------------------- */
typedef struct {
    uintptr_t base;            /* dirección base del IP                    */
    uint16_t  ultimo_codigo;   /* último código válido leído (0 a 4095)    */
    uint32_t  muestras_nuevas; /* lecturas con EOC = 1                     */
    uint32_t  sin_muestra;     /* lecturas sin dato nuevo                  */
    uint32_t  canal_erroneo;   /* EOC con un canal distinto de VAUX3       */
} xadc_p4_t;

/*
 * Calibración lineal: i[mA] = (codigo - codigo_cero) * ma_por_codigo_q16 / 65536
 *
 * Valores nominales (documento de diseño, sección 5.8):
 *   K_s = 0,4 V/A, V_off = 0,5 V  ->  codigo_cero = 2048
 *   1 código = (1 V / 4096) / 0,4 V/A = 0,6103515625 mA = 40000 / 65536 mA
 */
typedef struct {
    int32_t codigo_cero;        /* código que corresponde a 0 A             */
    int32_t ma_por_codigo_q16;  /* mA por código, en formato Q16.16         */
} xadc_p4_calibracion_t;

#define XADC_P4_CAL_NOMINAL_CERO  2048
#define XADC_P4_CAL_NOMINAL_Q16   40000

/* ---------------------------------------------------------------------------
 * Funciones de acceso al hardware
 * ------------------------------------------------------------------------- */

/* Prepara el driver: deshabilita las interrupciones del IP, deja CONVSTR en 0,
 * descarta un EOC pendiente y verifica la configuración (VAUX3, eventos,
 * unipolar, canal único). Devuelve XADC_P4_OK o XADC_P4_ERR_CONFIG. */
int xadc_p4_iniciar(xadc_p4_t *x, uintptr_t base);

/* Base por defecto: la de xparameters.h si existe, o XADC_P4_BASE_DOC. */
uintptr_t xadc_p4_base_por_defecto(void);

/* Inicia una conversión por software (escribe 1 y luego 0 en CONVSTR).
 * Volver a 0 es obligatorio: CONVSTR y convst_in se combinan con una OR,
 * y si CONVSTR queda en 1 el XADC ya no ve los flancos de pwm_trigger_o. */
void xadc_p4_disparo_software(const xadc_p4_t *x);

/* Lee el registro SR. Ojo: la lectura limpia EOC y EOS. */
uint32_t xadc_p4_leer_estado(const xadc_p4_t *x);

/* Lee el código de VAUX3 sin consultar el estado (0 a 4095). */
uint16_t xadc_p4_leer_codigo(const xadc_p4_t *x);

/* Consulta SR una sola vez. Si EOC = 1, lee el código, lo guarda en *codigo
 * y devuelve true. Si no hay dato nuevo devuelve false y no toca *codigo.
 * Pensada para la ISR: nunca se queda esperando. */
bool xadc_p4_leer_si_hay_nueva(xadc_p4_t *x, uint16_t *codigo);

/* Disparo por software y espera activa del EOC, con un máximo de
 * 'intentos' lecturas de SR. Solo para pruebas y puesta en marcha. */
int xadc_p4_convertir_y_esperar(xadc_p4_t *x, uint16_t *codigo, uint32_t intentos);

/* ---------------------------------------------------------------------------
 * Conversiones (no tocan el hardware)
 * ------------------------------------------------------------------------- */

/* Extrae el código de 12 bits de la palabra del registro (bits [15:4]). */
static inline uint16_t xadc_p4_registro_a_codigo(uint32_t registro)
{
    return (uint16_t)((registro >> 4) & 0x0FFFu);
}

/* Código a microvoltios en la entrada VAUX3: codigo * 1e6 / 4096. */
static inline uint32_t xadc_p4_codigo_a_uv(uint16_t codigo)
{
    return ((uint32_t)codigo * 15625u) >> 6;   /* 1e6/4096 = 15625/64 */
}

/* Calibración nominal (K_s = 0,4 V/A, V_off = 0,5 V). */
void xadc_p4_calibracion_nominal(xadc_p4_calibracion_t *cal);

/* Calibración con dos puntos medidos: (codigo1, corriente1) y
 * (codigo2, corriente2), con las corrientes en mA medidas con un
 * instrumento de referencia. Devuelve XADC_P4_ERR_PARAM si los códigos
 * son iguales o la pendiente resulta cero. */
int xadc_p4_calibrar_dos_puntos(xadc_p4_calibracion_t *cal,
                                uint16_t codigo1, int32_t corriente1_ma,
                                uint16_t codigo2, int32_t corriente2_ma);

/* Código a corriente en mA, con enteros (apto para la ISR). */
int32_t xadc_p4_codigo_a_ma(uint16_t codigo, const xadc_p4_calibracion_t *cal);

/* Código a corriente en A, con punto flotante. Si el MicroBlaze V no tiene
 * FPU, esta versión es más lenta; en la ISR conviene usar la de enteros. */
float xadc_p4_codigo_a_amperios(uint16_t codigo, const xadc_p4_calibracion_t *cal);

#ifdef __cplusplus
}
#endif

#endif /* XADC_P4_H */
