/*
 * xadc_p4.c - Driver de adquisición de corriente con el XADC Wizard
 * Proyecto 4, EL3313 Taller de Diseño Digital - Rol D3
 *
 * Ver xadc_p4.h para la descripción de cada función.
 */
#include "xadc_p4.h"

#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Acceso a registros
 *   En la tarjeta: Xil_In32 / Xil_Out32 del BSP de Vitis.
 *   En la PC (pruebas): funciones que simulan el IP, ver pruebas_pc/.
 * ------------------------------------------------------------------------- */
#ifdef XADC_P4_PRUEBA_PC
uint32_t xadc_p4_pc_leer(uintptr_t direccion);
void     xadc_p4_pc_escribir(uintptr_t direccion, uint32_t dato);
#define LEER(base, off)          xadc_p4_pc_leer((base) + (off))
#define ESCRIBIR(base, off, v)   xadc_p4_pc_escribir((base) + (off), (v))
#else
#include "xil_io.h"
#include "xparameters.h"
#define LEER(base, off)          Xil_In32((UINTPTR)((base) + (off)))
#define ESCRIBIR(base, off, v)   Xil_Out32((UINTPTR)((base) + (off)), (u32)(v))
#endif

/* Dirección base: la que define el usuario, la de xparameters.h o la del
 * documento de diseño, en ese orden. */
#if defined(XADC_P4_BASE)
#define XADC_P4_BASE_POR_DEFECTO  ((uintptr_t)(XADC_P4_BASE))
#elif defined(XPAR_XADC_WIZ_0_BASEADDR)
#define XADC_P4_BASE_POR_DEFECTO  ((uintptr_t)(XPAR_XADC_WIZ_0_BASEADDR))
#else
#define XADC_P4_BASE_POR_DEFECTO  ((uintptr_t)(XADC_P4_BASE_DOC))
#endif

/* Límite de la pendiente para que (codigo - cero) * pendiente quepa en 32 bits:
 * |codigo - cero| <= 4095 + 4095, y 8190 * 262143 < 2^31. */
#define XADC_P4_Q16_MAX  262143

uintptr_t xadc_p4_base_por_defecto(void)
{
    return XADC_P4_BASE_POR_DEFECTO;
}

int xadc_p4_iniciar(xadc_p4_t *x, uintptr_t base)
{
    if (x == NULL || base == 0u) {
        return XADC_P4_ERR_PARAM;
    }

    x->base = base;
    x->ultimo_codigo = 0u;
    x->muestras_nuevas = 0u;
    x->sin_muestra = 0u;
    x->canal_erroneo = 0u;

    /* Sin interrupciones del XADC: el ritmo lo marca el AXI Timer. */
    ESCRIBIR(base, XADC_P4_GIER, 0u);
    ESCRIBIR(base, XADC_P4_IPIER, 0u);

    /* CONVSTR en 0 para que solo convst_in (pwm_trigger_o) inicie conversiones. */
    ESCRIBIR(base, XADC_P4_CONVSTR, 0u);

    /* Leer SR descarta un EOC que haya quedado de antes. */
    (void)LEER(base, XADC_P4_SR);

    /* Verificación de la configuración generada por el XADC Wizard. */
    uint32_t cfr0 = LEER(base, XADC_P4_CFR0);
    uint32_t cfr1 = LEER(base, XADC_P4_CFR1);

    bool canal_ok    = (cfr0 & XADC_P4_CFR0_CANAL) == XADC_P4_CANAL_VAUX3;
    bool eventos_ok  = (cfr0 & XADC_P4_CFR0_EC) != 0u;
    bool unipolar_ok = (cfr0 & XADC_P4_CFR0_BU) == 0u;
    bool unico_ok    = (cfr1 & XADC_P4_CFR1_SEQ) == XADC_P4_CFR1_SEQ_UNICO;

    if (!(canal_ok && eventos_ok && unipolar_ok && unico_ok)) {
        return XADC_P4_ERR_CONFIG;
    }
    return XADC_P4_OK;
}

void xadc_p4_disparo_software(const xadc_p4_t *x)
{
    ESCRIBIR(x->base, XADC_P4_CONVSTR, 1u);
    ESCRIBIR(x->base, XADC_P4_CONVSTR, 0u);
}

uint32_t xadc_p4_leer_estado(const xadc_p4_t *x)
{
    return LEER(x->base, XADC_P4_SR);
}

