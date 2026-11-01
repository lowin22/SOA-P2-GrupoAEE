# P2-03 — Matriz de requisitos, pruebas y evidencia

**Estado:** suite funcional implementada y parcialmente validada; se conservan pendientes de cobertura y revisión.
**Base:** enunciado oficial del Proyecto 2 e interfaces acordadas en P2-01.
**Responsables:** cada autor prueba su módulo; Esteban integra suite y verificador en P2-11/P2-12; Edwin cubre carreras de cierre (P2-13); Adrián cubre estrés y sanitizers (P2-14).

## 1. Reglas generales

- `make test` ejecutará pruebas automáticas y devolverá código distinto de cero ante cualquier incumplimiento. **El target está implementado y se ha ejecutado correctamente**; la cobertura se acredita únicamente para los casos comprobados.
- Los timeouts en scripts detectan falta de progreso; jamás sustituyen los bloqueos POSIX del sistema.
- Cada caso tendrá: identificador, comando reproducible, configuración, salida esperada, salida obtenida, código de salida y ubicación de CSV/evidencia. Al implementar, registrar en esta tabla los resultados reales.
- Usar nombres POSIX únicos por prueba (p. ej., `/soa_p2_test_<identificador>`), limpiar únicamente recursos creados por el caso y no interferir con otras instancias.
- Para verificar mensajes, cruzar los CSV por `(producer_id,sequence)`, `enqueue_seq`, `dequeue_seq` y `payload_hex`; no inferir FIFO del orden de las líneas de distintos archivos.
- Los procesos `producer` y `consumer` deberán lanzarse como **procesos distintos**, nunca sustituirse por hilos.
- Los escenarios implementados se ejecutan mediante `make test`; los demás permanecen como requisitos pendientes de comprobación.

## 2. Matriz obligatoria

| ID | Escenario del profesor | Configuración / pasos | Evidencia y condición de aprobación | Issues |
|---|---|---|---|---|
| T01 | Validación de argumentos | Argumentos ausentes, capacidad 0/1025, IDs fuera de rango/duplicados, media no numérica, NaN/inf, overflow, nombre inválido y ausencia de instancia | Error legible y código != 0; sin alterar otras instancias | 04, 12 |
| T02 | FIFO básico | Capacidad 8; 1 productor, 1 consumidor; 1000 mensajes | 1000 aceptados y extraídos; identidades/payload coinciden; secuencias globales continuas y FIFO | 06, 09, 11, 12 |
| T03 | Concurrencia 4/4 | Capacidad 8; 4 productores × 1000 mensajes; 4 consumidores | 4000 aceptados/extraídos, pendientes 0, sin duplicados y FIFO global | 06, 09, 11, 12 |
| T04 | Capacidad mínima | Capacidad 1; 4 productores y 4 consumidores con cuotas finitas | Progreso, ocupación siempre 0..1 y conservación | 06, 11, 12 |
| T05 | Buffer lleno | Iniciar productores sin consumidor hasta llenar; luego agregar consumidores | Los productores quedan bloqueados, reanudan con extracción; sin sondeo | 06, 12 |
| T06 | Buffer vacío | Iniciar consumidores sin productores; luego agregar productores | Consumidores bloquean y despiertan al recibir datos; no terminan por vacío temporal | 06, 12 |
| T07 | Cierre durante actividad | Productores con mensajes pendientes y bloqueados; finisher durante carga | Corte `OPEN→CLOSING`, sin nuevas aceptaciones posteriores, drenaje, retirada y liberación | 10, 11, 13 |
| T08 | Cierre en vacío | Consumidores esperando sin datos; repetir con instancia sin trabajadores | Todos despiertan y terminan; resumen y limpieza correctos | 10, 13 |
| T09 | Cierre sin consumidor | Productor activo o datos pendientes, ningún consumidor | Finisher rechaza y conserva OPEN; tras registrar consumidor, cierre exitoso | 10, 13 |
| T10 | Aislamiento y reutilización | Dos nombres simultáneos, creación duplicada, cierre y recreación | Sin interferencias; creación duplicada no reinicializa; nombre reutilizable | 05, 12 |
| T11 | Carreras de ciclo de vida | Registro concurrente con cierre, conector en apertura, dos finishers | Sin doble destrucción ni uso después de destruir; segundo finalizador rechazado | 07, 10, 13 |
| T12 | Estrés 32/32 | 32 productores, 32 consumidores, pausas 0 y cuotas finitas | Todos los datos verificados, sin deadlock ni fallos de memoria | 11, 12, 14 |
| T13 | ASan y UBSan | Construcción instrumentada y ejecución de casos entregados | Informes sin errores sin corregir; documentar límites: no prueban ausencia de carreras | 14 |
| T14 | Semillas y pausas | `mean-ms=0`, valores límite, mismo `seed`, pausas exponenciales | Sin `log(0)`; secuencia local reproducible, sin prometer mismo intercalado | 04, 09, 12 |
| T15 | Integridad de CSV | Datos controlados y archivos con alteraciones intencionales | Verificador rechaza pérdidas, duplicados, payload alterado, saltos o inversiones FIFO | 03, 08, 11 |
| T16 | Registros históricos | ID usado, retirado y vuelto a registrar; ejecución con productor sin cuota | ID se rechaza en nueva alta; métricas persisten para resumen después de LEAVE | 07, 08, 12 |
| T17 | Reconstrucción nativa | Copia limpia; `make clean && make && make test` | GCC C17, sin rutas absolutas ni artefactos requeridos preexistentes; suite completa en entrega | 02, 12, 17 |

