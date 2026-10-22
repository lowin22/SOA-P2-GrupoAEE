# Arquitectura e interfaces (P2-01)

**Estado:** propuesta implementable, lista para revisión de Adrián y Esteban.

Este documento fija la representación, los límites entre módulos y la propiedad de
recursos. El algoritmo completo y sus puntos de linealización están en
[`PROTOCOLO.md`](PROTOCOLO.md).

## 1. Decisiones principales

- Cada instancia usa **un objeto de memoria compartida POSIX**, cuyo nombre es el
  valor validado de `--name`.
- El objeto contiene una cabecera fija y un arreglo flexible de mensajes. No hay
  punteros compartidos; cada proceso conserva su propia dirección de `mmap`.
- Se usan un mutex y variables de condición POSIX con
  `PTHREAD_PROCESS_SHARED`. Las condiciones usan `CLOCK_MONOTONIC`.
- Un bloqueo de registro `fcntl` sobre el descriptor de `shm_open` protege la
  publicación y la vida del mapeo. Cada proceso conectado mantiene un bloqueo de
  lectura desde antes de `mmap` hasta después de dejar de usar las primitivas. El
  creador y la fase destructiva mantienen un bloqueo de escritura.
- Un único mutex compartido protege estado, buffer, participantes y acumuladores.
  Es una granularidad deliberadamente simple: las pausas, el procesamiento y la
  escritura de CSV ocurren fuera de él.
- Los IDs válidos son `1..1024`, por rol. Se conserva un bit `used` por ID durante
  toda la instancia, aunque el participante se retire. Esto admite más de los 32
  participantes simultáneos exigidos sin memoria dinámica compartida.
- No se intenta recuperar una instancia si un proceso muere dentro de una región
  crítica. El enunciado limita las garantías a procesos cooperantes y cierre normal.

## 2. Dos niveles de estado

La disponibilidad de la representación y el estado operativo son distintos:

### Estado de publicación (`publication`)

| Valor | Significado |
|---|---|
| `INITIALIZING` | El objeto existe, pero no puede mapearse como instancia. En la práctica tiene tamaño cero mientras el creador obtiene el bloqueo exclusivo. |
| `READY` | Tamaño, cabecera y primitivas están completamente inicializados. |
| `RETIRED` | Las primitivas ya no se pueden tocar. Solo se lee bajo el bloqueo de descriptor. |

`publication` solo cambia mientras se mantiene el bloqueo exclusivo del descriptor.
No se usa para sondeo ni como sustituto del mutex.

### Estado operativo (`state`)

| Estado | Admisión | Inserción | Extracción |
|---|---|---|---|
| `OPEN` | Sí | Sí | Sí; espera si está vacío |
| `CLOSING` | No | No | Sí, hasta drenar; termina si está vacío |
| `CLOSED` | No | No | No |

`state` se lee y modifica únicamente con el mutex compartido. La transición es
monótona: `OPEN -> CLOSING -> CLOSED`.

## 3. Representación compartida

La implementación interna utilizará tipos enteros de ancho fijo y comprobará
overflow al calcular el tamaño del mapeo:

```text
SharedInstance
  magic, abi_version, mapped_size, capacity, publication
  pthread_mutex_t mutex
  pthread_cond_t not_full
  pthread_cond_t not_empty
  pthread_cond_t state_changed
  state, finisher_claimed
  head, tail, occupancy
  next_enqueue_seq, next_dequeue_seq
  accepted_count, extracted_count
  active_producers, active_consumers
  used_producer_ids[1025 bits]
  used_consumer_ids[1025 bits]
  participant_metrics[2][1025]
  first_worker_start_ns, last_dequeue_ns, closing_start_ns
  messages[capacity]
```

Los arreglos usan índices e IDs, nunca direcciones. `mapped_size` se calcula como
`offsetof(SharedInstance, messages) + capacity * sizeof(soa_message_t)`. La
capacidad válida es `1..1024`. Se validan `magic`, versión, tamaño, capacidad y
coherencia con `fstat` antes de tocar una primitiva pthread.

Los contadores y secuencias son de 64 bits. Antes de incrementarlos se detecta el
valor máximo; el error no modifica el buffer. Los índices circulares satisfacen
`0 <= head, tail < capacity`.

### Mensaje fijo

`soa_message_t`, declarado en `include/soa/message.h`, contiene:

- `producer_id` (`uint32_t`);
- `sequence` (`uint64_t`), iniciada en 1 por cada productor;
- `enqueue_seq` (`uint64_t`), asignada al aceptar el mensaje;
- `enqueue_time_ns` (`uint64_t`, `CLOCK_MONOTONIC`);
- `payload[32]`.

