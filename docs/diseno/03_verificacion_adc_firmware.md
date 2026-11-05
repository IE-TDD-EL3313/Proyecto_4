# Verificación de adquisición, firmware y temporización

**Estado:** plan de pruebas; no se han ejecutado simulaciones RTL ni ensayos en FPGA.

## 1. Objetivo y registro de evidencia

La verificación se realiza por subsistema antes de integrar potencia. Cada caso produce entrada reproducible, resultado esperado, observación real y PASS/FAIL. No se marca un caso aprobado por inspección de este documento. Cada evidencia conserva commit, versión Vivado/Vitis, configuración y parámetros aplicados.

Se proponen tb/xadc_if/ para el banco del wrapper ADC, scripts/ para modelos y recepción, y resultados/adc_firmware/ para evidencias. Las rutas se fijan durante la integración de los bancos de pruebas y las herramientas de adquisición.

## 2. Adquisición y AXI4-Lite

| ID | Estímulo | Resultado esperado / criterio de aprobación | Evidencia prevista |
|---|---|---|---|
| A01 | Reset en reposo y durante conversión | ENABLE/VALID/errores=0, SEQ=0; sin publicación residual | Log autoverificable y waveform |
| A02 | START_SW con modo diagnóstico | Un CONVST por solicitud aceptada; DATA correcto | Scoreboard solicitud–dato |
| A03 | Trigger PWM de cuatro ciclos en modo sincronizado, también alto durante reset/habilitación | Un disparo por flanco válido, sin duplicados; esperar nivel bajo tras reset | Captura trigger/CONVST |
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
| A19 | Cambio de modo con ENABLE previo=1 o BUSY=1; START_SW con ENABLE=0/1; strobe nulo | Cambio de modo rechazado con SLVERR y CTRL conservado; START_SW deshabilitado ignorado y habilitado aceptado una vez; strobe nulo sin efectos | Scoreboard de control |

Se verifican por separado el wrapper con modelo de respuestas XADC y la integración con el modelo de simulación suministrado por AMD. En este último se utiliza el mecanismo de estímulos analógicos documentado por UG480; no se confunden valores digitales ficticios con una medición del sensor real.

## 3. Timer, sincronización y ejecución temporal

| ID | Prueba | Criterio |
|---|---|---|
| T01 | TLR0=4998, reloj 100 MHz | Eventos separados exactamente 5000 ciclos en simulación; verificar también en placa |
| T02 | Configurar 19998, 9998 y 2498 como casos alternativos | Períodos 20000, 10000 y 2500 ciclos; PR/PWM reconfigurados con salidas bloqueadas |
| T03 | Arranque armado por generateout0 | Un sync_start_i por armado; fase inicial y latencia de registros conocidas |
| T04 | Trigger en valle e IRQ en fase 1000 a 20 kHz | Publicación en menos de 1000 ciclos con margen; escritura completada antes del siguiente valle |
| T05 | Simulación de un millón de períodos | Sin deriva entre trigger, Timer y portadora |
| T06 | ISR sin UART y exportando | Todas las cotas de la sección 9.2.3 del planteamiento del diseño; cero pérdidas de secuencia y deadlines |
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
| F05 | Límites Q1.15 y combinaciones de frecuencia/tiempo muerto | Redondeo y signo correctos; código dentro de ±31129 y margen de pulso mínimo; rechazar configuración sin margen |
| F06 | NaN, infinito, Vdc no positivo o configuración inválida | FAULT antes de cuantizar o escribir; PWM deshabilitado |
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
| U06 | Cabecera y payload conocidos, y fs_hz/TICKS incompatibles | Tamaños, endianness y signo correctos; rechazar fs_hz·sample_period_ticks distinto de 100000000 |
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
6. Comprobar compuertas y tiempo muerto del PWM con potencia desenergizada.
7. Ejecutar ensayos integrados con PEDP y límites de operación.
8. Consolidar los resultados y comprobar los criterios de aceptación.