## 3. Experimento obligatorio: 45 corridas

**Combinaciones:** capacidades 1, 8 y 64, y tasas (media productor / consumidor) de 2/8 ms, 8/2 ms y 5/5 ms. Cinco repeticiones por combinación, siempre 4 productores, 4 consumidores y 200 mensajes por productor. Total: `3 × 3 × 5 = 45` ejecuciones; 800 mensajes por ejecución. El cierre se solicita **después de terminar todos los productores**.

| Escenario | Media productor | Media consumidor | Capacidades | Repeticiones |
|---|---:|---:|---|---:|
| Producción más rápida | 2 ms | 8 ms | 1, 8, 64 | 5 cada una |
| Consumo más rápido | 8 ms | 2 ms | 1, 8, 64 | 5 cada una |
| Tasas equilibradas | 5 ms | 5 ms | 1, 8, 64 | 5 cada una |

Por cada corrida conservar: nombres de instancia, parámetros, semillas individuales, versiones, fecha de ejecución, ruta de logs, resultado del verificador, resumen y código de salida. Rechazar de la agregación las corridas con verificación fallida.

**Análisis:** media y desviación estándar **muestral** entre cinco repeticiones del throughput y latencia media; informar p95 y esperas por escenario/capacidad, al menos dos gráficas, CPU, Linux/kernel, GCC y posible costo de escritura CSV. La semilla reproduce pausas por proceso, no el intercalado del sistema operativo.

## 4. Evidencia automatizada esperada del verificador (P2-11)

1. Leer CSV con cabecera exacta y validar tipos, versión, cantidad/longitud de campos y nombres de evento.
2. Exigir identidad `(producer_id, sequence)` única en inserciones y extracciones; detectar duplicaciones y faltantes.
3. Validar `payload_hex` contra la carga reproducible, además de comparar inserción y extracción.
4. Ordenar `ENQUEUE` por `enqueue_seq` y `DEQUEUE` por `dequeue_seq` y exigir que ambos recorridos produzcan idéntica sucesión de identidades y contenidos; no ordenar por timestamp de diferentes procesos.
5. Revisar continuidad sin saltos de ambas secuencias globales, `occupancy` dentro de 0..capacity, y `accepted=extracted` con `pending=0` tras cierre normal.
6. Validar eventos JOIN/LEAVE, cambio de estado mediante evidencia acordada y ausencia de IDs duplicados durante toda la instancia.
7. Devolver código distinto de cero ante log ausente, truncado, errores de datos o incumplimiento; diagnosticar ID y secuencia cuando sea posible.

## 5. Registro de resultados