El campo de alineación `reserved` declarado en la cabecera se escribe siempre como
cero, de modo que la representación completa también sea determinista.

El payload no es texto ni contiene punteros. Para que sea verificable, el productor
llena los 32 bytes en cuatro bloques de 64 bits obtenidos con SplitMix64 a partir
de `(producer_id << 32) XOR sequence`; se aplica la función cuatro veces y se
serializa cada resultado en orden little-endian. La semilla de las pausas no afecta
el contenido. El consumidor o verificador puede reconstruirlo únicamente con la
identidad. El algoritmo se centralizará en el módulo de mensajes.

No se insertan mensajes de control: el cierre se representa mediante `state` y
despertares de condiciones, por lo que las métricas cuentan solo datos.

## 4. Estado privado por proceso

El tipo opaco `soa_instance_t` mantiene, en memoria privada:

- descriptor de `shm_open` abierto con `O_RDWR`;
- hecho de poseer el bloqueo de lectura del descriptor;
- dirección y longitud de su propio `mmap`;
- rol, ID y condición de registro del proceso;
- escritor CSV y generador pseudoaleatorio, cuando correspondan.

No se comparte `FILE *`, direcciones de mapeo, estado de `rand`, rutas ni memoria
asignada con `malloc`.

## 5. Bloqueos y orden obligatorio

Hay dos niveles, siempre en este orden:

1. **bloqueo de descriptor** de lectura o escritura;
2. **mutex compartido**.

Nunca se intenta adquirir el bloqueo de descriptor mientras se posee el mutex. Un
participante adquiere una vez el bloqueo de lectura antes de mapear y lo conserva;
las operaciones públicas adquieren internamente el mutex. El llamador nunca recibe
el mutex tomado ni llama una operación pública mientras posee uno.

La implementación tendrá exactamente un descriptor del objeto por proceso y no lo
duplicará. Esto es necesario porque los bloqueos POSIX de registro son propiedad del
proceso y cerrar otro descriptor del mismo objeto podría liberarlos. Los programas
son ejecutables independientes; no se soporta continuar el protocolo tras `fork`.

| Operación | Bloqueo adquirido internamente | Puede bloquear |
|---|---|---|
| crear/publicar | descriptor escritura | mientras termina una conexión que ganó la carrera inicial |
| abrir/mapear | descriptor lectura | durante creación o destrucción |
| registrar/retirar | mutex | solo adquisición del mutex |
| insertar | mutex + `not_full` | buffer lleno mientras siga `OPEN` |
| extraer | mutex + `not_empty` | buffer vacío mientras siga `OPEN` |
| pausa interrumpible | mutex + `state_changed` con plazo | hasta plazo o cierre |
| solicitar cierre | mutex | condiciones de participantes y drenaje |
| destruir | descriptor escritura, sin mutex activo | conexiones rezagadas |

`pthread_cond_wait` y `pthread_cond_timedwait` liberan el mutex durante la espera y
siempre se invocan en un ciclo que vuelve a comprobar el predicado.

## 6. Interfaces de módulos

Las cabeceras bajo `include/soa/` son contratos; P2-05 a P2-10 aportarán sus
implementaciones. Todas devuelven un `soa_status_t`; no escriben en consola ni CSV.

### `instance.h` — Edwin

- `soa_instance_create`: creación exclusiva, cálculo de tamaño, inicialización y
  publicación. No deja un handle conectado al retornar.
- `soa_instance_open`: toma el bloqueo de lectura, valida y mapea. Puede abrir una
  instancia en cierre para permitir que el llamador reciba `SOA_CLOSED` con
  seguridad; no concede admisión.
- `soa_instance_close`: solo es válido sin registro; desmonta, cierra y libera el
  handle. No hace `shm_unlink`.

El handle entregado pertenece al llamador. Ante fallo, `*out` queda en `NULL`.

### `participants.h` — Edwin

- `soa_participant_register`: solo en `OPEN`, ID válido, nunca usado y cupo
  disponible. El bit histórico y el contador activo cambian en la misma región
  crítica.
- `soa_participant_unregister`: exactamente una vez después de dejar de operar. El
  bit histórico no se limpia.

### `buffer.h` — Adrián

- `soa_buffer_put`: espera espacio; acepta y asigna tiempo/secuencia bajo mutex, o
  retorna `SOA_CLOSED` sin insertar.
