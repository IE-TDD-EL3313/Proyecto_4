# Proyecto 4 — Control de corriente en un inversor monofásico

**Curso:** EL3313 — Taller de Diseño Digital  
**Institución:** Tecnológico de Costa Rica  
**Período:** II Semestre 2026

## Descripción

Este proyecto consiste en el diseño e implementación de un sistema
embebido basado en el procesador MicroBlaze V, utilizando una FPGA
como plataforma de procesamiento y control.

El sistema integra periféricos mediante interfaces AXI4-Lite,
incluyendo un controlador PWM personalizado desarrollado en
SystemVerilog, adquisición de datos mediante ADC/XADC,
temporización mediante interrupciones y comunicación UART.

El software embebido se desarrolla en C y ejecuta periódicamente
un algoritmo de control proporcional-resonante (PR), coordinando
la adquisición de corriente y la actualización de las señales PWM.

Este repositorio corresponde al desarrollo del componente digital
del proyecto para el curso EL3313.

## Objetivos

- Implementar un sistema embebido basado en MicroBlaze V.
- Integrar periféricos mediante interfaces AXI4-Lite.
- Diseñar un controlador PWM unipolar con tiempo muerto configurable.
- Implementar adquisición de corriente mediante ADC/XADC.
- Ejecutar una rutina periódica de control mediante interrupciones.
- Almacenar y transmitir muestras de corriente mediante UART.
- Verificar los módulos mediante simulaciones y pruebas funcionales.

## Herramientas

- AMD Vivado 2026.1
- AMD Vitis
- SystemVerilog
- Lenguaje C
- Git y GitHub

Dependencias para reproducir las pruebas en placa:

- Cable USB/JTAG de la Nexys 4 y sus reglas de acceso instaladas.
- `xsdb`, incluido con Vitis 2026.1.
- `picocom` o un monitor serie equivalente a 115200 bit/s, 8N1.
- Osciloscopio con sondas conectadas únicamente a las salidas lógicas durante
  la prueba sin potencia.

## Alcance final

Por motivos de tiempo, la entrega se cierra con la plataforma digital y sus
pruebas seguras. Se verificaron el sistema MicroBlaze V, el PWM, el acceso AXI,
la adquisición sincronizada, la comunicación UART y la implementación numérica
del PR. También se observaron físicamente las señales PWM con osciloscopio y la
etapa de potencia desconectada.

No se completó la calibración analógica del sensor ni el control de corriente
en lazo cerrado sobre el inversor energizado. El repositorio distingue los
resultados de simulación, las pruebas físicas y las funciones que quedaron
preparadas pero no demostradas.

## Estructura del repositorio

```text
Proyecto_4/
├── constraints/         # Restricciones de FPGA
├── docs/
│   ├── diseno/          # Documento de diseño
│   └── informe/         # Informe técnico
├── resultados/          # Evidencias y resultados
├── rtl/
│   ├── pwm_controller/  # PWM original documentado y verificado
│   ├── pwm_pr/          # PWM usado en la adaptación final a Nexys 4
│   └── integracion_pr/  # Guardia física de salidas para ensayos seguros
├── scripts/             # Scripts de automatización
├── tb/
│   └── pwm_controller/  # Testbenches del PWM
├── vitis/
│   ├── app/             # Aplicación embebida
│   ├── drivers/         # Drivers de periféricos
│   └── integracion_pr/  # PR, autocero, protecciones y captura integrada
└── vivado/              # Sistema MicroBlaze V
```

## Arquitectura general

El sistema está compuesto por los siguientes subsistemas:

1. MicroBlaze V como procesador principal.
2. Interconexión AXI4-Lite para comunicación con periféricos.
3. Controlador PWM personalizado en SystemVerilog.
4. Interfaz ADC/XADC para adquisición de corriente.
5. AXI Timer para generación de interrupciones periódicas.
6. AXI UART Lite para comunicación con la computadora.
7. AXI GPIO para entradas de usuario y señales de estado.

## Plataforma Nexys 4

