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


## Validación final del P2-11

Se amplió el verificador para auditar los registros respecto
al inicio del cierre y la liberación de la instancia.

### Comprobaciones realizadas

- Identidad y contenido de mensajes.
- Detección de duplicados, pérdidas y extracciones inválidas.
- Continuidad de secuencias y orden FIFO.
- Incorporación y retirada de participantes.
- Inserciones permitidas únicamente durante OPEN.
- Ausencia de incorporaciones posteriores al corte.
- Extracciones y retiradas permitidas durante CLOSING.
- Validación de las 14 columnas del resumen.
- Conservación de mensajes y ausencia de pendientes.
- Coherencia de marcas temporales, capacidad y rendimiento.

### Evidencias

- 78 pruebas Python aprobadas.
- 264 casos del analizador de argumentos aprobados.
- Corrida real con 800 mensajes aceptados y extraídos.
- Rechazo de un resumen con duración alterada.
- Rechazo de una inserción simulada posterior al cierre.
- Código de salida 0 para registros válidos.
- Código de salida distinto de cero para los errores inyectados.

### Alcance de la comprobación

La comparación de marcas temporales permite auditar los
registros. La exclusión entre inserciones y transición
OPEN a CLOSING depende del mutex del protocolo en C.

Las pruebas verifican los escenarios ejecutados; no
demuestran formalmente todas las intercalaciones posibles.

La detección de archivos de participantes completamente
ausentes requiere disponer de un manifiesto independiente.
