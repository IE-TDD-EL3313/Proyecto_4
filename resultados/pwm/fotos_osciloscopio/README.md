# Evidencia física del PWM

Esta carpeta contiene las dos fotografías tomadas durante la prueba en la
Nexys 4:

- `pwm_complementarias.jpg`: canales complementarios de una misma rama.
- `pwm_dos_salidas.jpg`: segunda toma de las mismas señales, con las mediciones
  automáticas visibles.

Condiciones documentadas del ensayo:

- Placa: Nexys 4 Rev. B (`xc7a100tcsg324-1`).
- Reloj del sistema: 100 MHz.
- Frecuencia PWM seleccionada: 10 kHz.
- Tiempo muerto programado: 100 ciclos, equivalente a 1 us.
- Puntos de prueba: dos salidas complementarias del conector JA.
- Referencia del osciloscopio: GND del conector JA.
- Etapa de potencia y drivers: desconectados.
- Programa: diagnóstico `pwm_xadc_continuo`.
- Bitstream: `build/nexys4_pwm_test/nexys4_control.bit`.

Lecturas visibles en el instrumento:

| Medición | Fotografía 1 | Fotografía 2 |
|---|---:|---:|
| Frecuencia CH1 | 10,00 kHz | 10,00 kHz |
| Frecuencia CH2 | 10,00 kHz | 10,00 kHz |
| Máximo CH1 | 3,44 V | 3,44 V |
| Máximo CH2 | 3,36 V | 3,40 V |
| Escala horizontal | 50 us/div | 50 us/div |
| Escala vertical | 1 V/div | 1 V/div |

Las fotografías demuestran presencia, frecuencia, nivel lógico y
complementariedad visible de dos señales. No muestran cursores con resolución
suficiente para medir el tiempo muerto de 1 us; ese valor se reporta como
configurado y verificado en simulación, no como medición física.
