# Matriz de cierre de la implementación

Esta matriz compara la entrega con los requisitos del Proyecto 4. Los estados
no significan una calificación; identifican qué evidencia existe y qué quedó
fuera del alcance final.

| Requisito | Evidencia disponible | Estado final |
|---|---|---|
| MicroBlaze V a 100 MHz | XSA, bitstream, aplicaciones ejecutadas y reportes de timing | Demostrado |
| PWM personalizado AXI4-Lite | RTL, testbench, simulación, prueba AXI y observación física | Demostrado por partes |
| Tres frecuencias PWM | Simulación de 10, 15 y 20 kHz | Demostrado en simulación; físicamente se observó 10 kHz |
| Enable, running y saturación | Testbench y dos pruebas AXI en placa | Demostrado |
| PWM unipolar y salidas complementarias | Simulación temporizada y dos fotografías de osciloscopio | Demostrado cualitativamente en placa |
| Tiempo muerto | 1 us programado y medido en simulación temporizada | Falta lectura física trazable con cursores |
| Disparo PWM hacia XADC | Testbench y diagnóstico continuo de muestras | Demostrado digitalmente; falta correlación física del pulso |
| XADC sincronizado | 300000 muestras nuevas en 300000 consultas | Demostrado sin calibración analógica |
| Entrada de corriente calibrada | Autocero rechazó ~93 códigos frente al rango 1650-2000 | No demostrado; sensor/acondicionamiento pendiente |
| AXI Timer e ISR periódica | 19990-20010 ciclos alrededor de 20000 | Demostrado en ensayo seguro |
| PR discreto | Código C y prueba de 1000 muestras/12 puntos | Demostrado numéricamente |
| PR sobre la planta | Autocero impidió habilitar actuación | No demostrado |
| Referencias 0,2/0,5/1,0 A | Selección implementada en software | No demostradas físicamente |
| Almacenamiento y UART | Dos bancos de 2048 muestras recibidos por PC | Demostrado en placa |
| Botón de exportación | Captura completa solicitada mediante BTNC | Demostrado en placa |
| Timing de implementación | WNS +1,145 ns; WHS +0,010 ns | Demostrado para las restricciones definidas |
| Integración con puente H energizado | No se conectó por falta de calibración y tiempo | No demostrado |
| Ensayos de cambio de referencia y carga | No se energizó la planta | No realizados |
| Comparación PLECS-medición | No hay medición de corriente en lazo cerrado | No realizada |

## Archivos que todavía debe aportar el equipo

1. Las dos fotografías del osciloscopio, preferiblemente en resolución original.
2. Una nota por fotografía con escala horizontal, escala vertical, canales,
   puntos de prueba y, si existe, lectura de cursores.
3. Los parámetros finales de la etapa de potencia, carga RL y modelo PLECS si
   forman parte de la entrega conjunta con Procesamiento Electrónico de Potencia.

No se deben completar con valores supuestos las filas pendientes. La principal
fortaleza del cierre es que cada afirmación queda asociada a una evidencia y a
su alcance real.
