# Verificación y trazabilidad: adquisición, firmware y temporización

**Responsable:** Luis Diego Álvarez

**Referencia:** [Issue #3](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/3)

**Estado:** plan de pruebas; no se han ejecutado simulaciones RTL ni ensayos en FPGA.

[Diseño principal](03_adquisicion_firmware_temporizacion.md) · [Diagramas](figuras/03_diagramas_adc_firmware.md)

## 1. Objetivo y registro de evidencia

La verificación se realiza por subsistema antes de integrar potencia. Cada caso produce entrada reproducible, resultado esperado, observación real y PASS/FAIL. No se marca un caso aprobado por inspección de este documento. Cada evidencia conserva commit, versión Vivado/Vitis, configuración y parámetros aplicados.

Se proponen tb/xadc_if/ para el banco del wrapper ADC, scripts/ para modelos y recepción, y resultados/adc_firmware/ para evidencias. Estas rutas se acuerdan con el equipo antes de implementar; este entregable solo agrega documentación.

## 2. Adquisición y AXI4-Lite

| ID | Estímulo | Resultado esperado / criterio de aprobación | Evidencia prevista |
|---|---|---|---|
| A01 | Reset en reposo y durante conversión | ENABLE/VALID/errores=0, SEQ=0; sin publicación residual | Log autoverificable y waveform |
| A02 | START_SW con modo diagnóstico | Un CONVST por solicitud aceptada; DATA correcto | Scoreboard solicitud–dato |
| A03 | Trigger PWM en modo sincronizado | Un disparo por evento válido y fase definida | Captura trigger/CONVST |
| A04 | START_SW durante SYNC_MODE | Sin segunda conversión; OVERRUN registrado | Aserciones |
| A05 | Trigger con FSM ocupada o XADC no disponible | Solicitud rechazada; no publicar dato duplicado | Conteo de aceptaciones |
| A06 | EOC sin DRDY inmediato | DATA/SEQ no cambian hasta finalizar lectura | Scoreboard de publicación |
| A07 | Resultado XADC conocido | raw12 alineado a la derecha, bits altos=0 | Vectores 0, medio y máximo |
| A08 | Timeout EOC y timeout DRDY | TIMEOUT=1, SEQ no incrementa por fallo | Casos de falla separados |
| A09 | Limpieza de errores y error simultáneo | Escritura de 1 limpia; set nuevo domina clear | Aserciones de registros |
| A10 | Publicación entre lecturas CPU | SEQ1/SEQ2 rechazan la tupla inconsistente | Modelo MMIO intercalado |
| A11 | SEQ pasa de máximo a cero | Incremento modular válido; no falso fallo | Prueba dirigida |
| A12 | AW antes de W, W antes de AW, ambos juntos | Una sola escritura por transacción | Driver AXI aleatorio |
| A13 | BREADY/RREADY retenidos | VALID y respuesta/dato estables hasta handshake | Aserciones de protocolo |
| A14 | WSTRB parcial, RO, offset inválido y desalineado | Máscara respetada; RO inmutable; SLVERR donde se especifica | Scoreboard de banco |
| A15 | ENABLE=0 con conversión aceptada | No aceptar nuevas; la aceptada termina según diseño | Prueba de deshabilitación |
| A16 | Lecturas DATA/TIME y ADC_TICKS | Lectura no borra VALID; TIME capturado y TICKS vivo | Lectura repetida |
| A17 | EOC/DRDY después de timeout; limpiar TIMEOUT sin reset | ERROR sigue bloqueado; no DATA/SEQ nuevos; reset coordinado permite WARMUP | Inyección de eventos tardíos |
| A18 | DRDY sostenido alto, tiempo tras reset y asentamiento incompleto | Sin nueva operación DRP hasta DRDY bajo; ningún disparo antes de disponibilidad; CONVST y ADCCLK cumplen hoja de datos | Aserciones y modelo AMD |
| A19 | Cambio de modo ocupado; START_SW con ENABLE=0/1; strobe nulo | SLVERR y CTRL conservado al cambiar modo activo; deshabilitado ignora; habilitado acepta una vez; strobe nulo sin efectos | Scoreboard de control |

Se verifican por separado el wrapper con modelo de respuestas XADC y la integración con el modelo de simulación suministrado por AMD. En este último se utiliza el mecanismo de estímulos analógicos documentado por UG480; no se confunden valores digitales ficticios con una medición del sensor real.

## 3. Timer, sincronización y ejecución temporal

| ID | Prueba | Criterio |
|---|---|---|
| T01 | TLR0=4998, reloj 100 MHz | Eventos separados exactamente 5000 ciclos en simulación; verificar también en placa |
| T02 | Configurar 9998 y 3998 como casos alternativos | Períodos 10000 y 4000 ciclos; PR/PWM reconfigurados con salidas bloqueadas |
| T03 | Arranque armado por generateout0 | Un sync_start_i por armado; fase inicial y latencia de registros conocidas |
| T04 | Trigger en pico e IRQ en fase 3500 | Muestra publicada antes de ISR; escritura completada antes del valle |
| T05 | Simulación de un millón de períodos | Sin deriva entre trigger, Timer y portadora |
| T06 | ISR sin UART y exportando | Todas las cotas de la sección 5.3; cero pérdidas de secuencia y deadlines |
| T07 | Retener servicios de fondo y pulsar botones | No modificar período de hardware ni exceder presupuesto ISR |
| T08 | Forzar lectura tardía y ejecución excesiva | Detectar incumplimiento y entrar en FAULT; no continuar con dato viejo |
| T09 | Wrap del contador RTL de 32 bits | Diferencias modulares correctas, sin falso jitter ni antigüedad negativa |
| T10 | Reconocimiento Timer/INTC | Una atención por período, sin tormenta de interrupciones por flag pendiente |
| T11 | WARMUP con potencia bloqueada y liberación posterior | Portadora/trigger activos sin pulsos de compuerta; primera IRQ sin falso error de SEQ; liberación en valle; fallo de arranque acotado |

Mediciones a registrar:

- Período de muestra y período entre entradas a la ISR.
- Latencia IRQ→entrada ISR y duración GPIO_ISR.
- Tiempo de publicación ADC y escritura AXI completada.
- Margen mínimo escritura→latch PWM.
- Mínimo, máximo, media, desviación estándar y jitter pico a pico.
- Pérdidas de secuencia, overrun, timeout y deadlines.
- Herramienta, resolución, conexión de sondas y carga de software.

Ensayo propuesto: un millón de períodos (50 s a 20 kHz). ADC_TICKS es el reloj de medición base; su wrap se maneja con diferencias uint32 entre lecturas cercanas. No se atribuye continuidad exacta a un AXI Timer que recargue con ciclos adicionales. Si se usa como instrumento auxiliar se documenta esa diferencia.

## 4. Firmware, control y formato numérico

| ID | Prueba | Criterio |
|---|---|---|
| F01 | Inicialización sin parámetros PR/calibración | PWM bloqueado; diagnóstico disponible |
| F02 | Códigos ADC de calibración | Corriente calculada coherente con offset, ganancia y signo |
| F03 | Referencias de 0,2 / 0,5 / 1,0 A pico a 50 Hz | Frecuencia y amplitud digitales correctas; conservar fase en el cambio |
| F04 | Vectores de error y coeficientes suministrados por PEDP | Salida y estados reproducen modelo de referencia con tolerancia acordada |
| F05 | Valores cercanos a límites Q1.15 | Redondeo, signo y saturación correctos; sin wrap numérico |
| F06 | NaN, infinito o configuración inválida | FAULT y PWM deshabilitado |
| F07 | Datos repetidos, saltos de SEQ y exceso de antigüedad | No ejecutar el PR con Ts ficticio |
| F08 | Saturación sostenida y rearme | Política coherente con PEDP; reinicio controlado de estados |
| F09 | Compilación para CPU/BSP reales | WCET y memoria dentro de límites; warnings revisados |

La comparación del PR usa los mismos Ts, orden de operaciones y unidades del modelo de PEDP. Las tolerancias de punto flotante se fijan antes del ensayo, según rango y acumulación; no se elige una tolerancia después de observar el error. Se conserva como referencia una implementación de mayor precisión.

## 5. Histórico, UART y receptor PC

| ID | Prueba | Criterio |
|---|---|---|
| U01 | Llenar y envolver cada ring | Orden temporal correcto y valid_count limitado a N |
| U02 | Solicitar captura mientras escribe la ISR | Intercambio coherente de punteros; sin copia de N registros en ISR |
| U03 | Transmisión lenta o pausada | Banco congelado inmutable; ISR continúa usando el otro |
| U04 | Segunda pulsación durante TX | Estado ocupado; no destruir la captura activa |
| U05 | Captura con menos de cinco ciclos | Exportar solo datos inicializados y flag de captura corta |
| U06 | Cabecera y payload conocidos | Tamaños exactos, endianness y signo comprobados |
| U07 | Vector CRC ASCII 123456789 | CRC32 = 0xCBF43926 |
| U08 | Cambiar un byte, truncar o declarar longitud excesiva | Receptor rechaza trama y se resincroniza con límite de memoria |
| U09 | Payload que contiene bytes TDD4 | No abortar una trama válida por el magic interno |
| U10 | Desconectar y reconectar PC | Timeout; nueva trama aceptable; control independiente de la PC |
| U11 | Exportar N=2048 | 8244 bytes; mínimo teórico 0,715625 s a 115200, 8N1 |
| U12 | Error que rompe continuidad | Captura cerrada antes de la discontinuidad y flags identificados |
| U13 | Capturas de 0, 2000, 2001 y 2048 registros a 20 kHz; fallo después de congelar | Cero no se exporta; 2000 corta, 2001/2048 cubren cinco períodos entre extremos; metadatos FROZEN inmutables |

El CSV se confronta con la captura binaria y sus metadatos; la conversión no debe incorporar una calibración distinta de la exportada. El análisis de rizado de conmutación se realiza con instrumentación apropiada de PEDP, no se deduce de una muestra sincronizada por período.

## 6. Integración gradual

1. Aprobar registros, canal, parámetros y enlace de sincronización.
2. Verificar wrapper ADC y AXI mediante simulación.
3. Probar firmware con secuencias sintéticas y PWM bloqueado.
4. Validar Timer, fase, lectura y escritura en FPGA sin etapa energizada.
5. Confirmar calibración y tiempos efectivos.
6. Coordinar con Keylor la comprobación de compuertas y tiempo muerto sin potencia.
7. Ejecutar ensayos integrados con PEDP y límites de operación.
8. Adjuntar resultados y revisión cruzada al PR.

## 7. Trazabilidad de criterios de aceptación del Issue #3

| Criterio del issue | Sección o figura | Verificación |
|---|---|---|
| Arquitectura ADC/XADC documentada | Principal §3; D2–D4 | A01–A19 |
| Sincronización con PWM definida | Principal §5.3; D9 | T03–T05, T11 |
| AXI Timer especificado | Principal §5.1 | T01–T02, T10 |
| Interrupciones documentadas | Principal §5.2, §6.2; D6 | T06–T08 |
| Arquitectura firmware definida | Principal §6; D5 | F01, F09 |
| Flujo de PR descrito | Principal §7; D6, D8 | F03–F08 |
| Almacenamiento y UART | Principal §8; D7 | U01–U13 |
| Diagramas técnicos incluidos | Archivo de figuras D1–D10 y Lucid | Revisión visual y semántica |
| Estrategia de verificación | Este documento y D10 | Revisión de cobertura |
| Contenido en docs/diseno/ | Tres documentos y enlaces desde planteamiento.md | Comprobar diff del PR |
| Revisión mediante Pull Request | Rama docs/adc-firmware-design → develop | Pendiente de publicación/revisión |

La documentación no marca el issue como completado ni sustituye la aceptación del profesor. Los contratos pendientes del diseño principal deben cerrarse y el PR debe pasar revisión. El informe final agregará valores reales, capturas y resultados, sin presentar este plan como evidencia experimental.

## 8. Revisión documental ejecutada el 4 de noviembre de 2026

| Comprobación | Resultado y alcance |
|---|---|
| Base y alcance | Rama docs/adc-firmware-design alineada con origin/develop en 32c4a65 antes de editar; sin cambios locales previos; sin AGENTS.md aplicable encontrado |
| Fuentes recuperadas | Tres Markdown del ZIP anterior, enunciado EL3313_proyecto4_2S2026.pdf, reparto genérico, Issues #2/#3 y diez páginas existentes de Lucid |
| Revisión técnica | Contrastados Timer descendente con PG079, SetResetValue/orden del handler con código oficial AMD y conversión/DRP con UG480; BSP concreto pendiente |
| Enlaces relativos | 29 destinos/anclas resueltos en los cuatro Markdown revisados; dos referencias a imágenes generales ausentes ya existían en la plantilla |
| Markdown | Cercos balanceados y diff sin errores de espacios; apartados de Marvin/Keylor conservados |
| Mermaid | Sintaxis de D1–D10 validada con Mermaid 12.3.0; diez SVG/PNG renderizados localmente con Edge headless y revisados visualmente |
| Cálculos | TLR=9998/4998/3998; bancos=16384 bytes; trama completa=8244 bytes; tiempo UART mínimo=0,715625 s; ventana entre extremos=102,35 ms; mínimo de 2001 muestras para cinco períodos a 20 kHz |
| CRC32 de referencia | Cálculo independiente de ASCII 123456789 dio 0xCBF43926; solo verifica el vector, todavía no un transmisor/receptor implementado |
| Cobertura del plan | 52 casos propuestos: 19 ADC/AXI, 11 temporales, 9 firmware y 13 UART/memoria; trazabilidad hacia criterios del Issue #3 |
| Hardware/software real | No se ejecutaron RTL, síntesis, compilación Vitis, medición WCET ni ensayos FPGA/PEDP; todos permanecen pendientes |

Los dos destinos preexistentes ausentes son `figuras/diagrama_primer_nivel.png` y `figuras/diagrama_segundo_nivel.png`, correspondientes a los diagramas generales del equipo. Esta revisión agrega figuras del subsistema de Luis Diego y no sustituye esas figuras globales. La tabla de correspondencia de Lucid identifica ADC_TICKS, recuperación por reset, arranque armado y D10 de verificación como ampliaciones del Markdown.

La sintaxis y el render local no garantizan el mismo motor o distribución visual en todas las versiones de GitHub. Las vistas previas y dependencias de validación se conservaron temporalmente fuera de Proyecto_4; el contenido versionado sigue siendo Markdown editable, sin RTL, firmware ni dependencias de compilación nuevas.
