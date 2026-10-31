# P2-03 — Matriz de requisitos, pruebas y evidencia

**Estado:** diseño de pruebas, no evidencia de pruebas ejecutadas.
**Base:** enunciado oficial del Proyecto 2 e interfaces acordadas en P2-01.
**Responsables:** cada autor prueba su módulo; Esteban integra suite y verificador en P2-11/P2-12; Edwin cubre carreras de cierre (P2-13); Adrián cubre estrés y sanitizers (P2-14).

## 1. Reglas generales

- `make test` ejecutará pruebas automáticas y devolverá código distinto de cero ante cualquier incumplimiento. **El target actual declara que la suite aún no está implementada**; no atribuirle cobertura.
- Los timeouts en scripts detectan falta de progreso; jamás sustituyen los bloqueos POSIX del sistema.
- Cada caso tendrá: identificador, comando reproducible, configuración, salida esperada, salida obtenida, código de salida y ubicación de CSV/evidencia. Al implementar, registrar en esta tabla los resultados reales.
- Usar nombres POSIX únicos por prueba (p. ej., `/soa_p2_test_<identificador>`), limpiar únicamente recursos creados por el caso y no interferir con otras instancias.
- Para verificar mensajes, cruzar los CSV por `(producer_id,sequence)`, `enqueue_seq`, `dequeue_seq` y `payload_hex`; no inferir FIFO del orden de las líneas de distintos archivos.
- Los procesos `producer` y `consumer` deberán lanzarse como **procesos distintos**, nunca sustituirse por hilos.
- Los comandos indicados son **plantillas de orquestación pendientes de implementar** en `tests/` y `scripts/`; no representan resultados observados.

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

## 5. Registro de resultados (para completar cuando existan implementaciones)

| Caso | Comando / script ejecutado | Resultado esperado | Resultado obtenido | Evidencia | Estado |
|---|---|---|---|---|---|
| T01–T17 | Pendiente | Según matriz | No ejecutado | Pendiente | Pendiente |
| 45 corridas | Pendiente | 45 corridas verificadas | No ejecutado | Pendiente | Pendiente |

## 6. Revisiones pendientes

- [ ] Validar con Edwin y Adrián que los casos observan el protocolo real de conexión y cierre.
- [ ] Aprobar el contrato de eventos y auditar el cierre sin inventar valores fuera de los enums.
- [ ] Confirmar los nombres definitivos de scripts y archivos antes de automatizarlos.
- [ ] Incorporar los comandos concretos y resultados reales conforme los demás Issues avancen.
- [ ] Dirigir el Pull Request del P2-03 a `develop` y no a `main`.