La adaptación usa una **Nexys 4 Rev. B** con el dispositivo
`xc7a100tcsg324-1`, reloj de 100 MHz y XADC en el canal **VAUX3**.
El RTL del PWM ubicado en `rtl/pwm_pr/` es el mismo que se validó en
el proyecto funcional `Proyecto4-TallerDigital`; los cambios de tarjeta
se concentran en el Block Design, el canal XADC y los constraints.

Mapa principal:

| Recurso | Configuración Nexys 4 |
|---|---|
| FPGA | `xc7a100tcsg324-1` |
| Reloj | 100 MHz |
| XADC | VAUX3 |
| UART Lite | 115200 bit/s, 8N1 |
| PWM | `0x44A00000` |
| XADC Wizard | `0x44A10000` |
| AXI Timer | `0x41C00000` |
| AXI UART Lite | `0x40600000` |
| BRAM compartida | 128 KiB |

Los pines de botones, depuración y compuertas están documentados en
`vivado/constraints/control_pins_pr.xdc`.

## Reconstrucción en Vivado

Se requiere AMD Vivado 2026.1. Desde la raíz del repositorio:

```bash
vivado -mode batch -source scripts/build_nexys4_pr.tcl -tclargs validate
vivado -mode batch -source scripts/build_nexys4_pr.tcl -tclargs synth
vivado -mode batch -source scripts/build_nexys4_pr.tcl -tclargs bitstream
```

Los productos quedan en `build/nexys4_pr/`:

- `nexys4_control.bit`: bitstream para programar la FPGA.
- `nexys4_control.xsa`: plataforma con bitstream para Vitis.
- `nexys4_control_hw.xsa`: plataforma de hardware sin bitstream.
- `timing_impl.rpt`: verificación de timing posterior al enrutamiento.
- `integrated_bd.tcl`: Block Design reconstruible.

El script trabaja sobre una copia temporal y no modifica el proyecto base de
Vivado ni los archivos fuente.

## Firmware de control

El firmware adaptado está en `vitis/integracion_pr/`. Conserva el controlador
PR, la adquisición sincronizada, el autocero, la captura y la UART del proyecto
funcional. La diferencia de tarjeta está encapsulada en el driver XADC, que usa
VAUX3 en lugar de VAUX6.

El PR quedó implementado para 5 kHz y una referencia de 50 Hz, con amplitudes
admitidas de 0,2, 0,5 y 1,0 A pico. Sus coeficientes corresponden al regulador
no ideal discretizado por Tustin prewarped. La prueba independiente ejecutada
en MicroBlaze V completó 1000 iteraciones y 12 puntos de comparación con
resultado PASS. Esta comprobación valida la ecuación numérica, no la respuesta
del inversor.

La adaptación final usa `rtl/pwm_pr/`, cuyo registro de referencia representa
la modulación en milésimas. El firmware convierte la salida del PR de ±0,85 a
±850 antes de escribir el PWM. El árbol `rtl/pwm_controller/` conserva el
periférico original en Q1.15 y sus campañas de simulación; sus resultados no se
atribuyen al núcleo adaptado sin indicar la diferencia.

Por seguridad, la actuación del puente está bloqueada en dos niveles:

1. `D4_CONTROL_PERMITIR_PWM` y `D4_BANCO_PUENTE_VALIDADO` permanecen en cero.
2. El build `build_nexys4_pr.tcl` inserta `pwm_output_guard.sv`, que fuerza las
   cuatro compuertas a cero.

Para observar las cuatro formas de onda **solamente con osciloscopio y sin
drivers ni etapa de potencia**, se dispone de:

```bash
vivado -mode batch -source scripts/build_nexys4_pwm_test.tcl -tclargs bitstream
```

También puede programarse toda la prueba desde terminal:

```bash
/opt/AMD/2026.1/Vitis/bin/xsdb scripts/run_pwm_xadc_cli.tcl
```

La prueba integrada segura, con las compuertas físicas forzadas a cero, se
ejecuta con:

```bash
/opt/AMD/2026.1/Vitis/bin/xsdb scripts/run_control_safe_cli.tcl
```

Para observar la salida UART en Linux:

```bash
picocom -b 115200 /dev/ttyUSB1
```