- `soa_buffer_take`: espera datos; extrae FIFO y asigna `dequeue_seq`, o retorna
  `SOA_CLOSED` cuando está en cierre y vacío.

Ambas copian a un `soa_event_t` privado los datos requeridos por logging. No
conservan referencias a una celda del buffer tras liberar el mutex.

### `timing.h` — Adrián

- `soa_producer_pause`: espera con `state_changed` y reloj monotónico; retorna
  `SOA_CLOSED` inmediatamente al observar cierre.
- El procesamiento del consumidor usa espera privada reiniciable ante `EINTR`; no
  se interrumpe por cierre porque el mensaje extraído debe terminar de procesarse.

### `shutdown.h` — Edwin

- `soa_shutdown_run`: reclama el único finalizador, valida la precondición, fija el
  punto de corte, espera drenaje/retirada, toma una instantánea privada, destruye y
  anula el handle. La escritura del resumen ocurre después mediante P2-03.

La instantánea devuelta no contiene direcciones compartidas y sobrevive a `munmap`.

## 7. Eventos y métricas

Las operaciones protegidas capturan en un valor privado: rol, ID, tipo de evento,
tiempo monotónico, identidad del mensaje, `enqueue_seq`, `dequeue_seq` y ocupación
resultante. El archivo se escribe después de liberar el mutex.

Cada registro histórico acumula espera por espacio/datos y adquisición del mutex.
El intervalo medido alrededor de `pthread_cond_wait` incluye la readquisición y se
declarará así en el contrato CSV. El primer inicio de operación y la última
extracción se actualizan bajo mutex. Los detalles de columnas y percentil pertenecen
a P2-03 y no alteran esta representación.

## 8. Propiedad y limpieza

| Recurso | Creador | Propietario durante uso | Quién lo retira |
|---|---|---|---|
| nombre y objeto shm | `creator` | instancia | único `finisher` |
| descriptor/mapeo | cada proceso | ese proceso | ese proceso; el finalizador consume el suyo |
| mutex/condiciones | `creator` | instancia | finalizador, con bloqueo exclusivo |
| slot e ID histórico | registro | instancia | slot activo al retirarse; historial al destruir |
| mensaje en una celda | inserción | instancia hasta extracción | extracción sobrescribe solo al reutilizar celda |
| evento/CSV | proceso trabajador | memoria/archivo privado | proceso trabajador |
| resumen final | finalizador | memoria privada | finalizador tras escribirlo |

En creación fallida se destruyen, en orden inverso, solo los atributos y primitivas
que esa llamada alcanzó a inicializar; luego `munmap`, `ftruncate` si aplica,
`shm_unlink` del nombre que creó y `close`. `EEXIST` jamás abre, trunca ni modifica
la instancia existente.

En una conexión fallida se liberan únicamente su mapeo, bloqueo y descriptor. Nunca
hace `shm_unlink`.

## 9. Errores

`soa_status_t` distingue argumento inválido, nombre existente, instancia ausente,
inicialización aún no publicada, ABI incompatible, cierre observado, ID inválido o
duplicado, límite, overflow, segundo finalizador, falta de consumidor y error del
sistema. `errno` se conserva internamente cuando sea útil para formar el diagnóstico
en el ejecutable, pero las capas comunes no imprimen.

Un fallo de logging ocurre después de la operación protegida: no revierte un mensaje
ya aceptado o extraído. Se reporta como error de evidencia y el proceso debe retirarse
siguiendo el protocolo.

## 10. Límites y supuestos

- Capacidad: `1..1024` mensajes.
- ID por rol: `1..1024`; como máximo 1024 IDs diferentes durante la instancia.
- Dos o más instancias se aíslan por nombre e inode de memoria compartida.
- Las secuencias y tiempos que no caben en 64 bits causan error controlado.
- Se supone GNU/Linux de 64 bits, procesos cooperantes y planificación que permite
  progreso. No se promete equidad, recuperación de `SIGKILL` ni mutex robusto.
- Una invocación que abre un nombre justo después de su recreación puede conectarse
  a la instancia nueva; nunca mezcla el inode viejo con el nuevo.

## 11. Revisión requerida

- [ ] Adrián valida buffer, pausas, predicados y API de eventos.
- [ ] Esteban valida disponibilidad de datos para CSV, resumen y verificador.
- [ ] Edwin incorpora observaciones y presenta los intercalados de `PROTOCOLO.md`.
- [ ] Adrián aprueba la versión final en el PR.
- [ ] Esteban aprueba la versión final en el PR.

La aprobación se registra en el Pull Request; no se marca anticipadamente en este
archivo.
