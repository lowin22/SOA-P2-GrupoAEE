# P2-03 — Contrato de CSV y métricas

**Estado:** propuesta para revisión de Edwin (revisor principal) y Adrián (instrumentación).
**Base:** enunciado oficial P2, `docs/ARQUITECTURA.md`, `docs/PROTOCOLO.md` e interfaces `include/soa/{types,message,events,buffer,participants,shutdown}.h` en `develop` (consultadas el 25-10-2026).
**Alcance:** especificación; la implementación corresponde principalmente a P2-08, el verificador a P2-11 y la suite a P2-12.

## 1. Principios y fuentes de verdad

- Un CSV **por proceso trabajador** (`producer` o `consumer`), usando la ruta `--log`. No confiar en el orden de escritura entre archivos: reconstruir la inserción con `enqueue_seq` y la extracción con `dequeue_seq`.
- Las operaciones sobre el buffer generan una instantánea privada `soa_event_t` **dentro de la región crítica**; la escritura al archivo ocurre **fuera del mutex**. `soa_event_t` ya ofrece `role`, `participant_id`, `kind`, `state`, `timestamp_ns`, `dequeue_seq`, `occupancy` y `message`.
- En el evento `ENQUEUE`, `message.enqueue_seq` y `message.enqueue_time_ns` se asignan al aceptar la inserción. En `DEQUEUE`, `dequeue_seq` se asigna al extraer. Para JOIN/LEAVE y cambios de estado, no hay mensaje aplicable.
- Todas las duraciones y marcas relativas al proceso emplean `CLOCK_MONOTONIC`; los tiempos de pared no se usan para orden ni latencias.
- La identidad del mensaje es el par `(producer_id, sequence)`. La carga útil son 32 bytes binarios reproducibles a partir de ese par según `docs/ARQUITECTURA.md`; se exporta como **64 dígitos hexadecimales minúsculos**, sin espacios.
- Los nombres CSV y su serialización no existen aún como implementación; **este documento propone su contrato** sin afirmar que las funciones de P2-08 están disponibles.

## 2. Formato del CSV de eventos (v1)

**Codificación:** UTF-8, separador coma, encabezado obligatorio, una fila por evento, final de línea LF, números decimales sin separador de miles, sin comillas para campos definidos en este esquema (todos controlados por el programa). Los archivos se abren por cada trabajador. Antes de retirarse se escriben y entregan los eventos de operación; `soa_participant_unregister` captura `LEAVE` en memoria privada y finaliza las métricas compartidas. Después de retornar, el proceso escribe `LEAVE`, vacía y cierra el archivo sin volver a acceder a la instancia. No escribir valores `NaN`, `inf` ni negativos en campos enteros.

**Cabecera exacta, en este orden:**

```csv
schema_version,role,participant_id,event,timestamp_ns,state,producer_id,sequence,enqueue_seq,dequeue_seq,enqueue_time_ns,occupancy,payload_hex
```

| Columna | Tipo / unidad | Significado y fuente |
|---|---|---|
| `schema_version` | entero | `1` en todas las filas; permite validar compatibilidad |
| `role` | enum texto | `producer` o `consumer`, desde `soa_role_t` |
| `participant_id` | entero 1–1024 | Identidad del proceso trabajador (`soa_event_t.participant_id`) |
| `event` | enum texto | `JOIN`, `ENQUEUE`, `DEQUEUE`, `LEAVE` o `STATE_CHANGE` (último sujeto a decisión sobre escritor del cambio) |
| `timestamp_ns` | entero uint64, ns | Marca monotónica del evento capturada bajo mutex |
| `state` | enum texto | `OPEN`, `CLOSING` o `CLOSED` capturado bajo mutex |
| `producer_id` | entero 1–1024 o vacío | Solo `ENQUEUE` / `DEQUEUE`: `message.producer_id` |
| `sequence` | uint64 o vacío | Solo mensajes: secuencia local del productor, iniciando en 1 |
| `enqueue_seq` | uint64 o vacío | Solo mensajes: posición global de aceptación, iniciando en 1 |
| `dequeue_seq` | uint64 o vacío | Solo `DEQUEUE`: posición global de extracción, iniciando en 1 |
| `enqueue_time_ns` | uint64 ns o vacío | Solo mensajes: tiempo de inserción asignado bajo mutex |
| `occupancy` | entero 0–capacity | Ocupación **después** de operación protegida; JOIN/LEAVE según captura en mutex |
| `payload_hex` | 64 caracteres hex o vacío | Solo mensajes: 32 bytes exactos para auditoría de integridad |

**Regla de campos no aplicables:** celda vacía, no `0`, `NULL` ni `N/A`. Para `JOIN`, `LEAVE` y `STATE_CHANGE`, las cinco columnas del mensaje (`producer_id`, `sequence`, `enqueue_seq`, `dequeue_seq`, `enqueue_time_ns`) y `payload_hex` están vacías; `occupancy` sigue informando la ocupación tomada bajo protección.

**Ejemplo ilustrativo (no resultado de una ejecución):**

