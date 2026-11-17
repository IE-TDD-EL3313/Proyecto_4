# Guía personal de estudio: adquisición y firmware

Material de apoyo para comprender el código y preparar la defensa. La documentación técnica del proyecto se conserva en [planteamiento.md](../diseno/planteamiento.md).

Este documento se versiona únicamente en la rama personal `feat/adc-firmware`. La rama `integracion/adc-firmware` contiene la versión destinada a revisión e integración con `develop`, sin esta guía. El futuro PR del proyecto debe prepararse desde la rama de integración, comprobando que `docs/personal/` no aparezca entre los archivos añadidos. Un merge completo desde la rama personal incluiría también esta guía; `.gitignore` no excluye archivos ya versionados de un merge.

Al incorporar nuevos cambios de código, se actualiza la rama de integración con los cambios técnicos y se conserva esta guía fuera de ella. No se debe fusionar toda la rama personal a `develop` esperando que Git omita el documento automáticamente.

Las referencias a las secciones 8, 9 y 15 corresponden al planteamiento técnico; las secciones 1 y 2 siguientes pertenecen a esta guía. La revisión de requisitos conserva su fecha y alcance original: 11 de noviembre de 2026.

## 1. Explicación del código y preparación de la defensa

### 1.1 Qué se desarrolló y hasta dónde está verificado

Esta parte implementa el camino digital de adquisición, procesamiento de corriente y preparación de la exportación. El recorrido previsto es: el PWM solicita una conversión, el wrapper publica una muestra con secuencia y tiempo, la rutina periódica calcula el PR y prepara el mando PWM, y el histórico conserva los datos para exportarlos desde main. Se separaron el hardware, el algoritmo y el acceso a la plataforma para probarlos antes de disponer del sistema completo.

El resultado actual contiene RTL propio, bibliotecas C compilables, un servicio periódico con periféricos simulados, pruebas y herramientas de PC. No contiene todavía un `main` enlazado con un BSP de la Nexys 4, un XSA del sistema ni la conexión física del XADC. Por tanto, puede presentarse como implementación parcial verificada y base de integración. Su ejecución sobre MicroBlaze V debe demostrarse posteriormente.

### 1.2 `rtl/xadc_if/xadc_acquisition.sv`: motor de adquisición

Este módulo controla una operación XADC, independientemente del bus del procesador. Recibe el reloj, reset, habilitación, selección de modo, solicitud software y trigger PWM. Las entradas ready, BUSY, EOC, DRDY y DO provienen de la plataforma/XADC. Emite CONVST para disparar y DEN/DADDR/DWE para leer el resultado por DRP.

| Parte del código | Explicación |
|---|---|
| `trigger_previous` y `trigger_armed` | Detectan un flanco ascendente. Exigir primero nivel bajo evita convertir un trigger residual de reset en una solicitud nueva |
| `request` | Selecciona exclusivamente START_SW o el flanco PWM según SYNC_MODE |
| `available` y `quiet_cycles` | Exigen ready, XADC libre, DRDY/EOC inactivos y el intervalo de asentamiento parametrizado |
| `accepted_time` | Conserva el contador de tiempo al aceptar la solicitud; no fecha la muestra al terminar de leerla |
| `pending_data` | Guarda DO[15:4], los doce bits de conversión del formato unipolar propuesto |
| `watchdog` | Acota la espera para que una conversión o lectura sin respuesta termine en ERROR |
| `overrun_o` / `timeout_o` | Conservan las fallas hasta W1C; una falla nueva domina una limpieza simultánea |
| `ticks_o` / `seq_o` | El primero cuenta ciclos libremente; el segundo identifica publicaciones exitosas. Ambos trabajan módulo 2^32 |

| Estado | Acción y condición de salida |
|---|---|
| IDLE | Espera una solicitud habilitada y disponible |
| PULSE | Mantiene CONVST el número configurado de ciclos |
| WAIT_EOC | Espera el fin de conversión |
| ISSUE_READ | Espera DRDY bajo y emite DEN por un DCLK |
| WAIT_DRP | Captura el dato cuando DRDY indica una respuesta válida |
| PUBLISH | Actualiza DATA, TIME, SEQ y VALID en el mismo flanco |
| ERROR | Bloquea operaciones nuevas y descarta respuestas tardías hasta reset coordinado |

Publicar los campos juntos permite que el software compruebe coherencia mediante SEQ. VALID significa que existe una muestra publicada; no demuestra que el procesador todavía no la haya consumido. Esa novedad se verifica con la secuencia y la antigüedad. Deshabilitar nuevas solicitudes permite finalizar una operación aceptada; reset sí la cancela.

### 1.3 `rtl/xadc_if/xadc_axi_lite.sv`: acceso del procesador

Este módulo instancia el motor anterior y presenta registros de 32 bits. Los canales de dirección de escritura AW y de datos W pueden llegar en ciclos distintos: `aw_held` y `w_held` conservan cada transferencia y se realiza una sola escritura cuando están ambas disponibles. La respuesta B se mantiene hasta BREADY. En lectura, se captura el dato al aceptar AR y se conserva mientras RREADY esté bajo, aunque el ADC publique otra muestra.

| Offset | Registro | Uso desde software |
|---:|---|---|
| 0x00 | CTRL | ENABLE, SYNC_MODE y pulso START_SW |
| 0x04 | STATUS | VALID, BUSY, OVERRUN y TIMEOUT |
| 0x08 | DATA | Código ADC de doce bits alineado a la derecha |
| 0x0C | SEQ | Identificador de la publicación |
| 0x10 | TIME | Tiempo de aceptación de la solicitud publicada |
| 0x14 | ERR_CLEAR | Limpieza W1C de los indicadores de error |
| 0x18 | TICKS | Lectura del contador libre actual |

WSTRB evita modificar bytes no seleccionados. Las direcciones inválidas o desalineadas y las escrituras RO producen SLVERR. START_SW se lee como cero porque representa una acción, no un estado persistente. Cambiar de modo con ENABLE previo activo o BUSY alto se rechaza para no reinterpretar una conversión en curso. El empaquetado y la conexión de este módulo a la plataforma final todavía requieren integración.

### 1.4 `vitis/drivers/tdd_mmio.h` y `tdd_mmio.c`: puente entre C y registros

`struct tdd_bus` reúne un contexto y las funciones `read32` y `write32`. En las pruebas, esas funciones representan registros simulados. En la Nexys 4 deberán realizar MMIO ordenado y sin caché sobre las direcciones del Address Editor. El driver trabaja con offsets; no inventa direcciones base ni accede directamente a un arreglo de registros como si fuese el periférico real.

**Lectura `tdd_adc_read`.** Primero valida los punteros y después intenta como máximo dos veces la secuencia:

```text
SEQ1 -> DATA -> TIME -> STATUS -> SEQ2
```

