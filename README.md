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

## Plataforma seleccionada

La placa del proyecto es [Digilent Nexys 4](https://digilent.com/reference/_media/reference/programmable-logic/nexys-4/nexys4_rm.pdf). El perfil de verificación
utiliza `xc7a100tcsg324-1` y reloj AXI de 100 MHz. La variante seleccionada
es Nexys 4 original; el pinout y la memoria de una Nexys 4 DDR o Nexys A7
deben tratarse con sus archivos de plataforma correspondientes.

Para verificar adquisición y crear los periféricos aislados en Vivado:

```powershell
& C:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat -mode batch -source scripts/verify_vivado.tcl -tclargs nexys4 all
```

Los resultados se guardan en `build/vivado/nexys4/`. El script también
selecciona Nexys 4 al omitir el argumento de destino. La plataforma completa,
las direcciones y las restricciones de pines se integran con el Block Design.

## Estructura del repositorio

```text
Proyecto_4/
├── constraints/         # Restricciones de FPGA
├── docs/
│   ├── diseno/          # Documento de diseño
│   └── informe/         # Informe técnico
├── resultados/          # Evidencias y resultados
├── rtl/
│   └── pwm_controller/  # Periférico PWM personalizado
├── scripts/             # Scripts de automatización
├── tb/
│   └── pwm_controller/  # Testbenches del PWM
├── vitis/
│   ├── app/             # Aplicación embebida
│   └── drivers/         # Drivers de periféricos
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

## Instalación y compilación

Las instrucciones de reconstrucción del proyecto en Vivado
y compilación del software en Vitis se documentarán conforme
se complete la integración del sistema.

El subsistema de adquisición y el núcleo portable de firmware pueden
verificarse sin FPGA. Desde la raíz del repositorio, con Python 3.10 o
superior, GCC e Icarus Verilog disponibles en PATH:

```powershell
python scripts/test_local.py
```

La prueba genera una captura **sintética**, la transmite desde C y la
decodifica en Python. Los datos y ejecutables quedan en `build/host/`.
Las instrucciones de integración y el alcance implementado están en
la sección 15 de [planteamiento.md](docs/diseno/planteamiento.md#15-implementación-de-adquisición-y-firmware).
Los resultados ejecutados se registran en
[verificacion_local.md](resultados/adc_firmware/verificacion_local.md).

La [sección 16 del planteamiento](docs/diseno/planteamiento.md#16-explicación-del-código-y-preparación-de-la-defensa)
explica cada archivo, las funciones y un guion de defensa. La
[sección 17](docs/diseno/planteamiento.md#17-revisión-de-requisitos-y-alcance-de-la-entrega)
contrasta el avance con el enunciado y los criterios de los issues.

Para decodificar una captura binaria guardada:

```powershell
python scripts/capture_receiver.py --input captura.bin --output-dir resultados/capturas
```

El receptor también admite `--port COM5` a 115200, 8N1. Ese modo requiere
`pyserial` y firmware integrado en una plataforma real; el modo archivo
solo usa la biblioteca estándar de Python.

## Verificación

El proyecto contempla:

- Testbenches autoverificables del controlador PWM.
- Verificación de transacciones AXI4-Lite.
- Comprobación de señales PWM y tiempo muerto.
- Validación de la adquisición sincronizada de corriente.
- Verificación de la ejecución periódica del software.
- Pruebas de comunicación UART.
- Simulación post-implementación temporizada.

## Estado del proyecto

**En desarrollo.**

## Documentación

- `docs/diseno/`: documento de diseño del sistema.
- `docs/informe/`: informe técnico y resultados finales.

## Control de versiones

El desarrollo se realiza mediante Git, utilizando ramas por
funcionalidad, issues para seguimiento de tareas y pull requests
para revisión e integración de cambios.