```csv
schema_version,role,participant_id,event,timestamp_ns,state,producer_id,sequence,enqueue_seq,dequeue_seq,enqueue_time_ns,occupancy,payload_hex
1,producer,1,JOIN,1000000000,OPEN,,,,,,0,
1,producer,1,ENQUEUE,1001000000,OPEN,1,1,1,,1001000000,1,0000000000000000000000000000000000000000000000000000000000000000
1,consumer,1,DEQUEUE,1003000000,OPEN,1,1,1,1,1001000000,0,0000000000000000000000000000000000000000000000000000000000000000
```

Las dos filas de mensajes muestran la **forma del formato**, no un payload válido conforme a SplitMix64: el verificador debe rechazarlas si se usan como prueba de integridad real. Como cada participante tiene su CSV independiente, en ejecución real la fila de consumidor estaría en `c1.csv` y las filas de productor en `p1.csv`.

### Eventos de ciclo de vida y cierre

- `JOIN`: registro exitoso; ocurre bajo `OPEN` y consume un ID histórico.
- `LEAVE`: retirada confirmada; si el consumidor retiró un mensaje, sucede después de completar su procesamiento. El resumen del finalizador usa los acumuladores compartidos ya finalizados y no espera la escritura del evento privado; el verificador de logs se ejecuta después de terminar los trabajadores.
- `STATE_CHANGE`: transición `OPEN → CLOSING → CLOSED`. **Decisión pendiente**: no debe inventarse una fila de trabajador para un evento originado por `finisher`. Se recomienda registrar estos eventos en un archivo de auditoría separado del finalizador (`summary` y/o `lifecycle.csv`), conservando timestamp y estado, y ampliar si hace falta el contrato mediante un tipo de evento dedicado. `soa_event_t` contiene rol de trabajador: un registro `finisher` en la misma estructura requiere aprobación del equipo.
- Una inserción rechazada durante cierre **no genera** `ENQUEUE`, y no aumenta aceptados. Las salidas con `SOA_CLOSED` quedan auditadas con eventos de retirada y/o auditoría de cierre.

**Decisión necesaria con Edwin/Adrián:** quién emite `STATE_CHANGE`, qué ruta de auditoría se conserva y qué campos privados adicionales permiten rastrear el corte sin usar un rol inexistente en `soa_role_t`.

## 3. Resumen de finalización (`--summary`)

Un único archivo CSV del finalizador, con encabezado y una fila por instancia cerrada normalmente. El snapshot privado de `soa_shutdown_run` debe contener lo necesario para escribirlo **después** de destruir y desvincular la memoria compartida.

**Cabecera mínima propuesta:**

```csv
schema_version,instance_name,capacity,accepted,extracted,pending,first_worker_start_ns,last_dequeue_ns,closing_start_ns,release_complete_ns,throughput_msg_s,latency_mean_ns,latency_p95_ns,closing_duration_ns
```

- `accepted`, `extracted`, `pending` son totales de datos, nunca mensajes de control. Durante ejecución: `accepted = extracted + pending`; tras cierre normal: `pending = 0` y `accepted = extracted`.
- `first_worker_start_ns` se captura **en el primer registro exitoso de productor/consumidor**, bajo mutex (es la definición operativa propuesta para «primer inicio de operación»); debe validarse con el equipo antes de implementar throughput. Si un trabajador se registra pero no realiza operaciones, ese instante sigue siendo el inicio medido.
- `last_dequeue_ns` se captura bajo mutex cuando se completa la extracción del último mensaje. Se mantiene vacío si no hubo extracción.
- `closing_start_ns` se captura cuando el finalizador cambia a `CLOSING` bajo mutex.
- `release_complete_ns` se obtiene con reloj monotónico al terminar la liberación completa de recursos; no se incluye la escritura posterior del CSV.
- `throughput_msg_s = extracted / ((last_dequeue_ns-first_worker_start_ns)/10^9)` cuando `extracted > 0` y el intervalo es estrictamente positivo; de lo contrario, celda vacía (no falsear un cero).
- `latency_ns = dequeue_timestamp_ns - enqueue_time_ns` por cada mensaje. Para obtener esta duración es obligatorio conservar **la marca monotónica del DEQUEUE** (puede ser `soa_event_t.timestamp_ns`) en una muestra acumulada antes de liberar memoria; no incluye procesamiento simulado.
- `latency_mean_ns = suma(latency_ns)/N`, para `N>0`; cuando `N=0`, vacío.
- `latency_p95_ns`: ordenar las N latencias de menor a mayor y seleccionar la posición `ceil(0.95*N)` (índice basado en 1, método *nearest rank*). Cuando no hay datos, vacío.
- `closing_duration_ns = release_complete_ns - closing_start_ns`, medido hasta liberación total; validar orden monotónico y detectar underflow.

### Esperas y adquisición del mutex por proceso

Se propone un segundo CSV `worker_metrics.csv` o representación equivalente en el resumen del finalizador para **todos los IDs históricos registrados**, incluidos los que se retiraron antes del cierre:

```csv
schema_version,role,participant_id,wait_space_ns,wait_data_ns,mutex_acquire_ns,accepted,extracted
```