uint16_t xadc_p4_leer_codigo(const xadc_p4_t *x)
{
    return xadc_p4_registro_a_codigo(LEER(x->base, XADC_P4_VAUX3));
}

bool xadc_p4_leer_si_hay_nueva(xadc_p4_t *x, uint16_t *codigo)
{
    uint32_t sr = LEER(x->base, XADC_P4_SR);   /* limpia EOC */

    if ((sr & XADC_P4_SR_EOC) == 0u) {
        x->sin_muestra++;
        return false;
    }
    if ((sr & XADC_P4_SR_CANAL) != XADC_P4_CANAL_VAUX3) {
        x->canal_erroneo++;
    }

    uint16_t c = xadc_p4_leer_codigo(x);
    x->ultimo_codigo = c;
    x->muestras_nuevas++;
    if (codigo != NULL) {
        *codigo = c;
    }
    return true;
}

int xadc_p4_convertir_y_esperar(xadc_p4_t *x, uint16_t *codigo, uint32_t intentos)
{
    if (x == NULL || codigo == NULL) {
        return XADC_P4_ERR_PARAM;
    }

    (void)LEER(x->base, XADC_P4_SR);   /* descartar un EOC anterior */
    xadc_p4_disparo_software(x);

    for (uint32_t i = 0; i < intentos; i++) {
        if (xadc_p4_leer_si_hay_nueva(x, codigo)) {
            return XADC_P4_OK;
        }
    }
    return XADC_P4_ERR_TIEMPO;
}

/* ---------------------------------------------------------------------------
 * Conversiones
 * ------------------------------------------------------------------------- */

/* División entera de 64 bits redondeada al entero más cercano. */
static int64_t dividir_redondeando(int64_t num, int64_t den)
{
    if (den < 0) {
        num = -num;
        den = -den;
    }
    if (num >= 0) {
        return (num + den / 2) / den;
    }
    return -((-num + den / 2) / den);
}

void xadc_p4_calibracion_nominal(xadc_p4_calibracion_t *cal)
{
    if (cal == NULL) {
        return;
    }
    cal->codigo_cero = XADC_P4_CAL_NOMINAL_CERO;
    cal->ma_por_codigo_q16 = XADC_P4_CAL_NOMINAL_Q16;
}

int xadc_p4_calibrar_dos_puntos(xadc_p4_calibracion_t *cal,
                                uint16_t codigo1, int32_t corriente1_ma,
                                uint16_t codigo2, int32_t corriente2_ma)
{
    if (cal == NULL || codigo1 == codigo2 ||
        codigo1 > XADC_P4_CODIGO_MAX || codigo2 > XADC_P4_CODIGO_MAX) {
        return XADC_P4_ERR_PARAM;
    }

    int64_t di = (int64_t)corriente2_ma - (int64_t)corriente1_ma;
    int64_t dc = (int64_t)codigo2 - (int64_t)codigo1;
    int64_t q16 = dividir_redondeando(di * 65536, dc);

    if (q16 == 0 || q16 > XADC_P4_Q16_MAX || q16 < -XADC_P4_Q16_MAX) {
        return XADC_P4_ERR_PARAM;
    }

    /* codigo_cero = codigo1 - corriente1 / pendiente */
    int64_t cero = (int64_t)codigo1 -
                   dividir_redondeando((int64_t)corriente1_ma * 65536, q16);
    if (cero < -(int64_t)XADC_P4_CODIGOS || cero > 2 * (int64_t)XADC_P4_CODIGOS) {
        return XADC_P4_ERR_PARAM;
    }

    cal->ma_por_codigo_q16 = (int32_t)q16;
    cal->codigo_cero = (int32_t)cero;
    return XADC_P4_OK;
}

int32_t xadc_p4_codigo_a_ma(uint16_t codigo, const xadc_p4_calibracion_t *cal)
{
    int32_t d = (int32_t)codigo - cal->codigo_cero;
    int32_t p = d * cal->ma_por_codigo_q16;          /* Q16.16 */
    /* Redondeo al mA más cercano; en GCC el >> de un negativo es aritmético. */
    return (p + 32768) >> 16;
}

float xadc_p4_codigo_a_amperios(uint16_t codigo, const xadc_p4_calibracion_t *cal)
{
    float d = (float)((int32_t)codigo - cal->codigo_cero);
    return d * ((float)cal->ma_por_codigo_q16 / 65536.0f) / 1000.0f;
}
