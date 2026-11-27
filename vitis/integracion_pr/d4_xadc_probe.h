#ifndef D4_XADC_PROBE_H
#define D4_XADC_PROBE_H

#include <stdint.h>

/* Diagnostico no invasivo: SOLO lecturas AXI, no modifica el XADC/PWM. */
void d4_xadc_probe_print(void);
uint16_t d4_xadc_probe_last_raw12(void);
uint32_t d4_xadc_probe_raw12_to_mv(uint16_t raw12);

#endif
