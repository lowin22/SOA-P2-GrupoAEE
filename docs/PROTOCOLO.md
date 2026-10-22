# Protocolo de ciclo de vida y buffer (P2-01)

**Estado:** propuesta implementable, lista para revisión conjunta.

Se usa `L(fd)` para el bloqueo de lectura del descriptor, `X(fd)` para el de
escritura y `M` para el mutex compartido. Los bloqueos de descriptor son bloqueos
de registro `fcntl` sobre todo el objeto. Todos los errores liberan en orden inverso
solo los recursos adquiridos por la operación.

## 1. Predicados protegidos por `M`

```text
PUEDE_REGISTRAR = state == OPEN && id_valido && !used[role][id]
PUEDE_INSERTAR  = state == OPEN && occupancy < capacity
PUEDE_EXTRAER   = occupancy > 0
FIN_CONSUMIDOR  = state != OPEN && occupancy == 0
CIERRE_VALIDO   = active_consumers > 0
                  || (active_producers == 0 && occupancy == 0)
DRENADO         = occupancy == 0
                  && active_producers == 0
                  && active_consumers == 0
```

`finisher_claimed`, `state`, contadores, índices, IDs y métricas solo se consultan
con `M`. Las comprobaciones de publicación y ABI se hacen con un bloqueo de
descriptor, antes de tocar `M`.

## 2. Creación y publicación

```text
create(name, capacity):
  validar nombre, capacidad y overflow del tamaño
  fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600)
  si EEXIST: retornar EXISTS sin abrir ni modificar el objeto

  adquirir X(fd), reintentando si EINTR
  # El objeto continúa con tamaño cero hasta poseer X.
  ftruncate(fd, mapped_size)
  map = mmap(mapped_size, READ | WRITE, SHARED, fd)
  inicializar campos escalares privados para publicación

  crear atributos mutex/cond
  configurar pthread_mutexattr_setpshared(PROCESS_SHARED)
  configurar pthread_condattr_setpshared(PROCESS_SHARED)
  configurar pthread_condattr_setclock(CLOCK_MONOTONIC)
  inicializar M y cada condición, registrando cuáles existen

  bajo X(fd): escribir magic, ABI, tamaño y publication = READY al final
  msync de la cabecera no es requisito de persistencia; X ordena los accesos
  munmap
  liberar X y close(fd)
  retornar OK

fallo:
  destruir en orden inverso solo primitivas inicializadas
  munmap si existe
  shm_unlink(name) porque esta llamada obtuvo O_EXCL
  liberar X, close(fd)
  retornar error
```

Existe una ventana mínima entre `shm_open` y `X(fd)`. Si un conector gana esa
carrera, obtiene `L(fd)`, observa mediante `fstat` que el tamaño sigue en cero y
retorna `NOT_READY`; jamás mapea ni lee una primitiva parcial. Si el creador obtuvo
primero `X`, el conector duerme en el kernel hasta la publicación. No hay sondeo.

## 3. Apertura, mapeo y reserva de vida

```text
open(name, out):
  *out = NULL
  fd = shm_open(name, O_RDWR, 0)
  adquirir L(fd), reintentando si EINTR
  fstat(fd)
  si tamaño == 0: liberar L, close, retornar NOT_READY
  si tamaño menor que cabecera: liberar L, close, retornar ABI_ERROR
  mapear primero la cabecera fija
  validar publication == READY, magic, ABI, capacidad y mapped_size
  validar mapped_size == tamaño del objeto y cálculo sin overflow
  ampliar/recrear mmap al tamaño validado
  volver a validar cabecera bajo L
  construir handle privado que conserva fd + L + map
  *out = handle; retornar OK
```

El bloqueo `L` es la reserva entre apertura, mapeo, registro, operación y retirada.
Aunque el proceso aún no figure en `active_*`, el finalizador no puede obtener `X`
ni destruir primitivas. El handle mantiene exactamente un descriptor abierto.