El nombre del puerto puede cambiar. Debe comprobarse antes con
`ls /dev/ttyUSB* /dev/ttyACM*`.

No se debe sustituir la guardia ni habilitar el puente hasta comprobar con la
etapa desenergizada la polaridad, el tiempo muerto, el interlock, el sensor de
corriente y la desconexión por falla.

## Verificación

El proyecto contempla:

- Testbenches autoverificables del controlador PWM.
- Verificación de transacciones AXI4-Lite.
- Comprobación de señales PWM y tiempo muerto.
- Validación de la adquisición sincronizada de corriente.
- Verificación de la ejecución periódica del software.
- Pruebas de comunicación UART.
- Simulación post-implementación temporizada.

## Estado verificado

- Block Design validado para Nexys 4.
- Síntesis e implementación completadas.
- Timing cerrado a 100 MHz (WNS de implementación: 1,145 ns; peor holgura de
  *hold*: 0,010 ns en la compilación registrada).
- Bitstream y XSA generados en `build/nexys4_pr/`.
- RTL del PWM coincidente con el proyecto funcional.
- Prueba numérica del PR: 1000 muestras y 12 puntos, todos PASS.
- Diagnóstico continuo PWM-XADC: 300000 ISR y 300000 muestras nuevas, sin
  pérdidas, canal erróneo ni fallas.
- Dos salidas PWM complementarias documentadas con osciloscopio a 10,00 kHz,
  con máximos de 3,44 V y 3,36–3,40 V y la etapa de potencia desconectada.
- Ensayo integrado seguro: la protección rechazó el autocero porque VAUX3
  estaba cerca de 93 códigos, fuera del intervalo esperado de 1650 a 2000.
- Pendiente: acondicionamiento y calibración del sensor, validación cuantitativa
  del tiempo muerto en las fotografías y control energizado del puente H.

## Documentación

- `docs/diseno/`: documento de diseño del sistema.
- `docs/informe/`: informe técnico y resultados finales.
- `docs/informe/matriz_cierre.md`: comparación entre requisitos, evidencias y
  actividades pendientes.
- `resultados/control_pr/`: pruebas del PR y del arranque integrado seguro.
- `resultados/pwm_xadc/`: continuidad de adquisición durante 60 segundos.
- `resultados/pwm/fotos_osciloscopio/`: fotografías y condiciones del ensayo.

## Resultados principales

| Prueba | Resultado | Alcance |
|---|---|---|
| PWM RTL | 85/85 comprobaciones PASS | Simulación funcional |
| PWM post-implementación | 25/25 comprobaciones PASS | Simulación temporizada |
| PR discreto | 1000 muestras y 12 puntos PASS | Ejecución numérica en MicroBlaze V |
| PWM-XADC continuo | 300000/300000 muestras nuevas | FPGA, entrada sin calibrar como corriente |
| Osciloscopio | 10,00 kHz; máximos de 3,44 V y 3,36–3,40 V | Una pareja complementaria, sin potencia |
| Control energizado | No ejecutado | Sensor y etapa de potencia no validados |

## Control de versiones

El historial conserva ramas por arquitectura, RTL, adquisición, firmware,
simulación e integración. Entre las integraciones registradas están:

| PR | Rama / propósito |
|---:|---|
| #29 | `feat/pwm-rtl`: implementación del PWM personalizado |
| #30 | `integracion/adc-firmware`: adquisición y firmware |
| #31 | `test/pwm-tb-sync`: sincronización y testbench |
| #32 | `fix/integracion-vivado`: correcciones de integración |
| #33 | `fix/firmware-vitis`: firmware y linker script |
| #34 | `docs/informe-pwm`: informe y evidencias del PWM |
| #35 | `fix/issue-13-evidencia-axi-pwm`: corrección y prueba AXI en placa |

Los mensajes del historial identifican cambios de tipo `feat`, `fix`, `test`,
`docs` y `chore`. Las evidencias asociadas a los problemas encontrados se
conservan en `resultados/`, en particular la saturación del PWM, las capturas
UART y el diagnóstico PWM-XADC.
