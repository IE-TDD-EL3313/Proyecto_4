# Verificación local de adquisición y firmware

## Actualización de integración del 16 de noviembre de 2026

La versión actual incorpora la arquitectura MicroBlaze V y PWM de `develop` (`98b0758`) y el arranque coordinado de adquisición/control. Los resultados de noviembre 10/11 que siguen se conservan como antecedentes; no representan síntesis ni ejecución física de esta versión ampliada.

| Verificación actual | Resultado |
|---|---|
| AXI/ADC y reset coordinado | 312 comprobaciones satisfactorias |
| Núcleo C y captura | 64508 comprobaciones satisfactorias |
| Runtime, precarga, muestra previa y fallas | 160 comprobaciones satisfactorias |
| Períodos exactos, fase y protocolo v2 | 44467 comprobaciones satisfactorias |
| PWM y contrato de armado | 77 comprobaciones satisfactorias |
| RTL conectado con ADC/Timer sintéticos | 106 comprobaciones; períodos 10000/6666/5000 |
| Publicaciones RTL reproducidas mediante runtime C | 127 comprobaciones; WARMUP/PRECHARGE/RUNNING |
| Receptor Python | 12 pruebas satisfactorias, incluyendo trama v2 producida por C |
| Objetos RV32IMC/ILP32 | Cinco objetos; suma de text: 7231 bytes, sin bibliotecas ni aplicación enlazada |
| Entrada SDT de placa | Compila contra cabeceras AMD con configuración de prueba explícita; no es un BSP/ELF real |
| BD integrado en Vivado 2026.1 | Validado y wrapper generado el 15 de noviembre; no se ejecutó síntesis/implementación de esta ampliación |

Los contadores de comprobaciones incluyen repeticiones de secuencias y no representan esa cantidad de casos independientes. La simulación utiliza respuestas ADC digitales, sin modelo analógico AMD ni procesador ejecutando firmware. El pin de LED0 de la Nexys 4 original se contrastó con el Master XDC incluido y se fija en T8.

Reproducción desde la raíz:

```powershell
python scripts/test_local.py
python scripts/cross_compile_core.py --toolchain-dir C:/AMDDesignTools/2026.1/Vitis/gnu/riscv/nt/bin
python scripts/check_bsp_api.py
vivado -mode batch -source scripts/build_nexys4.tcl -tclargs validate
```

El script de integración conecta el wrapper ADC a XADC DRP por evento en VAUX3 y dirige Timer al armado PWM. `project_path.txt` identifica la copia temporal del proyecto en `build/nexys4_integrated/`. Hay advertencias de conexión individual DRP/GPIO y dirección BRAM; su aceptación final requiere revisar síntesis, implementación y mapa de memoria. Los modos synth/bitstream están preparados para continuar ese trabajo.

El modo predeterminado de `vitis/bsp/main.c` es diagnóstico con potencia bloqueada. Siguen pendientes calibración/polaridad del sensor, coeficientes PR para 24 V y L=30 mH, máximo de modulación, actuación del interruptor manual, BSP/ELF, enlace de memoria y medidas físicas de tiempos/UART. El histórico actual conserva ADC y referencia; salida PR y modulación del Issue #15 no están incorporadas todavía.

## Antecedentes del 10 y 11 de noviembre

Fecha: 10 de noviembre de 2026. Alcance: RTL aislado, firmware portable, modelo RL sintético y receptor de capturas. No se utilizó una FPGA ni una etapa de potencia conectada.

Revalidación del 11 de noviembre de 2026: se ejecutaron nuevamente `test_local.py` y `cross_compile_core.py`, con los mismos resultados PASS y recuentos de la tabla. Las fuentes RTL actuales coinciden por SHA-256 con las copias utilizadas en la síntesis Nexys 4 del día 10; no se presenta esa síntesis como una nueva ejecución del día 11.

La verificación local respalda un avance de implementación con integración física pendiente. El enunciado indica la versión 2025.1, mientras el entorno utilizado es 2026.1; esa diferencia debe confirmarse con el profesor/equipo o resolverse mediante reproducción en la versión solicitada.

## Reproducción

```powershell
python scripts/test_local.py
python scripts/cross_compile_core.py --toolchain-dir C:/AMDDesignTools/2026.1/Vitis/gnu/riscv/nt/bin
& C:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat -mode batch -source scripts/verify_vivado.tcl -tclargs nexys4 all
```

Herramientas utilizadas: Icarus Verilog, GCC 13.2.0, Python 3.12.14, compilador RISC-V de Vitis GCC 13.4.0 y Vivado 2026.1. Los comandos se ejecutan desde la raíz del repositorio.

La placa confirmada es Nexys 4 original. El destino predeterminado es `nexys4`; para reproducir la comprobación previa de Basys 3 se sustituye por `basys3` en el comando Vivado.

## Resultados ejecutados