Si SEQ1 difiere de SEQ2, el ADC publicó durante la lectura y se repite la operación. Si son iguales, se comprueban los errores y el rango raw. La máscara decimal 12 equivale a 0x0C: selecciona OVERRUN, bit 2, y TIMEOUT, bit 3. La máscara 1 selecciona VALID. Se devuelve `TDD_ADC_OK`, `TDD_ADC_NOT_READY`, `TDD_ADC_INCOHERENT` o `TDD_ADC_ERROR`, de modo que la aplicación pueda decidir sin esperar indefinidamente.

Una tupla coherente puede ser una muestra antigua. Por eso este driver no reemplaza las comprobaciones posteriores de siguiente SEQ y edad en `tdd_control_step`. La lectura solo copia el resultado a `out` después de validarlo.

**Deshabilitación `tdd_pwm_disable`.** Escribe CTRL en offset cero con ENABLE=0 y el código de frecuencia recibido en bits 2:1. La expresión `(frequency_code & 3u) << 1` conserva únicamente los dos bits de selección. Esta función no implementa por sí sola el bloqueo independiente de compuertas C4.

**Actualización `tdd_pwm_write_duty`.** Rechaza códigos fuera de ±31129. Convierte el `int16_t` con signo a `uint16_t` para conservar sus dieciséis bits, y después a `uint32_t` para escribir con los bits superiores reservados en cero. Por ejemplo, -31129 se transmite como 0x00008667. Esto conserva el complemento a dos sin extender el signo sobre los bits reservados. La función comprueba el argumento; la respuesta física del bus depende del adaptador BSP.

### 1.5 `vitis/drivers/tdd_timer.h` y `tdd_timer.c`: base temporal

| Función | Qué realiza |
|---|---|
| `tdd_timer_prepare` | Comprueba que fs divida exactamente 100 MHz, detiene ambos contadores, calcula TLR, carga el contador 0 y lo deja preparado sin arrancar |
| `tdd_timer_start` | Habilita cuenta descendente, Generate, auto-reload e IRQ después de que la plataforma prepare INTC y sincronización |
| `tdd_timer_ack` | Comprueba interrupción habilitada y pendiente; reconoce T0INT mediante W1C conservando configuración y sin recargar el período |

A 20 kHz hay 5000 ciclos de reloj por período y el valor crudo TLR es 4998 por la relación del Timer documentada en la sección 9.2.1. Los bits se nombran en un `enum` para separar modo, carga, habilitación e interrupción. Se reconoce el evento antes de calcular el control. El código no registra automáticamente el handler en INTC y no verifica por sí solo que el Timer físico produzca 50 µs.

### 1.6 `control_core.h`, `control_core.c` y `reference_lut.h`: algoritmo y estados

El header define la configuración, la muestra, la salida, los estados WARMUP/RUNNING/FAULT y las causas de falla. La configuración contiene los parámetros físicos y numéricos; los estados `e1`, `e2`, `u1`, `u2` conservan la historia del regulador. `tdd_output` entrega corriente medida, referencia, acción en voltios, modulación calculada y código Q1.15.

| Función | Explicación |
|---|---|
| `tdd_sine` | Interpreta una fase de 32 bits, identifica cuadrante, refleja un cuarto de seno e interpola dos valores de tabla |
| `tdd_to_q15` | Valida voltios, Vdc y límite; calcula m=u/Vdc, limita, redondea y acota el código; informa si hubo saturación |
| `tdd_config_valid` | Rechaza parámetros no finitos, calibración nula, tiempos incompatibles, umbrales inválidos y modulación sin margen frente al tiempo muerto |
| `tdd_control_init` | Inicializa estados en cero, valida configuración, establece WARMUP y calcula el incremento de fase para 50 Hz |
| `tdd_control_set_amplitude` | Cambia la amplitud conservando la fase; se invoca desde el propietario ISR, una vez publicada la solicitud de main |
| `tdd_control_step` | Valida muestra, secuencia y tiempo; calibra corriente, genera referencia, evalúa PR, convierte a Q1.15 y actualiza estados |
| `fail` | Centraliza FAULT, conserva su causa y devuelve una salida cero |

La tabla `reference_lut.h` contiene 65 valores de sin(pi·k/128), incluyendo los extremos del primer cuadrante. Ocupa 260 bytes de valores float32. La simetría y la interpolación permiten generar el seno sin llamar a una función trigonométrica en cada interrupción. El error de interpolación se contrasta con una referencia de mayor precisión en la prueba numérica.

La calibración implementa `i=(raw-offset_code)*gain_a_per_code`; la ganancia puede incorporar la polaridad del sensor. El PR aplica la ecuación de segundo orden de la sección 15.3. El denominador se comprueba con condiciones de estabilidad de segundo orden; eso no demuestra estabilidad de todo el lazo con planta, retardos y saturación.

