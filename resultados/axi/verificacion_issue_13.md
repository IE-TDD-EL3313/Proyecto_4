# Verificación AXI en Nexys 4 · Issue #13

Se ejecutó el firmware de prueba en MicroBlaze V con el bitstream integrado de 128 KiB y UARTLite a 115200 baudios, 8N1. Se obtuvieron **dos `PASS AXI_PWM #13` por UART**. Según el operador, entre ambas corridas se pulsó el botón físico **CPU RESET (C12)** y el programa volvió a ejecutarse desde BRAM, sin volver a descargar el ELF ni cargar el bitstream.

El firmware lee los valores de reset antes de su primera escritura. En ambas corridas se observaron `CTRL_STATUS=0`, `DUTY_REF=0`, `DEADTIME=100`, `SYNC_PHASE=1000` y `DUTY_LATCHED=0`. El segundo arranque confirmó que se restauraron esos valores después de modificar los registros. Las lecturas también confirmaron los campos de solo lectura y reservados de `CTRL_STATUS`, la escritura y lectura de referencias positivas y negativas, los límites `0x7999` y `0x8667` y el valor sombra detenido `0x00008667` con `VALID=0`. La prueba mantuvo `enable` y `gates_en` en cero.

El wrapper integrado del bitstream original pasaba `DUTY_MAX` como cadena. La corrección incluida en este PR declara el parámetro como `int` y lo convierte a 16 bits al pasarlo al banco de registros. En el wrapper corregido se verificó `.DUTY_MAX(31129)` y la secuencia AXI completa pasó en XSim antes del ensayo en placa.

- [Issue #13](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/13)
- [Captura UART final](uart_pwm_axi_test_final.txt)
- [Firmware de prueba](../../vitis/bsp/pwm_axi_test_main.c)
- [Corrección de DUTY_MAX](../../rtl/pwm_controller/pwm_controller_axi.sv)
- [Mapa AXI y construcción del Block Design](../../scripts/build_nexys4.tcl)

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

La copia de texto versionada quita el byte NUL inicial y normaliza los finales de línea a LF. El archivo original se conserva sin cambios en el directorio de compilación.
