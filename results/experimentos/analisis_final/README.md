# Resultados experimentales P2-15

Este directorio contiene los resultados consolidados y las
gráficas de los experimentos del Proyecto 2 de Sistemas
Operativos Avanzados.

## Diseño experimental

- 3 capacidades de buffer: 1, 8 y 64 mensajes.
- 3 escenarios: producción rápida, consumo rápido y equilibrado.
- 5 repeticiones por configuración.
- 45 corridas en total.
- 4 productores y 4 consumidores por corrida.
- 800 mensajes por corrida.
- 36.000 mensajes procesados durante el experimento.

## Validación

Las 45 corridas finalizaron correctamente.

En cada corrida se verificaron los registros CSV de los
participantes y su correspondencia con el resumen final.

## Archivos

- resultados_individuales.csv: métricas por corrida.
- resultados_agrupados.csv: promedios y desviaciones.
- rendimiento.png: comparación de throughput.
- latencia_promedio.png: comparación de latencia media.
- latencia_p95.png: comparación del percentil 95.

## Trazabilidad

Sesión experimental original: 414cce0564.

Los registros individuales de productores, consumidores,
salidas de procesos y manifiestos se conservan localmente
en la carpeta de la sesión original.

Los resultados publicados provienen de ejecuciones reales.
