# Planteamiento del diseño
## Proyecto 4 – Control de corriente de un inversor monofásico con un sistema embebido MicroBlaze V y periférico PWM personalizado

<!--
PLANTILLA DEL EQUIPO
- Cada integrante edita solo sus secciones, desde su propia rama creada a partir de develop.
- Responsables: Marvin = MicroBlaze V / Block Design / IP de Xilinx (issue #1)
                Keylor = periférico PWM y registros AXI4-Lite (issue #2)
                Luis Diego = ADC/XADC, firmware y temporización (issue #3)
- Las figuras van en docs/diseno/figuras/ con nombres descriptivos (ej. tercer_nivel_pwm.png).
- Reemplazar cada *(Redactar: ...)* por el contenido correspondiente.
-->

---

## 1. Introducción y comprensión del problema

*(Redactar — todos: el problema en 2–4 párrafos. Inversor monofásico en puente H que regula la corriente de una carga RL, con referencia sinusoidal de 50 Hz y amplitudes de 0,2, 0,5 y 1,0 A pico. Regulador PR discreto ejecutado en un MicroBlaze V. Ciclo del lazo: muestreo ADC → PR → actualización del PWM → buffer. Qué le corresponde a Taller y qué a PEDP.)*

---

## 2. Objetivos del proyecto

**Objetivo general:** *(Redactar)*

**Objetivos específicos:**

1. *(Redactar)*

---

## 3. Arquitectura general y subsistemas

*(Redactar: descripción general de la plataforma.)*

Los principales bloques funcionales considerados son:

- *(Completar)*

**Distribución hardware / software:**

- **Hardware (FPGA):** *(Completar)*
- **Software embebido (C):** *(Completar)*
- **Software externo (PC):** *(Completar)*
- **Etapa de potencia (PEDP):** *(Completar)*

---

## 4. Diagrama de primer nivel

*(Redactar: el sistema digital de la FPGA como un único bloque, con sus interfaces físicas.)*

**Entradas:**

| Señal | Descripción |
|---|---|
| | |

**Salidas:**

| Señal | Descripción |
|---|---|
| | |

![Diagrama de primer nivel del sistema](figuras/diagrama_primer_nivel.png)

---

## 5. Diagrama de segundo nivel

*(Redactar: división del sistema en sus bloques principales: MicroBlaze V y memoria, interconexión AXI, AXI Timer, controlador de interrupciones, PWM Controller, interfaz ADC/XADC, UART Lite y GPIO. Bloques externos: etapa de potencia, sensor y PC.)*

![Diagrama de segundo nivel del sistema](figuras/diagrama_segundo_nivel.png)

---

## 6. Diagramas de tercer nivel

*(Redactar: introducción breve al tercer nivel.)*

Cada apartado de este nivel incluye: tabla de **señales de entrada**, tabla de **señales de salida**, **señales internas relevantes**, el **diagrama** y la **explicación del bloque**.

### 6.1 Procesador MicroBlaze V, memoria e interconexión

*(Redactar — Marvin, issue #1)*

### 6.2 Periférico PWM personalizado (PWM Controller)

*(Redactar — Keylor, issue #2)*

### 6.3 Adquisición de corriente (interfaz ADC/XADC)

**Responsable: Luis Diego, [Issue #3](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/3). Estado: diseño propuesto; implementación pendiente.**

El XADC Wizard se encapsula en un periférico AXI4-Lite propio. Un disparo del PWM inicia la adquisición en operación; START_SW permite diagnóstico con el lazo detenido. Una FSM espera conversión y lectura DRP antes de publicar DATA, SEQ y TIME en un mismo flanco. MicroBlaze V captura esa muestra en la ISR del Timer y comprueba coherencia, secuencia y antigüedad.

| Interfaz | Dirección respecto del wrapper | Propósito |
|---|---|---|
| clk/rst_n | Entrada | Dominio común propuesto de 100 MHz y reset coordinado |
| pwm_trigger_o | Entrada desde Keylor | Disparo de adquisición |
| S_AXI | Entrada/salida con plataforma de Marvin | Configurar y leer registros de 32 bits |
| CONVST, DEN/DWE/DADDR, reset_xadc | Salida al XADC Wizard | Inicio, lectura DRP y recuperación |
| BUSY/EOC/DRDY/DO | Entrada desde XADC Wizard | Conversión y dato disponible |
| Canal analógico externo | Entrada al XADC | Corriente acondicionada por PEDP; canal/pines pendientes |

Las señales internas relevantes son req_accept, capture_en, raw12, sample_seq, sample_time y adc_ticks. [D3: módulos y relaciones](figuras/03_diagramas_adc_firmware.md#d3-nivel-3-módulos-de-adquisición) y [diseño §3](03_adquisicion_firmware_temporizacion.md#3-arquitectura-de-adquisición) contienen las tablas y justificación. [Lucid editable](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=n3adc) conserva la arquitectura conceptual inicial.

### 6.4 Periféricos de Xilinx: AXI Timer, UART Lite y GPIO

*(Redactar — Marvin, issue #1)*

---

## 7. Diagramas de cuarto nivel

*(Redactar: introducción breve al cuarto nivel.)*

Cada subbloque de este nivel sigue el mismo formato:

> **Objetivo** · **Entradas** (tabla) · **Salidas** (tabla) · **Diagrama** · **Relación con los demás módulos** · **Funcionamiento** · **Diseño y justificación técnica** · **Comportamiento durante el reset** · **Casos especiales y condiciones de borde** · **Estrategia de validación**

### 7.1 – 7.5 Subbloques del periférico PWM

*(Redactar — Keylor, issue #2)*

### 7.6 en adelante: subbloques de la interfaz ADC/XADC

Los subbloques propuestos son interfaz AXI, registros, árbitro de solicitudes, FSM/DRP, contador/watchdog y captura atómica. Sus objetivos, entradas, salidas, reset, condiciones de borde y pruebas se especifican en [diseño §3.4](03_adquisicion_firmware_temporizacion.md#34-contratos-por-subbloque-de-cuarto-nivel). El diagrama [D4: FSM de adquisición](figuras/03_diagramas_adc_firmware.md#d4-nivel-4-estados-de-adquisición) amplía la FSM de tercer nivel.

La solicitud se acepta solo si el XADC está disponible y ha terminado el asentamiento. EOC permite solicitar la lectura DRP; DRDY habilita la captura de DO[15:4]. Una solicitud adicional se rechaza sin reemplazar la operación activa. Un timeout bloquea nuevas solicitudes hasta reset coordinado: un EOC/DRDY tardío no puede publicarse como muestra nueva. Reset cancela la operación, limpia VALID y reinicia los registros; deshabilitar ENABLE impide nuevos disparos y permite terminar una operación ya aceptada.

Los flujos de software se ejecutan en MicroBlaze V: [D5: main](figuras/03_diagramas_adc_firmware.md#d5-nivel-3-estados-del-firmware-y-main), [D6: ISR](figuras/03_diagramas_adc_firmware.md#d6-nivel-4-isr-periódica), [D7: bancos y exportación](figuras/03_diagramas_adc_firmware.md#d7-nivel-4-almacenamiento-y-exportación) y [D8: ruta numérica](figuras/03_diagramas_adc_firmware.md#d8-nivel-4-ruta-numérica). No representan nuevos bloques RTL.

---

## 8. Mapa de memoria, registros e interfaces

### 8.1 Mapa de memoria

*(Completar — Marvin, con los valores del Address Editor de Vivado.)*

| Periférico | Dirección base | Rango | Interfaz |
|---|---|---|---|
| | | | |

### 8.2 Registros del periférico PWM

*(Completar — Keylor: offset, bits, acceso, valor de reset y descripción de cada campo.)*

### 8.3 Registros de la interfaz ADC/XADC

Offsets relativos a ADC_BASE, pendiente de asignación por Marvin. Todos los bits definidos reinician a cero. La semántica completa y la política AXI están en [diseño §4](03_adquisicion_firmware_temporizacion.md#4-interfaz-axi4-lite-propuesta); esta tabla es un resumen del contrato propuesto.

| Offset | Registro | Acceso | Campos y comportamiento |
|---|---|---|---|
| 0x00 | ADC_CTRL | R/W | bit0 ENABLE; bit1 SYNC_MODE; bit2 START_SW, pulso y lectura cero |
| 0x04 | ADC_STATUS | RO | bit0 VALID; bit1 BUSY; bit2 OVERRUN; bit3 TIMEOUT |
| 0x08 | ADC_DATA | RO | raw12 en [11:0], resto cero |
| 0x0C | ADC_SEQ | RO | Secuencia de publicación, módulo 2^32 |
| 0x10 | ADC_TIME | RO | Timestamp del disparo de la muestra publicada |
| 0x14 | ADC_ERR_CLEAR | WO | Bits 2/3 W1C; lectura cero; set domina clear |
| 0x18 | ADC_TICKS | RO | Contador vivo de 32 bits, un incremento por reloj |

La ISR lee SEQ1, DATA, TIME, STATUS, SEQ2 con un máximo de dos intentos. Solo acepta tuplas coherentes, sin errores y con secuencia esperada. VALID persiste; SEQ indica novedad. ADC_TIME se mantiene con la muestra mientras ADC_TICKS continúa avanzando. Limpiar TIMEOUT no rearma una FSM en ERROR.

### 8.4 Protocolo AXI4-Lite

*(Redactar: canales, handshake `VALID`/`READY`, respuestas y su uso entre el MicroBlaze V y los periféricos.)*

---

## 9. Software embebido y aplicación de PC

### 9.1 Flujo de la aplicación de control

El firmware distingue INIT, WARMUP, RUNNING y FAULT. Inicializa drivers, calibración, PR, bancos y sincronización con compuertas bloqueadas. WARMUP comprueba la primera muestra con espera acotada; RUNNING ejecuta el control desde la interrupción periódica; FAULT bloquea PWM y conserva diagnóstico hasta rearme explícito. Main filtra botones durante 20 ms sin espera bloqueante y atiende UART con trabajo acotado por llamada.

La ISR reconoce el Timer, obtiene una muestra coherente, convierte a amperios, calcula referencia/error/PR, limita y escribe DUTY_REF pendiente; finalmente registra el histórico y métricas. No realiza UART, printf ni esperas indefinidas. [Diseño §6 y pseudocódigo](03_adquisicion_firmware_temporizacion.md#6-arquitectura-del-firmware), [D5–D6](figuras/03_diagramas_adc_firmware.md).

### 9.2 Temporización del lazo de control

Se propone una muestra y una ISR por período PWM. El caso de dimensionamiento de 20 kHz da Ts=50 µs a 100 MHz; sigue pendiente de acuerdo con PEDP y de medir WCET. AXI Timer 0 opera descendente, Generate y auto-reload, con TLR0=4998 para ese ejemplo, usando Ts=(TLR0+2)·Tclk. Timer→AXI INTC→MicroBlaze V proporciona la interrupción.

Reloj y frecuencia compartidos no fijan fase. Se propone armar un arranque por el primer generateout0 y una entrada adicional sync_start_i al PWM: ambos requieren aprobación e implementación con Marvin y Keylor. En el ejemplo se dispara en fase 2500, se interrumpe en 3500 y se aplica la referencia en el valle 5000. Los márgenes de 10 µs para adquirir y 15 µs para atender/escribir son presupuestos, no mediciones. La portadora debe funcionar en WARMUP aunque las compuertas sigan bloqueadas. [Diseño §5](03_adquisicion_firmware_temporizacion.md#5-temporización-e-interrupciones), [D9](figuras/03_diagramas_adc_firmware.md#d9-arranque-en-fase-y-secuencia-de-actuación).

GPIO_ISR, ADC_TICKS y traza de generateout0/trigger permiten medir período, latencia, duración y jitter con/sin exportación. Se incluyen despachador y retorno en el costo total; ADC_TIME no sustituye al contador vivo.

### 9.3 Implementación del regulador PR

PEDP diseña y discretiza el PR; el firmware implementa su ecuación y coeficientes con unidades, Ts, estados iniciales y política de saturación acordados. La referencia es de 50 Hz con 0,2, 0,5 y 1,0 A pico; se propone LUT y acumulador de fase, preservando fase al cambiar amplitud. La corriente se obtiene como (raw−offset_code)·gain_A_per_code.

Float32 es la opción inicial, condicionada a medir tiempo y precisión sin suponer FPU. La salida se normaliza solo si PEDP entrega voltios, se limita a ±m_max y se cuantiza al formato que acuerde Keylor; Q1.15 es una propuesta. Se mantienen offsets PWM 0x00/0x04. No se inventan ganancias, Vdc, calibración ni anti-windup. [Diseño §7](03_adquisicion_firmware_temporizacion.md#7-procesamiento-y-formato-numérico), [D8](figuras/03_diagramas_adc_firmware.md#d8-nivel-4-ruta-numérica).

### 9.4 Buffer de muestras y exportación por UART

Se proponen dos bancos circulares de 2048 registros {raw:uint16, iref_mA:int16}: 16 KiB más metadatos. Al recibir la solicitud de botón, la ISR congela un banco e intercambia punteros en O(1); main transmite ese banco inmutable mientras el control escribe el otro. Una segunda pulsación durante TX indica ocupado.

UART v1 propone 115200 baudios, 8N1, little-endian, cabecera de 48 bytes, registros de 4 bytes y CRC32. Una captura completa mide 8244 bytes y tarda al menos 0,715625 s. El receptor Python previsto valida formato/CRC, aplica la calibración exportada y genera CSV/gráficas. A 20 kHz, 2048 registros abarcan 102,35 ms entre extremos; permiten seleccionar cinco períodos completos de 50 Hz. El rizado de conmutación se mide con instrumentación de PEDP. [Diseño §8](03_adquisicion_firmware_temporizacion.md#8-memoria-y-exportación-uart), [D7](figuras/03_diagramas_adc_firmware.md#d7-nivel-4-almacenamiento-y-exportación).

---

## 10. Estrategia de integración con la etapa de potencia

*(Redactar — todos: conexión del sistema digital con la etapa de PEDP y acuerdos pendientes.)*

| Tema | Propuesta actual | Responsable / con quién se acuerda | Estado |
|---|---|---|---|
| | | | |

---

## 11. Decisiones de diseño y justificación

- **Arquitectura del sistema:** *(Redactar — Marvin)*
- **Periférico PWM:** *(Redactar — Keylor)*
- **Adquisición:** XADC Wizard con wrapper propio separa conversión y bus; DATA/SEQ/TIME se publican juntos y SEQ detecta muestras perdidas. ADC_TICKS permite medir tiempo sin confundirlo con un timestamp congelado. El timeout bloquea hasta reset para descartar respuestas tardías.
- **Software:** ISR acotada para el control y main para UART/botones. Dos bancos evitan copiar el histórico dentro de la ISR. Float32, Q1.15 y 20 kHz son propuestas sujetas a medición y acuerdos [C1–C8](03_adquisicion_firmware_temporizacion.md#10-acuerdos-pendientes-antes-de-la-implementación-integrada).

---

## 12. Estrategia de validación y plan de implementación

### 12.1 Validación por bloque

*(Completar — cada uno agrega las pruebas de su bloque.)*

| Bloque | Prueba | Resultado esperado |
|---|---|---|
| | | |
| ADC/XADC y AXI, Luis Diego | Reset, solicitudes, DRP, backpressure, coherencia y timeout | A01–A19 del plan: solo publicar conversiones válidas; sin datos tardíos |
| Temporización, Luis Diego con Marvin/Keylor | Timer, fase, WCET, jitter y deadline con/sin UART | T01–T11: cumplir presupuestos documentados, sin pérdidas ni deriva |
| Firmware y captura, Luis Diego | Modelo PR de PEDP, bancos, trama/CRC y receptor | F01–F09 y U01–U13: unidades, orden temporal y exportación coherentes |

Las pruebas anteriores están **pendientes de implementación y ensayo**. El [plan de verificación y trazabilidad del Issue #3](03_verificacion_adc_firmware.md) define criterios y evidencia; [D10](figuras/03_diagramas_adc_firmware.md#d10-verificación-incremental) muestra la integración incremental. La revisión documental no equivale a PASS en FPGA.

### 12.2 Plan de implementación

| Hito | Contenido | Fecha |
|---|---|---|
| M1 — Diseño del sistema | Documento de diseño (issues #1–#3) | 05/11 (G1) · 06/11 (G2) |
| M2 — Implementación de hardware | Issues #4–#9 | *(Completar)* |
| M3 — Integración y validación | Issues #10–#15 | *(Completar)* |
| M4 — Entrega final | Issues #16–#18, informe y repositorio | 19/11 (G1) · 20/11 (G2) |

### 12.3 Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| | |

---

## 13. Distribución del trabajo

| Persona | Área principal | Issues |
|---|---|---|
| Marvin | *(Completar)* | |
| Keylor | *(Completar)* | |
| Luis Diego | Adquisición ADC/XADC, firmware, temporización y exportación UART | #3 (diseño); implementación en issues posteriores |
| Todos | Integración y revisión cruzada de pull requests | — |

---

## 14. Referencias

1. AMD, *MicroBlaze V Processor*, [En línea]. Disponible en: https://www.amd.com/en/products/software/adaptive-socs-and-fpgas/microblaze-v.html
2. AMD, *MicroBlaze V Embedded Design Tutorial (UG1711)*, [En línea]. Disponible en: https://docs.amd.com/r/en-US/ug1711-microblaze-v-embedded-design
3. AMD, *MicroBlaze Processor Reference Guide (UG984)*, [En línea]. Disponible en: https://docs.amd.com/r/en-US/ug984-vivado-microblaze-ref
4. AMD, *Vivado Design Suite User Guide: Creating and Packaging Custom IP (UG1118)*, [En línea]. Disponible en: https://docs.amd.com/r/en-US/ug1118-vivado-creating-packaging-custom-ip
5. AMD, *AXI Reference Guide (UG1037)*, [En línea]. Disponible en: https://docs.amd.com/v/u/en-US/ug1037-vivado-axi-reference-guide
6. AMD, *Vitis Unified Software Platform Documentation (UG1416)*, [En línea]. Disponible en: https://docs.amd.com/v/u/en-US/ug1416-vitis-documentation
