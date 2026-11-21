# Verificación AXI del periférico PWM — Issue #13

## 1. Objetivo

Verificar en la Nexys 4 Rev. B el acceso AXI4-Lite al periférico PWM
desde MicroBlaze V, incluyendo los valores iniciales, las operaciones
de lectura y escritura, la saturación de referencias y el reset.

## 2. Configuración

| Parámetro | Valor |
|---|---|
| Procesador | MicroBlaze V |
| Reloj del sistema | 100 MHz |
| Dirección base PWM | `0x44A10000` |
| Límite de referencia | ±31129 |
| Límite positivo | `0x7999` |
| Límite negativo | `0x8667` |

Firmware utilizado:
[`pwm_axi_test_main.c`](../../vitis/bsp/pwm_axi_test_main.c).

## 3. Verificación de registros

| Registro | Offset | Valor inicial observado |
|---|---|---|
| `CTRL_STATUS` | `0x00` | `0x00000000` |
| `DUTY_REF` | `0x04` | `0x00000000` |
| `DEADTIME` | `0x08` | `0x00000064` |
| `SYNC_PHASE` | `0x0C` | `0x000003E8` |
| `DUTY_LATCHED` | `0x10` | `0x00000000` |

También se comprobaron los campos reservados y de solo lectura
de `CTRL_STATUS`, las referencias positivas y negativas, y las
escrituras de `DEADTIME` y `SYNC_PHASE`.

## 4. Saturación de referencias

| Prueba | Resultado |
|---|---|
| Referencia positiva | `0x00001234` |
| Referencia negativa | `0x0000EDCC` |
| Saturación positiva | `0x00007999` |
| Saturación negativa | `0x00008667` |

Los límites observados corresponden a ±31129 en representación
de complemento a dos de 16 bits.

Durante la integración se corrigió la parametrización de
`DUTY_MAX` en el wrapper
[`pwm_controller_axi.sv`](../../rtl/pwm_controller/pwm_controller_axi.sv).

El parámetro pasó de `logic signed [15:0]` a `int`, con una
conversión explícita a 16 bits al conectarlo con `pwm_axi_regs`.

## 5. Diagnóstico de DUTY_LATCHED

Los registros iniciales mostraron:

    FAIL STOPPED DUTY_LATCHED got=00008667 expected=00000000

La prueba esperaba inicialmente cero con el PWM detenido.
Sin embargo, el firmware actual documenta que el registro
de sombra sigue la referencia `DUTY_REF` en esa condición,
con el bit `VALID` desactivado.

Después de escribir la referencia negativa saturada,
el valor esperado es `0x00008667`.

La prueba final utiliza esta expectativa y termina
correctamente.

Este ajuste de la prueba es distinto de la corrección
de parametrización de `DUTY_MAX`.

## 6. Resultado final

El registro UART final contiene dos ejecuciones completas
con resultado `PASS`.

En ambas ejecuciones se comprobaron los valores iniciales,
las lecturas y escrituras AXI y los límites de saturación.

El procedimiento incluyó una repetición mediante reset
físico del procesador.

## 7. Evidencias

- [Registro inicial](uart_pwm_axi_test.log).
- [Registro de repetición](uart_pwm_axi_test_retest.log).
- [Registro final: dos ejecuciones PASS](uart_pwm_axi_test_final.log).
- [Firmware de diagnóstico](../../vitis/bsp/pwm_axi_test_main.c).

## 8. Alcance y limitaciones

La verificación confirma las transacciones AXI4-Lite y el
comportamiento observado de los registros del periférico PWM.

No constituye una medición física de las señales PWM, el
tiempo muerto, los retardos de los drivers ni el desempeño
del controlador PR en lazo cerrado.

## 9. Mapa de memoria y trazabilidad de artefactos

## Rangos verificados en el XSA implementado

| Región | Rango inclusivo | Ruta |
| --- | --- | --- |
| BRAM local DLMB e ILMB, 128 KiB compartidos | `0x00000000–0x0001FFFF` | Datos e instrucciones |
| BRAM AXI | `0x10000000–0x10007FFF` | Instrucciones |
| AXI GPIO | `0x40000000–0x4000FFFF` | Datos |
| AXI UARTLite | `0x40600000–0x4060FFFF` | Datos |
| AXI INTC | `0x41200000–0x4120FFFF` | Datos |
| AXI Timer | `0x41C00000–0x41C0FFFF` | Datos |
| XADC | `0x44A00000–0x44A0FFFF` | Datos |
| PWM | `0x44A10000–0x44A1FFFF` | Datos |

En la prueba previa de adquisición en la misma plataforma se verificó una trama con 2048 muestras RAW consecutivas a 20 kHz y la exportación por BTNC. Esa prueba ejercitó ADC, Timer e interrupciones, UARTLite y GPIO. La prueba documentada aquí agrega transacciones explícitas a los registros PWM desde el procesador. [Evidencia de adquisición digital y exportación](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/6#issuecomment-7055549949)

## Identificación de artefactos

| Archivo | SHA-256 |
| --- | --- |
| `nexys4_control.bit` corregido | `db2766ef85d93f7b0bab780c1dccaff38b9f64311205feff1d1594e20262a898` |
| `pwm_axi_test.elf` | `e5f621a54637ba93bb108d1c32d68c083b587646280afe1723ed7b4a001b3903` |
| `uart_pwm_axi_test_final.log` original | `f5eda71bf6ef05a279d3d8de2bc666f10f8269e056ff92b9b8e0edb413356ee4` |

La copia de texto versionada elimina el byte NUL inicial
y normaliza los finales de línea a LF. El archivo original
se conserva sin cambios en el directorio de compilación.

### Evidencias adicionales del equipo

- [Captura UART final en formato TXT](uart_pwm_axi_test_final.txt).
- [Issue #13](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/13).
- [Mapa AXI y construcción del Block Design](../../scripts/build_nexys4.tcl).
