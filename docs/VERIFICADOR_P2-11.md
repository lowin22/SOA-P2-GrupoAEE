# P2-11 — Verificador automatizado de mensajes y cierre

## Objetivo

Comprobar la integridad de los mensajes intercambiados entre productores y consumidores, así como verificar que los registros coincidan con el resumen generado al cerrar la instancia.

## Verificaciones implementadas

- Estructura y formato de los archivos CSV.
- Identidad y contenido de los mensajes.
- Detección de mensajes duplicados o faltantes.
- Comprobación del orden FIFO.
- Continuidad de las secuencias globales.
- Incorporación y retirada de participantes.
- Conservación de mensajes y ausencia de pendientes.
- Correspondencia con el resumen final.
- Coherencia básica de las marcas temporales del cierre.

## Evidencias de validación

Se ejecutó la suite completa del proyecto mediante `make -j4 test`, obteniendo los siguientes resultados:

- 69 pruebas Python aprobadas.
- 264 casos del analizador de argumentos aprobados.
- Código de salida general: 0.

También se verificó una corrida experimental real de la sesión `414cce0564`, con 800 mensajes aceptados, 800 extraídos y código de salida 0.

## Limitación conocida

La verificación permite comprobar la conservación de mensajes y la coherencia básica del resumen final. Sin embargo, no demuestra por sí sola que todas las inserciones ocurrieron antes de la transición atómica de `OPEN` a `CLOSING`.

Esta comprobación requiere evidencia adicional del protocolo de cierre.

## Estado

Implementación y pruebas funcionales realizadas. Pendiente de revisión final de los criterios del P2-11 y aprobación del equipo para su integración.