El candidato de pruebas se obtiene de la estructura PR amortiguada y de Tustin, descritas también por [Imperix, TN110](https://imperix.com/doc/implementation/proportional-resonant-controller):

```math
C(s)=K_p+\frac{2K_r\omega_b s}{s^2+2\omega_b s+\omega_0^2},\qquad
s=\frac{2}{T_s}\frac{1-z^{-1}}{1+z^{-1}}.
```

```math
D=4+4\omega_bT_s+\omega_0^2T_s^2,\quad
\alpha=\frac{4K_r\omega_bT_s}{D},\quad
d_1=\frac{2\omega_0^2T_s^2-8}{D},\quad
d_2=\frac{4-4\omega_bT_s+\omega_0^2T_s^2}{D}.
```

La rama resonante tiene numerador `[alpha, 0, -alpha]` y denominador `[1, d1, d2]`. Al sumar Kp se obtiene el PR completo que evalúa el código: `b=[Kp+alpha, Kp*d1, Kp*d2-alpha]`, `a=[1, d1, d2]`. Con Ts=50 µs, Kp=10, Kr=100, omega_b=2·pi·3 y omega_0=2·pi·50 se obtienen los coeficientes de ensayo de la sección 15.3. Esto explica su origen y unidades; la sintonización definitiva debe validarse con PEDP y con el retardo real.

En WARMUP se exige una primera muestra dentro del plazo y de los umbrales; esa primera publicación establece secuencia y origen de fase y deja mando cero. Las siguientes muestras deben incrementar SEQ en uno y TIME exactamente un período, con aritmética modular. Un dato repetido no se procesa otra vez. La saturación numérica está implementada, pero el supervisor adopta provisionalmente FAULT al alcanzarla. No se presenta esa política como anti-windup definitivo.

### 1.7 `control_runtime.h` y `control_runtime.c`: servicio periódico

`struct tdd_platform` conecta los buses ADC, PWM y Timer y las operaciones de bloqueo, liberación en valle y marcación GPIO. `struct tdd_runtime` reúne el núcleo, la captura, las cotas temporales y las métricas. Es una estructura grande por sus bancos y debe reservarse estáticamente.

`tdd_runtime_prepare` bloquea compuertas, deshabilita PWM, valida C4 y las cotas, inicializa control/captura y prepara Timer. Solo marca `prepared` al finalizar correctamente. Todavía exige que la plataforma conecte INTC, configure XADC y arranque las bases temporales con la fase correcta.

`tdd_runtime_irq` realiza la siguiente secuencia acotada:

1. Lee TICKS y eleva la marcación de ISR.
2. Reconoce Timer; una invocación sin evento pendiente no procesa una muestra.
3. Si ya hay FAULT, permite atender solicitudes de captura de diagnóstico.
4. Solicita una tupla ADC coherente y procesa WARMUP o RUNNING.
5. Comprueba la reserva de tiempo para MMIO antes del latch.
6. Escribe la referencia pendiente y comprueba el tiempo después de completar la escritura.
7. Solicita una única liberación de compuertas en valle, cuando corresponde al arranque.
8. Inserta la muestra y referencia en el histórico.
9. Actualiza la duración instrumentada máxima y retira la marcación.

`ticks` encapsula la lectura del contador. `runtime_fault` pide bloqueo prioritario, deshabilita PWM, fija el estado y marca la captura. Un callback simulado no demuestra que las compuertas físicas se apaguen: C4 necesita implementarse y probarse con PWM. Asimismo, `max_instrumented_ticks` mide una ventana del código; no incluye automáticamente todo el prólogo, epílogo y despachador del BSP ni constituye una medición WCET de la FPGA.

### 1.8 `capture.h` y `capture.c`: histórico y trama

El header define registros, metadatos, dos bancos, banderas y estado del transmisor. Cada registro actual contiene raw de 16 bits y referencia en mA de 16 bits con signo. Los metadatos conservan calibración, frecuencia, secuencia inicial, tiempo inicial, cantidad y flags del intervalo.

| Función | Explicación |
|---|---|
| `tdd_capture_init` | Valida fs/ticks/calibración e inicializa bancos y señales atómicas antes de habilitar interrupciones |
| `tdd_capture_request` | Publica desde main una solicitud de snapshot; informa ocupado si ya hay una solicitud o un congelado |
| `tdd_capture_push` | Valida raw/referencia y continuidad, inserta un registro y avanza el índice circular sin salir del arreglo |
| `tdd_capture_service` | Atiende liberación o solicitud, congela metadatos y cambia de banco sin copiar los 2048 registros |
| `tdd_capture_fault` | Detiene nuevas inserciones y marca únicamente el intervalo activo, conservando el FROZEN anterior |
| `tdd_tx_begin` | Toma el banco publicado y construye la cabecera de 48 bytes |
| `tdd_tx_read` | Produce hasta el número solicitado de bytes, recorre registros desde el más antiguo y libera el banco al terminar |
| `tdd_crc32_update` | Actualiza el CRC reflejado byte a byte con el polinomio 0xEDB88320 |
| `put16`, `put32`, `put_float` | Serializan campos explícitamente en little-endian y conservan la representación binaria de float32 |

Las comprobaciones de compilación verifican el tamaño de registro y el formato float requerido. No se envía una estructura C completa porque podría contener padding. El índice `head` señala la próxima escritura; el registro más antiguo se obtiene con `(head+N-count)%N`. El conteo se limita a N aunque el índice dé vueltas.

Las operaciones atómicas acquire/release publican propiedad y visibilidad de metadatos. El contrato supone un propietario ISR del banco activo y un único transmisor en main; no habilita llamadas concurrentes arbitrarias a la biblioteca. La prueba intercala el servicio de ambos contextos y verifica que un fallo posterior no altere el snapshot anterior.

Dos arreglos ocupan 16 KiB. A 20 kHz, 2048 muestras abarcan 102.35 ms entre extremos; 2001 son el mínimo para 100 ms entre extremos. A 40 kHz esa capacidad es insuficiente para el mismo intervalo y se marca captura corta. La UART requiere al menos 0.715625 s para 8244 bytes a 115200/8N1; por eso la exportación avanza por fragmentos desde main mientras la ISR usa el otro banco. Falta conectar esos fragmentos a la FIFO UART real.

### 1.9 `scripts/capture_receiver.py`: aplicación de PC

| Elemento | Explicación |
|---|---|
| `Capture` | Representa una captura decodificada y calcula su duración a partir de cantidad y ticks |
| `inspect_header` | Verifica magic, versión, tipo, tamaños, capacidad, fs/ticks, calibración y flags antes de confiar en la longitud |
| `FrameParser._parse` | Busca una cabecera, espera una trama completa, verifica CRC/raw y se resincroniza tras errores |
| `FrameParser.feed` | Acepta fragmentos arbitrarios, procesa por partes y mantiene acotado el buffer de recepción |
| `FrameParser.expire` | Descarta un candidato incompleto cuando vence su plazo |
| `FrameParser.finish` | Detecta una trama iniciada y truncada al terminar un archivo |
| `save_capture` | Escribe CSV de muestras y JSON de metadatos, aplicando la calibración transmitida y evitando sobrescribir archivos existentes |
| `main` | Selecciona archivo binario o puerto serie, configura 115200/8N1 en modo serie y procesa las capturas |

La secuencia se reconstruye módulo 2^32 y el tiempo CSV es relativo al primer registro. La referencia se convierte de mA a A. No se permite inferir rizado de conmutación desde una sola muestra sincronizada por período PWM. El modo archivo usa la biblioteca estándar; el modo serie requiere pyserial y una transmisión física que todavía debe validarse.

### 1.10 Scripts de reproducción y bancos de prueba

| Archivo | Función, entradas y evidencia |
|---|---|
| `scripts/test_local.py` | Orquesta compilación/simulación Icarus, dos ejecutables C, once pruebas Python y conversión de una trama sintética. Detiene la ejecución si un comando falla y guarda el log en `build/host/verificacion.txt` |
| `scripts/cross_compile_core.py` | Localiza GCC/size/nm de RISC-V, genera cinco objetos con RV32IMC/ILP32 y revisa que captura no requiera helpers atómicos. No enlaza BSP ni produce una aplicación final |
| `scripts/configure_control_peripherals.tcl` | `set_control_parameters` comprueba existencia y lectura de retorno; `create_control_peripherals` crea Timer/UART/GPIO sin sobrescribir celdas homónimas |
| `scripts/verify_vivado.tcl` | Selecciona FPGA, copia RTL a una ruta temporal corta, sintetiza OOC y genera reportes/checkpoint. En otro proyecto comprueba los parámetros IP y exporta Tcl. Los modos all/ips/synth delimitan el trabajo |
| `tb/xadc_if/tb_xadc_axi_lite.sv` | Modela respuestas digitales XADC y ejerce AXI con AW/W en distintos órdenes, backpressure, errores, reset, flancos, timeouts y wrap. Las tareas de bus contrastan respuestas y la simulación tiene timeout global |
| `tb/firmware/test_firmware.c` | Verifica números, supervisor, MMIO, buffers, CRC, fallo con snapshot previo y un lazo RL promedio. Produce la captura sintética que se compara con Python |
| `tb/firmware/test_runtime.c` | Simula Timer, buses, bloqueo y reloj. Comprueba reconocimiento temprano, ausencia de liberación sin C4 y fallos antes/después del deadline |
| `tb/firmware/test_capture_receiver.py` | `frame` construye estímulos; `ReceiverTests` comprueba fragmentación, corrupción, timeout, límites, duración, calibración, secuencias, CSV e interoperabilidad C/Python |

La configuración de `test_firmware.c` es un estímulo de ensayo, no una calibración medida. `closed_loop` implementa una planta RL promedio de mayor precisión y un retardo de un período. No representa conmutación, diodos, ruido ni ejecución real de MicroBlaze V. Los contadores de aserciones incluyen repeticiones en esos recorridos, no miles de ensayos físicos independientes.

### 1.11 Cómo explicarlo en una defensa

> Mi parte comprende adquisición de corriente, temporización, firmware y preparación de la exportación. Separé el motor XADC del bus AXI para verificar el protocolo y el manejo de errores de forma independiente. Cada muestra publicada incluye dato, secuencia y tiempo, lo que permite detectar lecturas mezcladas, pérdidas y datos antiguos. En C implementé calibración, referencia de 50 Hz, PR discreto y conversión a Q1.15. El servicio periódico comprueba las condiciones antes de escribir el PWM y pide bloqueo si ocurre una falla. Para exportar utilicé dos bancos: uno se actualiza y el otro permanece congelado mientras main serializa la captura con CRC. Verifiqué estos componentes con simulación, pruebas C/Python y compilación RISC-V, y sinteticé el ADC para Nexys 4. La etapa pendiente es integrar XADC, PWM y BSP, validar la UART física y medir el período, jitter y ejecución sobre la placa.

| Pregunta probable | Respuesta sustentada |
|---|---|
| ¿Por qué leer SEQ dos veces? | Para detectar una publicación durante la lectura de DATA/TIME/STATUS |
| ¿Por qué no basta VALID? | Puede seguir activo aunque el procesador ya haya usado esa muestra; SEQ y TIME identifican novedad y edad |
| ¿Por qué dos bancos? | Para conservar inmutable una captura mientras sigue el registro, sin copiar N muestras en la ISR |
| ¿Por qué Q1.15? | Representa modulación bipolar y coincide con los dieciséis bits del PWM; la conversión conserva el signo y respeta el límite |
| ¿El CRC demuestra que se midió bien la corriente? | Demuestra integridad de los bytes recibidos; la exactitud depende de adquisición y calibración |
| ¿Las pruebas prueban tiempo real? | Prueban las decisiones del código con tiempos simulados; WCET y jitter físico siguen pendientes |
| ¿El WNS positivo cierra el proyecto? | Es un resultado posterior a síntesis de un bloque aislado; faltan restricciones integradas, colocación y ruteo |
| ¿Qué falta para afirmar cumplimiento total? | Plataforma/BSP, señal analógica y calibración, C4 del PWM, UART/botón, memoria final y ensayos físicos |


---

## 2. Revisión de requisitos y alcance de la entrega

Revisión del 11 de noviembre de 2026. Se contrastaron el enunciado oficial `EL3313_proyecto4_2S2026.pdf` (secciones 4.1, 4.2, 5.2, 6, 7 y 8.1), los criterios actuales de GitHub y los archivos de esta rama. La conclusión es **cumplimiento parcial: componentes preparados y verificados localmente, con integración física pendiente**. La publicación del avance permite revisión del equipo; no acredita la demostración funcional final.

### 2.1 Requisitos del enunciado

| Requisito | Evidencia actual | Pendiente para cumplir en el sistema |
|---|---|---|
| MicroBlaze V a 100 MHz, caché de instrucciones 8 KB, BRAM local mínima 32 KB, AXI y depuración | Núcleo portable compilado para un perfil RV32IMC | Block Design, configuración CPU/MDM, BSP y linker; responsabilidad de plataforma coordinada con el equipo |
| Conversión por software y trigger PWM, indicador y dato ADC | Wrapper y registros ejercitados por testbench | XADC real, canal, acondicionamiento y prueba con MicroBlaze V |
| ISR periódica adquisición→PR→PWM→histórico | Runtime y Timer ensayados con periféricos simulados | INTC, reconocimiento BSP, secuencia real y medición de tiempo |
| PWM personalizado, registros y al menos tres frecuencias | Driver compatible con el diseño Q1.15 | RTL PWM de su responsable, prueba de interfaz y acuerdos C4/C5 |
| Histórico circular de corriente | Dos bancos e índices probados | Capacidad final frente a memoria real de programa/datos |
| Botón que inicia UART sin afectar control | Snapshot, serialización y receptor probados en archivo | Lectura/antirrebote del botón, FIFO UART y medición con TX activo |
| Validación, período/jitter extendidos y post-implementación PWM | Verificación local, síntesis ADC y compilación cruzada | FPGA, modelo AMD, ejecución extendida y simulación temporizada del PWM |
| Firmware completo y fuentes reconstruibles | Bibliotecas, pruebas y scripts disponibles | main de plataforma, direcciones, constraints, BSP y aplicación final |
| Informe, evidencias y revisión con ramas/PR | Diseño actualizado y resultados locales | Evidencias físicas, informe final y revisión/integración del equipo |

El enunciado, sección 5.2, menciona Vivado/Vitis **2025.1**. El entorno utilizado es **2026.1**. Es una diferencia que debe confirmarse con el profesor/equipo o resolverse reproduciendo en la versión solicitada; no se afirma compatibilidad de proyectos binarios entre versiones. Asimismo, sintetizar con un reloj de 10 ns no demuestra que el procesador ni el sistema completo operen correctamente a 100 MHz.

### 2.2 Criterios de los issues asignados

Estados de revisión: **L** = implementado y probado localmente en el alcance indicado; **D** = documentado; **P** = parcial; **H** = pendiente de integración o evidencia. L y D no equivalen a aprobación de la demostración física ni autorizan cerrar automáticamente el issue. La matriz es una evaluación de evidencia, no una modificación de las casillas en GitHub.

El [Issue #3](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/3) figura **cerrado** al consultar GitHub, con sus criterios de diseño marcados. Ese estado corresponde al diseño; no se extiende a los issues de implementación siguientes.

#### Issue #6: Configurar e integrar el sistema de adquisición ADC/XADC

Fuente: [Issue #6](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/6), abierto al momento de la revisión.

| Criterio de aceptación | Estado | Evidencia o falta concreta |
|---|---|---|
| ADC/XADC configurado correctamente en Vivado. | H | Falta configurar e instanciar el XADC real; el wrapper está probado. |
| Canal de adquisición definido. | H | 0x10 es un candidato DRP; falta acordar canal y conexión analógica. |
| Interfaz con MicroBlaze V preparada y verificada. | P | Registros y driver verificados con modelos; falta acceso desde MicroBlaze V. |
| Mecanismo de sincronización con PWM definido e implementado. | P | Detector y solicitud RTL probados; falta PWM y conversión reales. |
| Datos de conversión accesibles desde el sistema embebido. | H | No hay adquisición ejecutada sobre el sistema embebido. |
| Configuración compatible con los requisitos de temporización. | P | Cotas simuladas; faltan ADCCLK, asentamiento y latencias físicos. |
| Evidencias de verificación documentadas. | L | Hay pruebas RTL, síntesis aislada y resultados documentados. |
| Archivos necesarios incorporados al repositorio. | P | Se entrega el wrapper y sus pruebas; faltan plataforma/IP y constraints. |
| Cambios revisados mediante Pull Request. | H | Revisión e integración mediante PR pendientes. |

#### Issue #9: Configurar AXI Timer, UART Lite y GPIO

Fuente: [Issue #9](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/9), abierto al momento de la revisión.

| Criterio de aceptación | Estado | Evidencia o falta concreta |
|---|---|---|
| AXI Timer configurado correctamente. | L | Creación y lectura de parámetros Timer verificadas en Vivado; sin sistema integrado. |
| Temporizador preparado para generar interrupciones periódicas. | P | Driver prepara TLR e IRQ; falta medir el evento y conectar INTC. |
| AXI UART Lite configurado. | L | Parámetros UART 115200/8N1 aceptados y leídos de vuelta. |
| AXI GPIO configurado para las entradas requeridas. | L | GPIO dual de entradas y salida creado; asignación física pendiente. |
| Interfaces AXI4-Lite disponibles. | P | IP con interfaces disponibles; faltan interconexión y direcciones. |
| Señales de reloj y reset correctamente configuradas. | H | Conexiones de reloj y reset del sistema por completar. |
| Señales de interrupción identificadas. | P | Se definió Timer→INTC y handler; falta cableado y configuración BSP. |
| Configuración validada en Vivado. | P | El BD de prueba no está validado como sistema completo; conserva BD 5-339. |
| Archivos y scripts incorporados al repositorio. | P | Scripts entregables preparados; plataforma integrada pendiente. |
| Cambios revisados mediante Pull Request. | H | PR revisado por el equipo pendiente. |

#### Issue #12: Implementar firmware, interrupciones y regulador PR

Fuente: [Issue #12](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/12), abierto al momento de la revisión.

| Criterio de aceptación | Estado | Evidencia o falta concreta |
|---|---|---|
| Aplicación en C creada y compilada correctamente. | P | Bibliotecas y pruebas compiladas; falta aplicación main, BSP y ELF final. |
| Firmware ejecutándose sobre MicroBlaze V. | H | No se ejecutó firmware en MicroBlaze V. |
| Periféricos inicializados correctamente. | P | Inicialización portable implementada; no están conectados todos los periféricos. |
| AXI Timer configurado para ejecución periódica. | P | Registros Timer y cálculo de TLR probados con modelo; falta ejecución física. |
| Interrupciones funcionando correctamente. | H | Despachador/INTC y período real sin demostrar. |
| Regulador PR discreto implementado. | L | PR de segundo orden implementado y probado con candidato y planta sintética. |
| Coeficientes y estados internos documentados. | D | Coeficientes, unidades, ecuación y estados explicados en el planteamiento y esta guía. |
| Lectura de muestras ADC/XADC funcional. | P | Lectura coherente probada con registros simulados; adquisición física pendiente. |
| Escritura de la referencia PWM mediante AXI4-Lite. | P | Formato Q1.15 y escritura comprobados con mock; falta PWM integrado. |
| Saturación de la salida implementada. | L | Límite y cuantización implementados; política provisional FAULT al saturar. |
| Período de ejecución del control verificado. | H | No existe medición del período, WCET ni jitter en FPGA. |
| Resultados y evidencias incorporados al repositorio. | L | Pruebas locales y resultados reproducibles disponibles. |
| Cambios revisados mediante Pull Request. | H | Revisión mediante PR pendiente. |

#### Issue #15: Implementar adquisición, buffer y exportación UART

Fuente: [Issue #15](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/15), abierto al momento de la revisión.

| Criterio de aceptación | Estado | Evidencia o falta concreta |
|---|---|---|
| Adquisición ADC/XADC funcional desde MicroBlaze V. | H | Falta enlazar ADC y ejecutar desde MicroBlaze V. |
| Muestras válidas obtenidas correctamente. | P | Validez probada con datos sintéticos; falta adquisición del sensor. |
| Buffer de almacenamiento implementado. | L | Dos bancos circulares implementados y ejercitados con wrap. |
| Capacidad del buffer definida y documentada. | D | 2048 registros por banco; 16384 bytes de arreglos más metadatos. |
| Variables del sistema registradas correctamente. | P | Se almacenan raw e iref; faltan salida PR y modulación solicitadas. |
| Correspondencia temporal de las muestras verificada. | P | Continuidad verificada en el modelo; falta correspondencia temporal física. |
| Comunicación UART funcional. | H | No se probó una transmisión por AXI UART Lite/USB de la placa. |
| Exportación de muestras implementada. | P | Serializador y receptor implementados; falta FIFO UART y evento de botón. |
| Formato de transmisión documentado. | D | Cabecera, registros, flags, longitudes, CRC y CSV documentados. |
| Integridad de los datos exportados verificada. | L | Trama C decodificada en Python; corrupción, truncado y CRC comprobados. |
| Operaciones de registro compatibles con el período de control. | P | Intercambio O(1) y envío fragmentado probados; falta medición temporal ISR. |
| Sin accesos fuera de los límites del buffer. | L | Índices acotados y pruebas de lleno/wrap; no es una prueba formal exhaustiva. |
| Resultados y evidencias incorporados al repositorio. | L | Código, pruebas y resultados locales preparados para la rama de trabajo. |
| Cambios revisados mediante Pull Request. | H | PR revisado pendiente. |

#### Issue #18: Documentar firmware e integración del sistema

Fuente: [Issue #18](https://github.com/IE-TDD-EL3313/Proyecto_4/issues/18), abierto al momento de la revisión.

| Criterio de aceptación | Estado | Evidencia o falta concreta |
|---|---|---|
| Arquitectura del firmware documentada. | D | Arquitectura y separación entre núcleo, runtime y drivers descritas. |
| Diagrama de flujo principal incluido. | P | Diagrama de flujo de diseño incluido; main de placa aún no está implementado. |
| Configuración de MicroBlaze V y Vitis descrita. | P | Herramientas y perfil RISC-V registrados; plataforma/BSP final pendientes. |
| Configuración de interrupciones documentada. | P | Preparación y reconocimiento Timer explicados; falta INTC definitivo. |
| Rutina ISR explicada. | D | Rutina periódica y caminos de falla explicados por pasos. |
| Período de control documentado y verificado. | P | Período propuesto documentado; falta verificación física. |
| Regulador PR discreto explicado matemáticamente. | D | Estructura amortiguada, Tustin y ecuación discreta explicados. |
| Ecuación en diferencias y coeficientes incluidos. | D | Coeficientes candidatos y estados incluidos; falta validación final PEDP. |
| Implementación del algoritmo de control documentada. | D | Funciones del algoritmo y comprobaciones descritas. |
| Adquisición ADC/XADC descrita. | P | Wrapper y contrato descritos; configuración XADC física pendiente. |
| Sincronización PWM-ADC/XADC documentada. | P | Sincronización diseñada y ejercitada con modelo; falta integración PWM. |
| Comunicación AXI4-Lite con el PWM explicada. | D | Offsets, coherencia, signo Q1.15 y contrato MMIO explicados. |
| Buffer de almacenamiento documentado. | D | Organización, propietario y congelación de bancos documentados. |
| Utilización de memoria justificada. | P | Costo de arreglos y objetos calculado; falta linker map final y stack. |
| Exportación UART documentada. | P | Protocolo y envío acotado descritos; UART física aún no validada. |
| Formato de transmisión incluido. | D | Formato binario v1 y conversión CSV incluidos. |
| Resultados de ejecución presentados. | P | Hay ejecución local y compilación cruzada; falta ejecución del procesador real. |
| Evidencias de integración incorporadas. | H | Evidencias de integración física pendientes. |
| Información coherente con la implementación final. | P | Documentación coherente con el avance; todavía no hay implementación final integrada. |
| Contenido integrado al informe del proyecto. | P | Explicación técnica en el planteamiento y esta guía; falta consolidación del informe final. |
| Cambios revisados mediante Pull Request. | H | Revisión por Pull Request pendiente. |

### 2.3 Orden para completar la implementación

1. Confirmar sensor, canal y acondicionamiento para Nexys 4; acordar mapa y plataforma con el responsable de MicroBlaze V.
2. Integrar XADC y wrapper, con reset coordinado, DCLK/ADCCLK y ready justificados. Verificar conversiones antes de cerrar el lazo.
3. Conectar Timer, INTC, UART y GPIO en el BD real y ejecutar `validate_bd_design`. Generar XSA y BSP de esa plataforma.
4. Implementar con PWM el arranque sincronizado y bloqueo/liberación C4; confirmar registros, límites y tiempos.
5. Crear main y adaptadores MMIO/BSP, calibrar la corriente y validar coeficientes y saturación con PEDP.
6. Conectar el servicio UART y el botón. Ampliar el histórico para incluir salida PR y modulación del Issue #15, revisando versión de protocolo y memoria antes de cambiar el formato.
7. Medir ISR total, latencia, período, jitter y funcionamiento durante exportación. Completar evidencia FPGA, memoria final e integración con potencia.
8. Revisar el código mediante PR y consolidar el informe con los resultados efectivamente obtenidos.

La revisión de código y las pruebas actuales permiten publicar un avance de implementación en la rama personal. La entrega funcional completa permanece pendiente de los pasos anteriores.

## 3. Revisión de integración del 15 de noviembre de 2026

### 3.1 Versiones y conclusión

Se descargaron las referencias remotas y se contrastaron las siguientes versiones:

| Componente | Rama y commit revisados |
|---|---|
| ADC y firmware, incluida esta guía | `feat/adc-firmware`, `56f17d8` antes de agregar esta revisión |
| Entrega de ADC y firmware sin documentos personales | `integracion/adc-firmware`, `4609569` |
| Proyecto compartido más reciente | `origin/develop`, `98b0758` |
| Arquitectura MicroBlaze V | `origin/feat/arquitectura-microblaze-v`, `a3010b4` |
| PWM | `origin/feat/pwm-rtl`, `9deebc5`; su RTL y testbench coinciden con los de `develop` |

**La integración funcional todavía está incompleta.** En `develop` están el proyecto base de MicroBlaze V y el RTL del PWM; los commits de implementación del ADC y firmware aún están en sus ramas de trabajo. El Block Design tampoco instancia el PWM ni el wrapper ADC personalizado. La conservación del diseño escrito en `planteamiento.md` no significa que los periféricos estén conectados y ejecutándose.

La revisión no efectuó merges, commits, push ni cambios en el código compartido. Esta sección pertenece a la guía personal y no se agrega a la rama de integración.

### 3.2 Compatibilidades comprobadas

- El dispositivo del proyecto es `xc7a100tcsg324-1`, correspondiente a la selección de Nexys 4 utilizada en el desarrollo.
- La configuración guardada del MicroBlaze V indica 100 MHz, caché de instrucciones de 8192 bytes, multiplicación/división habilitada, sin FPU y sin caché de datos. La memoria local está mapeada con 32 KiB. Esto concuerda con la base prevista; falta el ELF y su mapa de memoria final.
- UARTLite está configurado para 115200 baudios, ocho bits y sin paridad, compatible con la trama y el receptor. La comunicación física aún debe comprobarse.
- Timer está conectado a la entrada 0 del concatenador de interrupciones; UART está en la entrada 1. El registro del handler y las prioridades efectivas corresponden al BSP pendiente.
- Los offsets PWM `CTRL_STATUS=0x00`, `DUTY_REF=0x04` y `DEADTIME=0x08`, el signo Q1.15 y el límite de ±31129 coinciden con el driver. Se añadieron `gates_en` en bit 4, `gates_on` en bit 5, `fault` en bit 6 y `SYNC_PHASE=0x0C`.
- Con `gates_en=0`, la portadora y el trigger siguen funcionando sin pulsos de compuerta. El trigger tiene cuatro ciclos. A 20 kHz, la portadora y las muestras de la prueba conjunta conservan un período de 5000 ciclos.
- Los pines de reloj, reset, UART y VAUX3 del archivo `system_pins.xdc` coinciden con las asignaciones del `Nexys-4-Master.xdc` incluido en el repositorio. Esto comprueba consistencia entre archivos, no el acondicionamiento analógico ni una medición física.

### 3.3 Pendientes que impiden dar por integrada la aplicación

#### A. Conectar los periféricos personalizados y conservar el contrato ADC

El BD contiene `xadc_wiz_0` con `INTERFACE_SELECTION=Enable_AXI`, `TIMING_MODE=Continuous` y canal `VAUXP3_VAUXN3`. Su segmento AXI está en `0x44A00000`. No contiene `xadc_axi_lite` ni `pwm_controller_axi`, y no existe una conexión de `generateout0` al PWM ni del trigger PWM al wrapper.

El driver `tdd_adc_read` no es un driver del XADC Wizard AXI: espera los registros propios `DATA`, `SEQ`, `TIME`, `STATUS` y `TICKS`. **No debe conectarse directamente a la base del Wizard suponiendo que los offsets significan lo mismo.** Para conservar la adquisición implementada, se debe integrar el wrapper AXI personalizado y un XADC con interfaz DRP y conversión por evento, conectando CONVST/EOC/DRDY/DO, reloj, reset y disponibilidad. Elegir otra arquitectura exige adaptar el firmware y volver a demostrar coherencia y fase.

También hay que unificar el canal: el wrapper conserva `DRP_ADDRESS=0x10` como valor candidato para VAUX0, mientras la plataforma selecciona VAUX3. La selección y la dirección DRP correspondientes deben configurarse juntas y verificarse con una señal conocida.

Responsabilidad de coordinación: plataforma y adquisición. Primero se valida una muestra conocida; después se habilita el lazo.

#### B. Evitar muestras anteriores a la sincronización

La secuencia descrita de habilitar portadora/trigger antes de arrancar Timer permite que el ADC publique una muestra antes de establecer la fase. `VALID` permanece activo hasta reset; deshabilitar el ADC no borra una publicación anterior.

Se reprodujeron dos secuencias con el RTL real del PWM y de adquisición, respuestas ADC sintéticas y pulsos que representan el Timer. Las tuplas generadas se procesaron después mediante el núcleo C real:

| Secuencia | Primera atención | Atenciones posteriores |
|---|---|---|
| ADC habilitado antes del primer `sync_start_i` | Muestra válida con edad de 5019 ticks; `TDD_FAULT_TIME` | El supervisor permanece en FAULT |
| ADC inicialmente sin muestra y habilitado en la sincronización | No hay muestra válida; permanece en WARMUP | Muestras con edad de 1019 ticks y separación de 5000 ticks; pasa a RUNNING |

La ventana de edad de la prueba fue de 900 a 1200 ticks; los 20 ciclos de demora de servicio y los tiempos ADC del modelo son sintéticos. No representan latencia o conversión medida en FPGA.

Se debe definir un arranque que descarte las muestras previas y tome la primera línea de base después de sincronizar. Una opción es mantener la adquisición deshabilitada y sin `VALID` hasta el evento inicial; otra es modificar WARMUP para descartar publicaciones anteriores a una marca de sincronización, conservando un tiempo máximo de espera. Cambiar únicamente ENABLE no invalida una muestra ya publicada. Este paso debe probarse también después de una falla y un rearme.

#### C. Actualizar C4 por la precarga del bootstrap

El PWM nuevo libera su estado `gates_on` en el **pico** y mantiene los MOSFET inferiores durante `PRECHARGE_CYC=2000` ciclos antes de iniciar la modulación normal. El firmware y el texto anterior todavía usan `release_at_valley` y liberación en el valle. No es el mismo contrato.

La prueba PWM verifica el arranque con inferiores primero y ausencia de conducción simultánea en las condiciones ensayadas. El cambio por bootstrap debe incorporarse a la secuencia del firmware, con confirmación de habilitación y de fin de precarga o con una secuencia temporal equivalente comprobada. `gates_on` por sí solo no significa que la precarga haya terminado. El estado RUNNING y la primera actuación deben describir el comportamiento real.

Además, `sync_load = sync_start_i && !sync_prev && !gates_on_o` acepta nuevos flancos mientras las compuertas están bloqueadas: **hay detección de flanco, pero no armado de un solo uso**. La prueba existente conserva la fase con pulsos periódicos ya alineados; una prueba adicional confirmó que otro pulso fuera de fase vuelve a cargar 1000 sin rearme. Para cumplir el contrato original se necesita una lógica de armado externo o interno. Si se decide conservar la resincronización periódica durante WARMUP, hay que documentar ese contrato y probar sus efectos en las muestras.

`SYNC_PHASE` también admite escrituras sin comprobar que el PWM esté detenido. El adaptador debe impedir cambios durante operación y validar el rango, o el hardware debe aplicar esa restricción. No se declara incumplida la fase estable a 20 kHz por estas observaciones; se señalan las condiciones que aún no están garantizadas por hardware.

Responsabilidad de coordinación: PWM, plataforma y firmware. Faltan las implementaciones de placa de bloqueo, solicitud de liberación y armado; no basta con pasar `c4_confirmed=true`.

#### D. Acordar la tabla de frecuencias y el período representable

El PWM cambió a `00=10 kHz`, `01≈15.0015 kHz`, `10=20 kHz` y `11=20 kHz`. El texto anterior del ADC/firmware todavía enumera 5/10/20/40 kHz.

Para `N=3333`, el período real es **6666 ciclos**, de modo que la frecuencia es `100000000/6666` Hz. El núcleo C, la captura y el receptor v1 exigen `fs_hz * period_ticks = 100000000` con `fs_hz` entero. No existe un valor entero que cumpla esa condición para 6666 ciclos. Se comprobó ejecutando `tdd_config_valid` que la pareja `15000,6666` se rechaza; `tdd_timer_prepare(15000)` también la rechaza porque la división no es exacta.

El caso común de 20 kHz y 5000 ciclos sí es compatible. Para añadir la opción de aproximadamente 15 kHz se debe representar el tiempo por período y reloj —o mediante una frecuencia racional— en Timer, fase de referencia, PR, captura y receptor. Deben revisarse los coeficientes y la compatibilidad/versionado de la trama. Redondear solamente el campo Hz no corrige la desigualdad ni garantiza la fase.

#### E. Completar BSP, GPIO y coherencia documental

- GPIO está configurado como entrada/salida dual de ocho bits, pero sus señales no aparecen conectadas a puertos externos en el BD revisado. Faltan botón de captura, salidas de instrumentación y sus constraints.
- Timer tiene `enable_timer2=0`; el diseño escrito y el helper anterior proponen dos contadores, y el driver escribe cero en `0x10` durante la preparación. Se debe unificar la selección y el driver. Esto es una diferencia de configuración; no se demostró una falla de bus por esa escritura.
- Faltan `main`, adaptadores MMIO con acceso ordenado y finalización de escrituras, registro INTC, envío UART y rearme de placa. Los callbacks sintéticos de las pruebas no sustituyen esos componentes.
- En `develop`, las secciones del PWM describen pico/precarga y frecuencias nuevas, pero los apartados 8.3.3, 9.1, 9.2 y varias pruebas del plan conservan el contrato anterior. La tabla C4 indica implementación PWM y reconoce que falta simulación con Timer; no debe interpretarse como aceptación del sistema completo.
- La previsualización de merge de `planteamiento.md` no produjo conflictos de texto. **Eso no resuelve estas contradicciones de contenido.**
- Los valores de potencia añadidos al documento compartido —24 V, R=5 Ω, L=30 mH— difieren del modelo sintético anterior —12 V, 5 Ω, 2.2 mH—. Las pruebas antiguas siguen siendo pruebas de software; no validan esos nuevos parámetros ni autorizan reutilizar sus coeficientes sin contrastarlos con PEDP.

### 3.4 Pruebas y evidencia local

| Comprobación ejecutada | Resultado y alcance |
|---|---|
| `scripts/test_local.py` de la rama personal | 284 comprobaciones RTL, 64508 de núcleo/captura, 70 de runtime y 11 pruebas Python, todas satisfactorias; los contadores incluyen iteraciones |
| Testbench PWM de `develop` | 72 comprobaciones satisfactorias, cero fallos |
| Conexión PWM → adquisición y reproducción de tuplas en C | Dos escenarios, diez comprobaciones RTL por escenario; confirma período, publicación, bloqueo, pico y aceptación de una segunda sincronización. El escenario de muestra previa reproduce la falla de arranque esperada |
| Inspección del BD y de los XCI | Confirma parámetros y ausencia de conexiones personalizadas, generateout y GPIO externo |
| Vivado 2026.1 sobre copia temporal del proyecto remoto | `open_bd_design`, `validate_bd_design -force`, `report_ip_status` y `make_wrapper` completados; valida la plataforma base, no la integración pendiente |
| Previsualización de merge del Markdown | Sin conflictos textuales; requiere consolidación semántica |

Los archivos de revisión están en [build/revision_integracion_20261115/](../../build/revision_integracion_20261115/), excluidos de Git: snapshots remotos, `pwm_sim.log`, `integration_probe.log`, CSV de tuplas, `tb_integration_probe.sv`, `probe_control.c`, `bd_connectivity_audit.json` y previsualización del documento. Las pruebas propias conservan su log en [build/host/verificacion.txt](../../build/host/verificacion.txt).

El log `vivado_validation.log` contiene la marca `REVIEW_VALIDATION_COMPLETED`. Se generó el wrapper que no venía versionado. La apertura advierte sobre archivos de runs y carpetas generadas ausentes, coherente con su exclusión de Git; la generación también informa diferencia de ancho entre las direcciones de 32 bits del bloque de memoria y las de 15 bits del controlador BRAM, conectando los bits inferiores. Estas advertencias deben revisarse al cerrar la plataforma y su memoria; no se ejecutaron síntesis ni implementación completa para resolverlas. Los intentos iniciales de ejecutar Vivado con permisos restringidos no llegaron a validar el diseño; el resultado indicado corresponde a la ejecución posterior completada con el usuario local.

La prueba conjunta usa componentes RTL reales y control C real, pero ADC/Timer sintéticos y procesamiento de tuplas fuera del simulador. No ejecuta MicroBlaze, AXI interconectado, modelo analógico AMD ni potencia. No se midieron WCET, jitter o hardware, y no se realizó implementación completa.

### 3.5 Orden recomendado de integración

1. Llevar el código técnico de `integracion/adc-firmware` a la integración compartida, manteniendo fuera esta guía personal. Consolidar el documento con ambos trabajos.
2. Acordar C4 actualizado: sincronización armada, bloqueo independiente, pico y precarga, estados de habilitación y rearme.
3. Usar inicialmente el caso común de 20 kHz. Preparar aparte el cambio necesario para 6666 ciclos.
4. Integrar PWM y wrapper ADC con el XADC en modo compatible, unificar VAUX3/dirección DRP y conectar Timer, interrupciones y GPIO.
5. Implementar el arranque sin muestras anteriores a la sincronización y los adaptadores BSP. Validar la captura con una entrada conocida y comprobar fallas/rearme con potencia deshabilitada.
6. Generar XSA/BSP, enlazar la aplicación y revisar mapa de memoria; ejecutar la simulación conjunta y las mediciones temporales antes del ensayo de potencia.

## 4. Actualización de la implementación del 16 de noviembre

Las explicaciones anteriores describen la versión inicial y la revisión que motivó la integración. Para defender el código actualizado hay que considerar estos cambios:

- Se incorporó `develop` en la rama personal. La entrega técnica se prepara en `integracion/adc-firmware`, sin este directorio personal; el PR se dirige a `develop` y requiere revisión.
- PWM tiene armado de un solo uso: `sync_arm` bit7, `sync_done` bit8, `precharge` bit9 y `power_ready` bit10. Pulsos posteriores se ignoran y SYNC_PHASE solo se escribe con PWM detenido. El ADC recibe el trigger únicamente después de sincronizar.
- `xadc_startup_guard.sv` coordina el reset físico y disponibilidad. ADC_CTRL bit3 solicita reset y ADC_READY está en 0x1C. Limpiar TIMEOUT por W1C no recupera una conversión; se utiliza reset coordinado.
- El runtime descarta publicaciones anteriores a su marca de arranque. El primer dato válido solicita compuertas con mando cero; PRECHARGE mantiene el PR sin acumular. La fase comienza cuando se confirma power_ready. Perder esa confirmación durante RUNNING causa FAULT.
- `request_gates` y `gates_ready` sustituyen la operación anterior llamada `release_at_valley`. La concesión se hace en el pico y respeta la precarga de los inferiores.
- `period_ticks` define Timer, verificación de muestras y referencia de 50 Hz. `fs_hz=0` permite el período racional de 6666 ciclos. Timer usa prepare_ticks; no se redondea el reloj a 15000 Hz.
- La trama v2 mantiene 48 bytes de cabecera, pero transmite reloj de ticks en lugar de frecuencia entera. Python calcula fs=reloj/período. Los casos enteros mantienen v1.
- `vitis/bsp/main.c` proporciona la entrada SDT y los adaptadores de INTC/MMIO/UART/botones. `board_config.h` permanece con confirmaciones en cero y parámetros pendientes: el modo inicial transmite códigos raw con las cuatro compuertas bloqueadas.
- La reconstrucción integrada se hace con `scripts/build_nexys4.tcl`; el proyecto base conservado en vivado no incluye por sí solo estas conexiones. El procedimiento, mapa de direcciones, pinout y alcance completo están en la sección 16 de planteamiento.md.

La validación del BD y las pruebas locales son satisfactorias. El objeto de la entrada SDT compila contra cabeceras AMD con configuraciones de prueba; todavía no existe un ELF enlazado al BSP real ni evidencia de ejecución en la Nexys. Faltan medidas físicas, calibración, PR definitivo, protección, límite bootstrap y registrar salida PR/modulación. Los resultados actualizados y los comandos de reproducción están en resultados/adc_firmware/verificacion_local.md.
