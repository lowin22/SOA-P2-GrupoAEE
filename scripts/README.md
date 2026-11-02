# Scripts de verificación y experimentación

Los scripts usan Python 3 y rutas relativas al repositorio. Propagan errores mediante códigos de salida distintos de cero.

| Script | Propósito |
|---|---|
| `verificar_csv.py` | Cruza eventos, identidades, payload, FIFO, ciclo de vida y resumen. |
| `metricas.py` | Calcula throughput, latencia media y percentil 95. |
| `preparar_experimentos.py` | Genera y valida la matriz de 45 configuraciones. |
| `ejecutar_experimentos.py` | Planifica o ejecuta corridas y conserva manifiestos locales. |
| `analizar_experimentos.py` | Consolida resultados individuales y agrupados. |
| `graficar_experimentos.py` | Genera las gráficas desde los CSV consolidados. |

## Verificación de una ejecución

```bash
python3 scripts/verificar_csv.py \
  results/demo/p1.csv results/demo/c1.csv \
  --validar-ciclo --summary results/demo/resumen.csv
```

También puede utilizarse `--manifiesto ARCHIVO.json` para exigir el conjunto exacto de registros de una instancia.

## Experimentos

```bash
# Mostrar los comandos de las 45 corridas sin ejecutarlas
python3 scripts/ejecutar_experimentos.py

# Ejecutar una corrida o la matriz completa
python3 scripts/ejecutar_experimentos.py --ejecutar --corrida 1
python3 scripts/ejecutar_experimentos.py --ejecutar --todas

# Consolidar y graficar una sesión conservada localmente
python3 scripts/analizar_experimentos.py --sesion ID_DE_SESION
python3 scripts/graficar_experimentos.py --sesion ID_DE_SESION
```

La matriz fuente está en `results/experimentos/configuraciones.csv`. Cada corrida usa un nombre POSIX exclusivo, inicia cuatro consumidores y cuatro productores, espera la terminación de todos los productores y luego ejecuta `finisher`. Una corrida se incorpora al análisis únicamente si sus procesos, CSV y resumen son válidos.

Los resultados consolidados están en `results/experimentos/analisis_final/`. Los registros masivos de cada sesión permanecen fuera de Git.