| Bloque | Resultado | Alcance |
|---|---|---|
| ADC/AXI, Icarus | PASS, 284 comprobaciones | Reset, lectura/conversión, trigger de cuatro ciclos, WSTRB, AW/W independientes, respuestas retenidas, cambio de modo, prioridad de errores, timeouts y wrap |
| Núcleo y captura C | PASS, 64508 comprobaciones | Referencia, cuantización, estados, modelo RL, bancos, fallo durante snapshot y serialización |
| Servicio periódico C | PASS, 70 comprobaciones | Reconocimiento temprano Timer, bloqueo C4 y detección de deadline antes/después de escribir |
| Receptor Python | PASS, 11 pruebas | Fragmentación, CRC, timeout, límites, calibración, duración, secuencia, CSV e interoperabilidad con C |
| Compilación RV32IMC | PASS, cinco objetos | C11, ILP32, software float, sin BSP ni enlace final |
| Síntesis ADC aislado | PASS, Vivado 2026.1 | Artix-7 xc7a35tcpg236-1; no sistema completo ni implementación física |
| Síntesis ADC para Nexys 4 | PASS, Vivado 2026.1 | Artix-7 xc7a100tcsg324-1; destino confirmado, sin implementación física |
| Creación Timer/UART/GPIO | PASS, Vivado 2026.1 | Parámetros leídos de vuelta; BD sin conexiones de plataforma ni validación integrada |

Los recuentos C/RTL incluyen aserciones repetidas durante las secuencias; no son casos independientes. El RTL se verificó contra un modelo digital DRP, sin el modelo analógico de AMD.

La captura completa emitida por C mide 8244 bytes y contiene 2048 registros. Python comprobó cabecera, CRC, secuencia inicial y calibración antes de generar CSV/JSON. La prueba intercala escrituras en el banco activo durante la transmisión fragmentada del banco congelado. Los dos arreglos de registros ocupan 16384 bytes, más metadatos y estados.

## Ensayo sintético RL

Condiciones: R=5 Ω, L=2.2 mH, Vdc=12 V, fs=20 kHz, un período de retardo y cuantización ADC de 1/1024 A por código alrededor de raw=2048. Se utilizan los coeficientes candidatos de la sección 15 del planteamiento. No se modelan conmutación, tiempo muerto ni pérdidas del puente.

| Referencia pico | Fundamental sintética | Error de amplitud aproximado |
|---:|---:|---:|
| 0.2 A | 0.191284 A | 4.358 % |
| 0.5 A | 0.478311 A | 4.338 % |
| 1.0 A | 0.956577 A | 4.342 % |

También se ejecutó cambio de R=5 a 6.5 Ω con referencia de 0.5 A, comprobando continuidad del procesamiento y ausencia de fallos del supervisor. No se atribuyen a ese caso mediciones de rizado ni de establecimiento experimental.

## Síntesis y compilación cruzada

La síntesis del wrapper ADC reportó 132 LUT y 258 FF, sin BRAM ni DSP. Con reloj de 10 ns, el reporte posterior a síntesis indica WNS=4.991 ns para las rutas analizadas. Las E/S no tienen presupuesto de integración y hay advertencia sobre la fuente de reloj OOC. No se ejecutó colocación/ruteo ni se acredita cierre temporal de la plataforma.

La comprobación adicional para Nexys 4 (`xc7a100tcsg324-1`) produjo 132 LUT, 258 FF, 0 BRAM y 0 DSP, con WNS posterior a síntesis de 5.139 ns para reloj de 10 ns. También se comprobaron los parámetros de Timer, UART y GPIO. Estos resultados corresponden a los mismos bloques aislados y no incluyen adquisición física, procesador ni PWM integrado.

Se generaron `xadc_synth.dcp` y `control_peripherals.tcl`. La comprobación de periféricos verifica cada parámetro mediante lectura de retorno. El BD aislado conserva la advertencia BD 5-339 porque faltan las conexiones de plataforma y `validate_bd_design`; no es una plataforma completa ni un XSA.

Los objetos RV32IMC suman 6327 bytes en la columna `text` del reporte `size`. Ese total no incluye las bibliotecas de software float, math, BSP, memoria de datos, stack ni aplicación final. En el objeto de captura no aparecen referencias indefinidas a helpers `__atomic` o `__sync`; el coste ISR completo permanece sin medir.

Las salidas regenerables se encuentran en `build/host/` y `build/riscv/`; las de Vivado se exportan a `build/vivado/nexys4/`, conservando las anteriores en `build/vivado/basys3/`. El directorio temporal de Vivado se imprime en la consola. Las rutas largas de OneDrive requieren generar el proyecto y checkpoint primero en esa ubicación temporal.

## Pendiente en hardware

Sensor y acondicionamiento para Nexys 4; canal XADC y constraints; IP XADC y ready/reset; Block Design/XSA; bloqueo y sincronización PWM C4; BSP e interrupciones; UART y GPIO físicos; enlace de memoria; WCET/jitter; simulación con modelo AMD; ensayos de un millón de períodos y mediciones en FPGA. La captura v1 conserva raw e iref; exportar también salida PR y modulación requiere revisar protocolo y memoria.
