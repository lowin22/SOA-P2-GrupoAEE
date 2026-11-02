# Guía de defensa individual

Cada integrante debe poder reconstruir el sistema, explicar los invariantes y recorrer un mensaje completo. Además de su área principal, preparará el siguiente módulo ajeno:

| Integrante | Módulo ajeno para explicar | Aspectos mínimos |
|---|---|---|
| Edwin | Buffer y sincronización | Predicados, `head`/`tail`, ocupación, condiciones y puntos de efecto. |
| Adrián | Métricas y verificador | Identidad, payload, secuencias globales, latencia y verificación FIFO. |
| Esteban | Memoria y cierre | Publicación, bloqueo de vida, estados, drenaje y destrucción segura. |

## Recorrido completo de un mensaje

1. `producer` abre la instancia por nombre, valida cabecera y tamaño y conserva un bloqueo de lectura sobre el descriptor.
2. Se registra con rol e ID únicos mientras la instancia está `OPEN`; el ID queda marcado históricamente.
3. Después de una pausa privada, construye `(producer_id, sequence, payload)` e intenta insertar.
4. Bajo el mutex espera en `not_full` mientras `OPEN && occupancy == capacity`.
5. Al aceptar, asigna `enqueue_seq` y `enqueue_time_ns`, escribe en `tail`, incrementa ocupación y aceptados y señala `not_empty`. Este es el punto de efecto de la inserción.
6. El evento `ENQUEUE` se copia bajo protección y se escribe en el CSV después de liberar el mutex.
7. `consumer` espera en `not_empty` mientras `OPEN && occupancy == 0`.
8. Bajo el mutex lee desde `head`, asigna `dequeue_seq`, reduce ocupación, incrementa extraídos y señala `not_full`. Este es el punto de efecto de la extracción.
9. El consumidor valida/procesa fuera de la región crítica y escribe `DEQUEUE` en su CSV.
10. El verificador cruza inserción y extracción por `(producer_id, sequence)`, comprueba payload y compara el orden de `enqueue_seq` con `dequeue_seq`.
11. Durante `CLOSING` no se aceptan nuevas inserciones; los consumidores drenan lo ya aceptado y terminan su procesamiento antes de `LEAVE`.
12. `finisher` espera ocupación y participantes en cero, obtiene después el bloqueo exclusivo del descriptor, destruye primitivas, elimina el nombre y escribe el resumen privado.

## Preguntas que cada integrante debe responder

- ¿Por qué una condición no almacena permisos y siempre se comprueba dentro de un ciclo?
- ¿Dónde toman efecto registro, inserción, extracción y cierre?
- ¿Por qué una inserción concurrente con el cierre queda antes o después del corte?
- ¿Cómo se demuestra `accepted = extracted + occupancy`?
- ¿Por qué el orden físico de los CSV no demuestra FIFO?
- ¿Qué impide destruir un mutex mientras una conexión todavía puede usarlo?
- ¿Por qué capacidad 64 aumentó latencia sin mejorar throughput en producción rápida?
- ¿Qué comprueban ASan/UBSan y qué carreras no pueden descartar?
- ¿Qué garantías requieren participantes cooperantes y planificación que permita avanzar?
- ¿Por qué el procesamiento del consumidor y la escritura de archivos ocurren fuera del mutex?

## Demostración sugerida

1. Reconstruir desde limpio con `make clean && make -j4 && make -j4 test`.
2. Ejecutar el ejemplo completo del README con capacidad 8.
3. Repetir con capacidad 1 y pausas cero.
4. Mostrar bloqueo de productor lleno y consumidor vacío.
5. Solicitar cierre durante actividad y explicar el punto de corte.
6. Ejecutar el verificador sobre CSV válidos y sobre una copia alterada.
7. Mostrar una configuración experimental y relacionarla con las gráficas.
