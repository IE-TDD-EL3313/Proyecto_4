# Diagramas de adquisición, firmware y temporización

**Responsable:** Luis Diego Álvarez · **Issue:** #3

[Volver al diseño](../03_adquisicion_firmware_temporizacion.md) · [Plan de verificación](../03_verificacion_adc_firmware.md)

Los diagramas siguientes son fuente Mermaid editable y se visualizan dentro de GitHub. La versión en [Lucid](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit) conserva las diez páginas conceptuales iniciales. Este archivo amplía ese borrador con el contrato de arranque, ADC_TICKS y la verificación; para implementar prevalecen las tablas y semántica de la sección principal.

Los bloques grises/externos del borrador Lucid corresponden a Marvin, Keylor o PEDP. Los flujos de software describen ejecución en la CPU, no nuevos módulos de hardware. Los niveles 1–4 son una organización de esta propuesta; el profesor debe validar el criterio de jerarquía del documento.

## Relación con las páginas editables de Lucid

Se leyeron las diez páginas existentes con el plugin Lucid el 4 de noviembre de 2026. No se modificó el documento compartido. Esta tabla evita confundir la numeración conceptual con la documentación ampliada del repositorio.

| Página Lucid | Contenido consultado | Correspondencia vigente |
|---|---|---|
| [01 · Contexto](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=n1) | Dependencias del subsistema | D1 |
| [02 · Arquitectura](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=n2) | Plataforma, ADC y software | D2 añade el arranque armado |
| [03 · Módulos ADC](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=n3adc) | Interfaz, FSM y captura | D3 explicita ADC_TICKS |
| [04 · FSM ADC](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=n4adc) | Conversión y DRP | D4 conserva ERROR hasta reset y no fija CONVST a un ciclo |
| [05 · Main](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=fwmain) | Inicialización y servicios de fondo | D5 explicita WARMUP/FAULT |
| [06 · ISR](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=isr) | Flujo periódico de control | D6 incluye STATUS y reserva de tiempo MMIO |
| [07 · Exportación](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=export) | Bancos A/B y UART | D7 y protocolo v1 del diseño §8 |
| [08 · Ruta numérica](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=numeric) | Calibración, PR y cuantización | D8; Q1.15 pendiente de acuerdo |
| [09 · Temporización](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=timing) | Orden de eventos y presupuestos | D9; se propone ADC_TICKS en lugar de depender de Timer 1 |
| [10 · Contratos](https://lucid.app/lucidchart/7cf6dd6b-5941-4a32-8457-00e8aec6a009/edit?page=contracts) | Registros hasta 0x14 y acuerdos | Diseño §4 añade 0x18 ADC_TICKS; §10 reúne C1–C8 |

**D10 es un diagrama adicional de verificación en el repositorio; no es la página 10 de Lucid.** Las diferencias anteriores necesitan revisión del equipo antes de actualizar el esquema compartido o implementar los puertos propuestos.

## D1. Nivel 1: contexto de la sección de Luis Diego

```mermaid
flowchart TD
    S["Sensor y acondicionamiento PEDP"] -->|"Entrada analógica"| LD["ADC/XADC, firmware y temporización"]
    MV["Plataforma de Marvin"] -->|"CPU, BRAM, AXI y Timer"| LD
    LD -->|"DUTY_REF / ENABLE"| PWM["PWM de Keylor"]
    PWM -->|"pwm_trigger_o"| LD
    B["Botones GPIO"] -->|"Eventos"| LD
    LD -->|"Captura UART"| PC["Receptor Python"]
    PP["PEDP: PR, Ts y calibración"] -->|"Contrato"| LD
```

## D2. Nivel 2: arquitectura de hardware y software

```mermaid
flowchart TD
    CPU["HW: MicroBlaze V de Marvin"] <-->|"Memoria local"| RAM["HW: BRAM con código, PR y bancos A/B"]
    CPU <-->|"MMIO"| AXI["Interconexión AXI"]
    AXI <-->|"AXI4-Lite"| ADC["Periférico ADC/XADC de Luis Diego"]
    AXI <-->|"AXI4-Lite"| PWM["Periférico PWM de Keylor"]
    AXI <-->|"AXI4-Lite"| TIMER["AXI Timer"]
    AXI <-->|"AXI4-Lite"| UI["AXI GPIO y UART Lite"]
    PWM -->|"pwm_trigger_o"| ADC
    IN["Corriente acondicionada"] --> ADC
    TIMER -->|"irq"| INTC["AXI Interrupt Controller"]
    INTC -->|"Interrupt"| CPU
    CPU -->|"Ejecuta"| ISR["SW: ISR de control"]
    CPU -->|"Ejecuta"| MAIN["SW: main con botones y UART"]
    TIMER -->|"generateout0"| ARM["Arranque armado por hardware"]
    ARM -->|"sync_start_i propuesto"| PWM
```

## D3. Nivel 3: módulos de adquisición

```mermaid
flowchart TD
    BUS["AXI4-Lite"] <--> IF["Interfaz AXI"]
    IF <--> REG["CTRL, STATUS, DATA, SEQ, TIME y TICKS"]
    REG -->|"start_sw / enable / mode"| ARB["Árbitro de solicitudes"]
    PWM["pwm_trigger_o"] --> ARB
    ARB -->|"req_accept"| FSM["FSM de adquisición"]
    ARB -->|"Solicitud rechazada"| ERR["Timeout y flags de error"]
    FSM -->|"CONVST / DEN / DADDR"| XA["XADC Wizard"]
    XA -->|"BUSY / EOC / DRDY"| FSM
    IN["Canal analógico"] --> XA
    XA -->|"DO[15:4]"| CAP["Captura coherente de muestra"]
    FSM -->|"capture_en"| CAP
    CLK["Contador libre de 32 bits"] -->|"Tiempo del disparo"| CAP
    CLK -->|"Valor actual"| REG
    CAP -->|"DATA / SEQ / TIME"| REG
    FSM --> ERR
    ERR -->|"OVERRUN / TIMEOUT"| REG
```

## D4. Nivel 4: estados de adquisición

```mermaid
stateDiagram-v2
    [*] --> REPOSO
    REPOSO --> DISPONIBILIDAD: solicitud
    DISPONIBILIDAD --> DISPARAR: libre y adquisición cumplida
    DISPONIBILIDAD --> RECHAZAR: no disponible
    RECHAZAR --> REPOSO: OVERRUN
    DISPARAR --> ESPERAR_EOC: CONVST emitido
    ESPERAR_EOC --> SOLICITAR_DRP: EOC
    SOLICITAR_DRP --> ESPERAR_DRDY: DEN emitido
    ESPERAR_DRDY --> PUBLICAR: DRDY
    PUBLICAR --> REPOSO: dato y secuencia publicados
    ESPERAR_EOC --> ERROR: timeout ADC
    ESPERAR_DRDY --> ERROR: timeout DRP
    ERROR --> ERROR: descartar eventos tardíos y bloquear solicitudes
    ERROR --> REPOSO: solo reset coordinado del wrapper y XADC
```

## D5. Nivel 3: estados del firmware y main

```mermaid
flowchart TD
    INIT["INIT: PWM bloqueado"] --> CFG["Preparar drivers, PR, calibración y bancos"]
    CFG --> AR["Armar sincronización e interrupciones"]
    AR --> W["WARMUP: esperar primera muestra válida"]
    W -->|"Configuración válida"| RUN["RUNNING: control desde ISR"]
    W -->|"Timeout o configuración inválida"| F
    RUN --> M["Main: sondear GPIO y filtrar botones"]
    M --> B{"¿Exportar y banco libre?"}
    B -->|"Sí"| REQ["Publicar snapshot_request"]
    B -->|"No"| TX["Servicio UART no bloqueante"]
    REQ --> TX
    TX --> M
    M -->|"Cambio de amplitud"| AMP["Publicar consigna para próxima muestra"]
    AMP --> TX
    RUN -->|"Falla de adquisición o tiempo"| F["FAULT: PWM deshabilitado"]
    F -->|"Rearme explícito; reset ADC si hubo timeout"| AR
```

## D6. Nivel 4: ISR periódica

```mermaid
flowchart TD
    E["Entrada ISR: GPIO=1 y leer ADC_TICKS"] --> A["Reconocer Timer"]
    A --> W{"¿WARMUP o FAULT?"}
    W -->|"Sí"| H["Atender estado; mantener potencia bloqueada"]
    W -->|"No"| R["SEQ1, DATA, TIME, STATUS, SEQ2"]
    R --> V{"¿Tupla coherente, secuencia esperada y límites válidos?"}
    V -->|"No"| F["FAULT: deshabilitar PWM y congelar PR"]
    V -->|"Sí"| C["Convertir a amperios y evaluar iref al muestreo"]
    C --> P["Ejecutar ecuación PR de PEDP"]
    P --> Q["Saturar y convertir al formato PWM"]
    Q --> D{"¿Cálculo finito y margen hasta latch?"}
    D -->|"No"| F
    D -->|"Sí"| O["Escribir DUTY_REF y completar MMIO"]
    O --> POST{"¿Escritura completada dentro del deadline?"}
    POST -->|"No"| F
    POST -->|"Sí"| B["Registrar muestra e intercambiar banco si procede"]
    B --> Z["Métricas, GPIO=0 y retorno"]
    F --> Z
    H --> Z
```

## D7. Nivel 4: almacenamiento y exportación

```mermaid
flowchart TD
    ISR["ISR productora"] --> ACT["Banco activo A o B"]
    BTN["Main: evento exportar"] --> REQ["snapshot_request"]
    REQ --> SW["ISR: intercambio O(1)"]
    ACT --> SW
    SW -->|"Continúa escritura"| NEXT["Otro banco activo"]
    SW -->|"snapshot_ready"| FROZEN["Banco congelado e inmutable"]
    FROZEN --> HEAD["Main: cabecera de 48 bytes"]
    HEAD --> DATA["Registros desde el más antiguo"]
    DATA --> CRC["CRC32; liberar banco"]
    CRC --> PC["PC: validar y generar CSV"]
    BUSY["Nueva pulsación durante TX"] --> REJECT["Indicar ocupado; no congelar otro banco"]
```

## D8. Nivel 4: ruta numérica

```mermaid
flowchart TD
    RAW["raw12"] --> CAL["i=(raw-offset)·ganancia"]
    PH["Fase de muestreo y amplitud"] --> REF["iref=Ip·sin(fase)"]
    CAL --> ERR["e=iref-i"]
    REF --> ERR
    ERR --> PR["PR discreto: ecuación y coeficientes PEDP"]
    PR --> U["Salida en V o m, según contrato"]
    U --> SAT["Normalizar si procede y limitar a ±m_max"]
    SAT --> Q["Cuantizar a Q1.15 propuesto"]
    Q --> PWM["DUTY_REF pendiente; latch en valle"]
```

## D9. Arranque en fase y secuencia de actuación

```mermaid
flowchart TD
    ARM["Armar con potencia bloqueada"] --> G["Primer generateout0 del Timer"]
    G --> LOAD["sync_start_i: cargar fase PWM propuesta"]
    G --> IRQ0["Primera IRQ: WARMUP"]
    LOAD --> CLK["Portadora en reloj común; sin reinicios sucesivos"]
    CLK --> S["Pico: aceptar trigger y marcar TIME"]
    S --> ADC["Conversión y lectura DRP"]
    ADC --> PUB["Publicar DATA / SEQ / TIME"]
    PUB --> IRQ["IRQ de control en fase acordada"]
    IRQ --> ISR["Leer, calcular y escribir pendiente"]
    ISR --> LATCH["Siguiente valle: aplicar referencia"]
    LATCH --> S
```

## D10. Verificación incremental

```mermaid
flowchart TD
    A["Revisar contratos y parámetros"] --> B["Testbench autoverificable ADC + AXI"]
    B --> C["Pruebas firmware con datos sintéticos"]
    C --> D["Integrar trigger, Timer y PWM sin potencia"]
    D --> E{"¿Coherencia y presupuesto temporal cumplen?"}
    E -->|"No"| FIX["Corregir fase, formato o costo de ejecución"]
    FIX --> B
    E -->|"Sí"| F["Calibrar y registrar evidencia"]
    F --> G["Ensayos integrados con PEDP"]
    G --> H["Revisión cruzada del PR hacia develop"]
```

## Convenciones y condiciones de las FSM

- Reset domina la FSM ADC y borra VALID. Las esperas EOC y DRDY permanecen en su estado hasta evento o timeout.
- ERROR mantiene bloqueo aunque se limpie el flag TIMEOUT. Solo reset coordinado permite nuevas solicitudes; DRDY/EOC tardíos se descartan.
- DEN dura un DCLK; CONVST dura N_convst ciclos conforme al dispositivo. Se espera asentamiento y DRDY bajo antes de admitir nuevas operaciones.
- Una solicitud extra durante adquisición activa OVERRUN y no reemplaza la solicitud aceptada.
- El retardo de CONVST respecto del trigger se mide. El timestamp conserva el tiempo del disparo aceptado; se documenta su relación con el muestreo físico del XADC.
- WARMUP establece secuencia y fase; FAULT no ejecuta el PR. El estado UART es independiente del estado de control.
- Las salidas PWM siguen bloqueadas hasta que existan muestra válida, sincronización y control habilitado.
- La decisión de presupuesto temporal incluye latencia de IRQ, cálculo, MMIO y margen de establecimiento.
- El reconocimiento temprano del Timer corresponde al adaptador ISR propio propuesto. El handler genérico AMD limpia después de su callback y requiere adaptar el flujo si se utiliza.
- El enlace de arranque sync_start_i es una interfaz propuesta que requiere aceptación de Keylor y Marvin.

## Secuencia temporal del caso propuesto a 20 kHz

| Evento | Fase relativa al valle | Cota o acción |
|---|---:|---|
| Valle de portadora | 0 ciclos | Aplicación de referencia previa |
| Pico y disparo | 2500 ciclos = 25 µs | Una muestra |
| Publicación ADC | Después del disparo | Antes de IRQ con margen |
| IRQ Timer | 3500 ciclos = 35 µs | Leer muestra ya publicada |
| Escritura pendiente | Antes del próximo valle | Validar deadline |
| Nuevo valle | 5000 ciclos = 50 µs | Aplicar la referencia calculada |

La separación muestra–IRQ es 10 µs; IRQ–latch es 15 µs. Ambos son presupuestos propuestos, no resultados experimentales. Si los ensayos no cumplen, se modifica la configuración y se coordina de nuevo con PEDP.

## Detalle de tiempo y coherencia

ADC_TIME representa la muestra publicada. ADC_TICKS es un registro adicional RO propuesto para leer el contador RTL vivo y medir entradas/salidas ISR con diferencias módulo 2^32. Son lecturas de 32 bits; DATA, TIME y SEQ se publican juntos. Las comprobaciones de SEQ1/SEQ2 detectan una publicación intercalada con la lectura del firmware.
