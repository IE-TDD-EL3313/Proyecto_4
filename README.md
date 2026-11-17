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