| Caso | Comando / script ejecutado | Resultado esperado | Resultado obtenido | Evidencia | Estado |
|---|---|---|---|---|---|
| Suite general | `make -j4 test` | Las pruebas disponibles terminan sin errores | 71 pruebas Python, 264 casos del parser y suites C aprobadas | Secciones 7, 9 y 10 | Aprobado |
| T12 | `make test-stress` | 32/32 con conservación y cierre completo | 32000 mensajes verificados por capacidad | Sección 10 | Aprobado |
| T13 | `make test-sanitizers ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` | Suite y estrés sin errores de ASan o UBSan | Ambas configuraciones aprobadas, detección de fugas desactivada en ASan | Sección 10 | Aprobado sin comprobación de fugas |
| T07–T11 (P2-13) | `make -j4 test-shutdown` | Corte ordenado, conservación, rechazo tardío y destrucción única | Todos los intercalados terminaron con código 0 | `tests/test_shutdown.c` y tabla de la sección 8 | Aprobado |
| 45 corridas | Pendiente | 45 corridas verificadas | No ejecutado | Pendiente | Pendiente |

## 6. Revisiones pendientes

- [ ] Validar con Edwin y Adrián que los casos observan el protocolo real de conexión y cierre.
- [ ] Aprobar el contrato de eventos y auditar el cierre sin inventar valores fuera de los enums.
- [ ] Confirmar los nombres definitivos de scripts y archivos antes de automatizarlos.
- [ ] Incorporar los comandos concretos y resultados reales conforme los demás Issues avancen.
- [ ] Dirigir el Pull Request del P2-03 a `develop` y no a `main`.

## 7. Evidencias de ejecución del P2-12 — 31 de octubre de 2026

### Suite general

Comando: `make -j4 test`, protegido externamente por un límite de 120 segundos.

- Pruebas Python: 65 aprobadas.
- Casos del analizador de argumentos: 264 aprobados.
- Código de salida: 0.
- La ejecución no alcanzó el límite de tiempo establecido.

Las pruebas Python incluyen verificaciones de mensajes duplicados,
mensajes faltantes, integridad de contenido y orden FIFO.

### Integración de productores y consumidores

Comando: `make test-buffer`.

| Escenario | Capacidad | Productores | Consumidores | Mensajes | Resultado |
|---|---:|---:|---:|---:|---|
| T02 | 8 | 1 | 1 | 1000 | Aprobado |
| T03 | 8 | 4 | 4 | 4000 | Aprobado |
| T04 | 1 | 4 | 4 | 4000 | Aprobado |

Estos escenarios utilizan el ejecutable auxiliar de integración,
la memoria compartida real y el verificador CSV.

### Bloqueo y reanudación

Se incorporaron dos pruebas funcionales en `tests/test_workers.py`:

- T05: productor reanudado cuando un consumidor libera espacio.
- T06: consumidor reanudado cuando un productor incorpora mensajes.

Ambas pruebas utilizan los ejecutables `producer` y `consumer`,
comprueban contadores y validan sus registros CSV.

### Protección contra bloqueos

El objetivo `test-buffer` incorpora un límite de 90 segundos,
con terminación forzada adicional tras cinco segundos.

Este control evita esperas indefinidas durante las pruebas;
no sustituye la sincronización del programa.

### Aspectos todavía pendientes de revisión

- Comprobar específicamente que un error inesperado del verificador
  provoque un fallo de `make test`.
- Confirmar la cobertura completa de los escenarios T01-T17.
- Ejecutar y documentar una reconstrucción desde un entorno limpio,
  según T17.
- La evidencia de estrés y sanitizers está en la sección 10

Las cifras anteriores describen ejecuciones observadas y no implican
que todos los escenarios de la matriz estén completos.

## 8. Intercalados y carreras de cierre — P2-13

El objetivo `make test-shutdown`, incluido por `make test`, usa procesos
independientes y sincronización explícita con tuberías y condiciones compartidas.
Las esperas por estado pertenecen únicamente a la instrumentación de prueba: toman
el mutex y esperan `state_changed`; no agregan sondeo al protocolo de producción.