Una instancia `READY` puede estar en `CLOSING`; abrirla es seguro, pero registro y
segundo cierre se rechazan al tomar `M`. Esto evita leer `state` sin sincronización.

## 4. Registro y retirada

```text
register(handle, role, id):
  exigir handle con L y sin registro local
  lock(M)
  si state != OPEN: unlock; retornar CLOSED
  si id fuera de 1..1024: unlock; retornar INVALID_ID
  si used[role][id]: unlock; retornar DUPLICATE_ID
  used[role][id] = true                 # punto de linealización
  active[role]++
  inicializar registro histórico y primer inicio si corresponde
  capturar evento JOIN
  broadcast(state_changed)
  unlock(M)
```

```text
unregister(handle):
  exigir registro local activo
  lock(M)
  active[role]--                        # punto de linealización
  marcar registro inactivo, sin limpiar used
  capturar evento LEAVE
  broadcast(state_changed)
  unlock(M)
  marcar handle privado como no registrado
```

El ID queda consumido aun si el proceso no llegó a insertar o extraer. El handle no
puede cerrarse mientras permanezca registrado.

## 5. Inserción

El productor crea identidad y payload en memoria privada y pausa antes del intento.

```text
put(handle, draft, event):
  lock(M), midiendo adquisición
  while state == OPEN && occupancy == capacity:
      medir pthread_cond_wait(not_full, M) como espera por espacio
  if state != OPEN:
      unlock(M); retornar CLOSED         # nada fue aceptado
  si next_enqueue_seq desborda: unlock; retornar OVERFLOW

  msg = draft
  msg.enqueue_seq = next_enqueue_seq++
  msg.enqueue_time_ns = monotonic_now()
  messages[tail] = msg
  tail = (tail + 1) % capacity
  occupancy++
  accepted_count++                      # punto de linealización
  capturar evento ENQUEUE y ocupación
  signal(not_empty)
  unlock(M)
  retornar OK
```

La comprobación de `state` y el incremento de ocupación comparten `M`. Por ello una
inserción concurrente queda completamente antes del cambio a `CLOSING` o se rechaza
completamente después, aunque el productor hubiese despertado por espacio.

## 6. Extracción y procesamiento

```text
take(handle, out_message, event):
  lock(M), midiendo adquisición
  while occupancy == 0 && state == OPEN:
      medir pthread_cond_wait(not_empty, M) como espera por datos
  if occupancy == 0 && state != OPEN:
      unlock(M); retornar CLOSED
  si next_dequeue_seq desborda: unlock; retornar OVERFLOW

  msg = messages[head]
  head = (head + 1) % capacity
  occupancy--
  dequeue_seq = next_dequeue_seq++
  extracted_count++                     # punto de linealización
  last_dequeue_ns = monotonic_now()
  capturar evento DEQUEUE y ocupación
  signal(not_full)
  if occupancy == 0: broadcast(state_changed)
  unlock(M)

  copiar msg a memoria privada
  escribir evento fuera de M
  procesar/pausar fuera de M, reiniciando nanosleep tras EINTR
  retornar OK solo al completar procesamiento
```

El consumidor permanece registrado durante el procesamiento. Así el finalizador no
puede observar `DRENADO` ni destruir hasta que ese mensaje retirado termine y el
consumidor se retire.

## 7. Pausa interrumpible del productor

La condición `state_changed` fue creada con reloj monotónico.

```text
producer_pause(handle, deadline):
  lock(M)
  while state == OPEN && monotonic_now() < deadline:
      rc = cond_timedwait(state_changed, M, deadline)
      si rc distinto de 0, ETIMEDOUT o EINTR: error
  result = OK si state == OPEN; CLOSED en otro caso
  unlock(M)
  retornar result
```

El cierre hace `broadcast(state_changed)`. Un despertar espurio solo vuelve a
comprobar estado y plazo. La generación exponencial y el cálculo del plazo son
privados y nunca mantienen `M`.