- `wait_space_ns`: suma de las duraciones de `pthread_cond_wait(not_full, mutex)` del productor mientras no existía espacio.
- `wait_data_ns`: suma de las duraciones de `pthread_cond_wait(not_empty, mutex)` del consumidor mientras no había datos.
- Esas duraciones incluyen la **readquisición del mutex** posterior al despertar. Se etiquetan como «espera por condición + readquisición», **no** como tiempo exclusivamente sin espacio/datos.
- `mutex_acquire_ns`: suma de intervalos que van desde antes de intentar `pthread_mutex_lock` hasta adquirirlo **en adquisiciones explícitas**. No sumar nuevamente a este contador la readquisición implícita de `cond_wait` salvo separar instrumentación y declararlo, para evitar doble conteo.
- `accepted`/`extracted`: contadores por participante desde las operaciones protegidas; los históricos sobreviven a LEAVE en memoria compartida hasta la instantánea privada del finalizador.
- Se necesitan campos de acumulación en el registro histórico compartido y una estructura privada de snapshot definida de común acuerdo con P2-07/P2-10; el encabezado `shutdown.h` actual declara el snapshot como **opaco**, por lo que esto es contrato a implementar, no acceso existente.

### Estrategia para el percentil y límites

- Para las 45 corridas prescritas: 4 productores × 200 mensajes = **800 latencias por corrida**; puede almacenarse un vector privado en el consumidor y agregarse a la estructura de medición, o una colección global acotada para ese caso. Sin embargo, los productores sin cuota pueden generar una cantidad indefinida, y un arreglo fijo no sería correcto como solución general.
- **Propuesta recomendada:** reconstruir el p95 exacto a partir de los CSV de eventos completos, una vez que todos los trabajadores cierren sus archivos, y calcular/resumir los tiempos de espera del snapshot. Antes de aceptarla, Edwin debe resolver cómo el finalizador encuentra de forma segura todas las rutas de logs (`--log` es libre) y si el requisito exige que el resumen de `finisher` se escriba sin depender de los logs de trabajadores. **No se debe asumir resuelto.** Alternativa: guardar latencias en almacenamiento privado o fichero auxiliar por instancia coordinado, con política clara de memoria y fallos.
- El diseño no debe mantener muestras ilimitadas en memoria compartida ni truncar silenciosamente percentiles; una capacidad agotada se informa claramente como error de medición, sin alterar mensajes aceptados.

## 4. Integridad, errores y concurrencia

- No escribir archivos mientras se mantiene el mutex compartido. Copiar valores completos del mensaje y evento en memoria privada antes de desbloquear.
- Comprobar `fopen`, `fprintf`, `fflush`, `fclose` y las operaciones equivalentes. Un fallo de log **no revierte** una inserción/extracción ya aceptada y debe tratarse como pérdida de evidencia, con error explícito y retirada cooperante según el protocolo.
- En el verificador, exigir ausencia de IDs duplicados por rol durante la vida de la instancia, identidades de mensaje únicas, continuidad global de `enqueue_seq` y `dequeue_seq`, payload válido y orden FIFO reconstruido por las secuencias, independientemente del orden físico de los archivos.
- Si un CSV obligatorio falta o es inválido, el verificador debe fallar (código distinto de cero); no concluir ausencia de pérdidas a partir de evidencia parcial.
- Dos instancias simultáneas: conservar separados los directorios de logs y asociar al resumen el nombre de la instancia. El argumento `--log` del trabajador debe apuntar a la ruta seleccionada para esa corrida.
- Los eventos CSV se almacenan en archivos por proceso y los acumuladores en la instancia hasta snapshot; es importante documentar cuál fuente se considera canónica si difieren por un fallo de E/S.

## 5. Casos límite y decisiones abiertas

1. **Sin mensajes:** throughput/latencia/p95 vacíos; aceptados, extraídos y pendientes son cero. El cierre puede tener duración válida.
2. **Reloj o resta inválidos:** error de medición explícito, nunca división por cero ni underflow.
3. **Productor sin `--messages`:** el formato no presupone cuota ni arreglos dimensionados por cuota.
4. **Procesamiento fuera de orden:** se verifica FIFO en DEQUEUE usando `dequeue_seq`, no por fin de procesamiento ni orden de escritura.
5. **Errores E/S:** el programa puede cerrar de forma cooperante, pero el informe no puede certificar entrega íntegra con CSV incompletos.
6. **Estado `CLOSED`:** el finalizador lo registra antes de destruir; el snapshot debe sobrevivir al `munmap`.
7. **Acordar:** auditoría de cambio de estado, fuente exacta del p95, descubrimiento de logs y formato definitivo del resumen/metricas; documentar decisiones en el PR de P2-03.

## 6. Revisión y aprobación

- [ ] Edwin valida snapshot, métricas históricas, cierre y origen de datos.
- [ ] Adrián valida en qué punto del buffer se capturan las marcas y la ocupación.
- [ ] Esteban contrasta ejemplos de eventos, fórmulas y casos límite.
- [ ] Se congela versión de columnas antes de iniciar P2-08/P2-11.
- [ ] PR del P2-03 revisado y dirigido a `develop` (no a `main`).
