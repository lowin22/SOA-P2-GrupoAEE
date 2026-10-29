# Manifiesto auxiliar para P2-11 (propuesta; no contrato institucional)

Archivo JSON ubicado en el mismo directorio que los CSV de una instancia:

```json
{
  "schema_version": 1,
  "files": ["p1.csv", "c1.csv"]
}
```

Ejemplo: `python3 scripts/verificar_csv.py --manifiesto results/runs/ejemplo/manifiesto.json --validar-ciclo`.

Los nombres son rutas relativas al directorio del manifiesto. La herramienta
rechaza rutas absolutas, rutas fuera del directorio, duplicados y CSV ausentes.
Si además se proporcionan CSV posicionales, el conjunto debe coincidir exactamente
con el del manifiesto. El archivo no se genera automáticamente y **no demuestra**
que se hayan declarado todos los participantes: el inventario debe provenir de una
fuente independiente y completa (orquestador o resumen aprobado del finalizador).
No certifica por sí solo el cierre normal.