| Intercalado buscado | Cómo se fuerza | Evidencia automática |
|---|---|---|
| Buffer lleno y productor bloqueado | Capacidad 1; el productor publica el primer mensaje y avisa por tubería antes de intentar el segundo | En `OPEN`: ocupación y aceptados valen 1 y el productor sigue activo. En `CLOSING`, el segundo `put` retorna `SOA_CLOSED`; el consumidor drena el único mensaje y el resumen conserva `accepted=extracted=1`, `pending=0`. |
| Productor en pausa | `test_close_interrupts_producer_pause` usa una pausa sorteada larga y el caso C solicita un cierre real antes de la primera inserción | El productor despierta por cierre, el resumen conserva `accepted=extracted=pending=0` y los CSV funcionales pasan `scripts/verificar_csv.py`. |
| Consumidor procesando con buffer vacío | El consumidor extrae el único mensaje, confirma ocupación 0 y avisa antes de iniciar procesamiento privado | El finalizador real espera `LEAVE`; el resumen conserva `accepted=extracted=1`, `pending=0`, demostrando que no confundió buffer vacío con consumidor retirado. |
| Consumidor esperando y cierre sin trabajadores | Un consumidor bloquea en `take`; otro caso cierra una instancia recién creada | El consumidor recibe `SOA_CLOSED`; ambos cierres terminan, retiran el nombre y producen snapshot válido. |
| Rechazo sin consumidor y recuperación | Se deja un mensaje pendiente sin consumidores y se intenta cerrar; después se registra uno | Primer resultado `SOA_NO_CONSUMER` con handle válido y estado `OPEN`; el consumidor drena y el cierre posterior conserva 1/1/0. |
| Conexión aún no registrada | Una conexión abre antes del corte y espera una orden por tubería; un consumidor mantiene `CLOSING` observable | El registro tardío retorna `SOA_CLOSED`; el finalizador espera que ambas conexiones liberen sus bloqueos de vida antes de destruir. |
| Dos finalizadores | El conductor reclama el cierre mientras un consumidor impide completar el drenaje; entonces se lanza el segundo | Solo el conductor obtiene `SOA_OK`; el segundo recibe `SOA_CLOSED` o `SOA_FINISHER_EXISTS`, sin segunda destrucción. |
| Reutilización del nombre | Se termina una instancia vacía, se comprueba `SOA_NOT_FOUND` y se crea otra con el mismo nombre | La segunda instancia se crea y termina correctamente. |

Los casos revisan estados y contadores solo bajo el mutex compartido. Los `waitpid`
confirman retirada normal de cada proceso antes de considerar aprobado el caso; el
finalizador no destruye las primitivas mientras quede una conexión o participante.
La comprobación del resumen se complementa con
`tests/test_verificador_cierre.py`, que valida conservación y rechazo de resúmenes
con pérdidas, extracciones extra, pendientes o cierre temporalmente inválido.

Comandos reproducibles:

```bash
make clean
make -j4 test-shutdown
make test-python
make -j4 test
```

Ejecución observada el 31 de octubre de 2026:

- `test-shutdown` aprobó desde una compilación limpia y en 50 repeticiones
  consecutivas.
- La suite integrada aprobó: 71 pruebas Python, 264 casos del parser y los
  escenarios C/integración, con código final 0.
- `git diff --check` no reportó errores de formato.

## 9. Propagación de errores y reconstrucción limpia

**Propagación de errores:**

Se ejecutó `make test-python` con un intérprete temporal que
finalizaba intencionalmente con código 17.

Resultado observado:
- Código del intérprete simulado: 17.
- Código de salida de `make`: 2.
- Resultado: correcto; el objetivo no ocultó el fallo.

Esta comprobación valida la propagación de errores de `test-python`,
pero no constituye una inyección directa de un CSV incorrecto.

**Reconstrucción limpia (T17):**

Se ejecutaron, en orden:

1. `make clean`
2. `make -j4`
3. `make -j4 test`, con límite externo de 120 segundos.

Resultados:
- Compilación completada correctamente.
- 65 pruebas Python aprobadas.
- 264 casos del analizador aprobados.
- Código de salida final: 0.

La reconstrucción se realizó en el directorio de trabajo actual,
después de eliminar los artefactos compilados. No se utilizó
una copia independiente del repositorio.

## 10. Estrés y sanitizers

Validación sobre d27bf8b con los cambios locales de estrés y sanitizers

make test incluye el estrés con 32 productores y 32 consumidores reales
Cada productor envía 1000 mensajes con media cero y semilla igual a su ID
Se comprueban capacidades 1 y 8 con el verificador CSV y el resumen del finisher real

Pasaron la suite nativa, ASan y UBSan
Cada configuración verificó 32000 mensajes por capacidad sin pérdidas ni duplicados y con cierre completo
La suite incluyó 71 pruebas Python, 264 casos del parser y las suites C existentes

Los [comandos, entorno y límites](ESTRES.md) permiten repetir la validación
Los CSV y diagnósticos de cada ejecución se generan en results/runs y están ignorados por Git
Los resultados sanitizados no se usan para medir rendimiento