## 8. Reclamación y punto de corte del cierre

```text
begin_shutdown(handle):
  lock(M)
  if state != OPEN: unlock; retornar CLOSED
  if finisher_claimed: unlock; retornar FINISHER_EXISTS
  finisher_claimed = true                # excluye al segundo finalizador

  if active_consumers == 0
       && (active_producers > 0 || occupancy > 0):
      finisher_claimed = false
      broadcast(state_changed)
      unlock(M)
      retornar NO_CONSUMER               # state continúa OPEN

  state = CLOSING                        # punto de corte
  closing_start_ns = monotonic_now()
  capturar evento de cambio
  broadcast(not_full)
  broadcast(not_empty)
  broadcast(state_changed)

  while !DRENADO:
      cond_wait(state_changed, M)
  state = CLOSED
  capturar contadores, tiempos y registros en snapshot privado
  broadcast(not_full, not_empty, state_changed)
  unlock(M)
```

Después del corte:

- productores en `not_full` despiertan y salen sin insertar;
- productores en pausa despiertan y salen sin intentar insertar;
- consumidores drenan mientras `occupancy > 0`;
- consumidores en vacío despiertan, observan cierre y se retiran;
- cada retirada despierta al finalizador mediante `state_changed`.

La validación de consumidor y la transición se hacen en una sola posesión de `M`.
Si la validación falla no cambia el estado y otro intento posterior es válido.

## 9. Destrucción y reutilización del nombre

Tras alcanzar `CLOSED`, el finalizador posee un snapshot privado y sigue conservando
su `L(fd)`:

```text
destroy(handle):
  munmap? NO todavía: se necesita marcar y destruir con el mapeo
  liberar L(fd)
  adquirir X(fd)                         # espera conexiones no registradas

  # Nadie con capacidad de tocar pthread mantiene L.
  publication = RETIRED
  destruir condiciones en orden inverso
  destruir M al final
  shm_unlink(name)
  munmap
  liberar X y close(fd)
  anular handle
  registrar monotonic_now como fin de liberación en snapshot privado
  retornar snapshot
```

Una conexión que abrió el descriptor antes de `shm_unlink` puede estar esperando
`L`. Cuando el finalizador libera `X`, esa conexión obtiene `L`, mapea el inode aún
vivo gracias a su descriptor, observa `RETIRED` y sale **sin tocar el mutex ya
destruido**. Una apertura posterior al unlink falla, salvo que el nombre ya designe
una instancia nueva; en tal caso se conecta solo al inode nuevo.

El `shm_unlink` ocurre una única vez porque solo quien fijó `finisher_claimed` llega
a esta fase. La escritura del CSV de resumen usa el snapshot después de liberar los
recursos, por lo que no prolonga ni accede al mapeo.

## 10. Intercalados exigidos

### Conexión durante inicialización

1. Si el creador ya tiene `X`, el conector espera bloqueado en `L`.
2. Si el conector gana la ventana inicial, ve tamaño cero y retorna `NOT_READY`.
3. Nunca interpreta bytes parciales ni espera sobre una condición no inicializada.

### Conexión durante cierre/destrucción

1. Si obtiene `L` antes de la fase exclusiva, puede mapear; al tomar `M` observa
   `CLOSING/CLOSED`, no se registra y libera su handle.
2. Su `L` impide que el finalizador destruya mientras hace lo anterior.
3. Si espera detrás de `X`, después observa `RETIRED` y no toca pthread.

### Productor obtiene espacio al comenzar cierre

El consumidor señala `not_full`, pero el productor debe readquirir `M`. Quien tome
`M` primero fija el orden: si el productor inserta primero, el mensaje es aceptado y
se drena; si el finalizador cambia primero a `CLOSING`, el productor lo rechaza.

### Consumidor procesando con buffer vacío

Aunque `occupancy == 0`, el consumidor continúa en `active_consumers`. El
finalizador espera. Tras terminar su pausa privada, el consumidor intenta otra
extracción, recibe `CLOSED`, se retira y permite `DRENADO`.

### Dos finalizadores

Ambos pueden abrir con `L`, pero serializan en `M`. El primero fija
`finisher_claimed`; el segundo retorna `FINISHER_EXISTS` (o `CLOSED` si llega tras
el corte), libera `L` y nunca destruye.

### Recreación del nombre

El primer cierre destruye bajo `X` y hace unlink. Un nuevo `creator` obtiene un inode
distinto mediante `O_EXCL`; bloqueos y mapeos rezagados del inode anterior no pueden
afectarlo.

## 11. Invariantes y argumentos de corrección

### Ocupación acotada

Inicialmente `occupancy = 0`. Solo `put`, con `occupancy < capacity`, la incrementa;
solo `take`, con `occupancy > 0`, la decrementa. Ambas operaciones poseen `M`.
Por inducción, `0 <= occupancy <= capacity`.

### Conservación y extracción única

Una aceptación escribe una celda antes de avanzar `tail` y `accepted_count`. Una
extracción lee la celda en `head` antes de liberarla. `M` excluye accesos parciales.
Siempre:

```text
accepted_count = extracted_count + occupancy
```

Durante cierre no crece `accepted_count`; al llegar a `CLOSED`, `occupancy = 0`,
por lo que aceptados y extraídos coinciden.

### FIFO

Cada aceptación recibe un `enqueue_seq` creciente y ocupa `tail`. Cada extracción
toma exclusivamente `head` y recibe un `dequeue_seq` creciente. El avance circular
de ambos índices preserva el orden de aceptación. El procesamiento posterior puede
terminar en otro orden sin cambiar el FIFO de extracción.

### Ausencia de deadlock cooperante

Solo existe un mutex y ninguna espera de condición lo conserva. Productores hacen
verdadero `occupancy > 0`; consumidores hacen verdadero `occupancy < capacity`; el
cierre despierta todas las condiciones. El finalizador no solicita `X` mientras
posee `M` y solo lo hace cuando los registrados se retiraron. Con procesos
cooperantes y planificación con progreso no hay ciclo de espera. POSIX no garantiza
equidad ni espera acotada entre trabajadores, y este protocolo tampoco.

### Vida segura

Todo uso de una primitiva requiere `L`; destruirlas requiere `X`. Estos bloqueos son
incompatibles. Los conectores posteriores a la destrucción solo leen `publication`
bajo un nuevo `L`. Por tanto ninguna primitiva se usa simultáneamente con o después
de su destrucción.

## 12. Casos de revisión manual

| Caso | Quién avanza | Quién espera/rechaza | Protección decisiva |
|---|---|---|---|
| creación en curso | creador con `X` | conector espera o recibe `NOT_READY` | tamaño cero + `X/L` |
| lleno sin consumidores | nadie modifica buffer | productores en `not_full` | predicado con `M` |
| vacío sin productores | nadie modifica buffer | consumidores en `not_empty` | predicado con `M` |
| cierre sin consumidor | trabajadores continúan en `OPEN` | finalizador recibe error | validación bajo `M` |
| cierre activo | consumidores drenan | productores salen; finalizador espera | estado y condiciones |
| conexión tardía | finalizador continúa al liberar su `L` | conector rechaza y libera `L` | `M` antes de `X` |
| dos finalizadores | primero reclamado | segundo recibe error | `finisher_claimed` bajo `M` |
| recreación | creador del inode nuevo | rezagados solo ven inode viejo | unlink + `O_EXCL` |

## 13. Aprobación

- [ ] Recorrido conjunto realizado.
- [ ] Aprobación de Adrián registrada en el PR.
- [ ] Aprobación de Esteban registrada en el PR.

Hasta registrar ambas revisiones, este documento es una propuesta de P2-01 y no una
afirmación de aprobación del equipo.
