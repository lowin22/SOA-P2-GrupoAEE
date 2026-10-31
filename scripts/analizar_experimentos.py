#!/usr/bin/env python3
"""Consolida las corridas reales del experimento P2-15."""

import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path

# Directorio principal del proyecto.
RAIZ = Path(__file__).resolve().parent.parent
BASE = RAIZ / "results/experimentos/corridas"


def guardar_csv(ruta, filas, columnas):
    # Guardar datos con encabezados estables.
    with ruta.open("w", newline="", encoding="utf-8") as archivo:
        escritor = csv.DictWriter(
            archivo, fieldnames=columnas, lineterminator="\n"
        )
        escritor.writeheader()
        escritor.writerows(filas)


def main():
    # Recibir el identificador de la sesión experimental.
    parser = argparse.ArgumentParser()
    parser.add_argument("--sesion", required=True)
    args = parser.parse_args()

    carpeta = BASE / args.sesion

    if not carpeta.is_dir():
        parser.error("No existe la sesión indicada")

    individuales = []
    grupos = defaultdict(list)

    # Leer únicamente las 45 corridas de esta sesión.
    for numero in range(1, 46):
        ruta = carpeta / f"corrida_{numero:02d}/manifiesto.json"

        if not ruta.is_file():
            raise SystemExit(f"ERROR: falta {ruta}")

        manifiesto = json.loads(ruta.read_text(encoding="utf-8"))

        # No incluir ejecuciones fallidas en el análisis.
        if manifiesto["estado"] != "VALIDA":
            raise SystemExit(f"ERROR: corrida {numero} no válida")

        if int(manifiesto["corrida"]) != numero:
            raise SystemExit(f"ERROR: identificador incorrecto: {numero}")

        config = manifiesto["configuracion"]
        resumen = manifiesto["resumen"]
        metricas = manifiesto["metricas_csv"]

        # Confirmar que cada corrida procesó exactamente 800 mensajes.
        if any(int(resumen[campo]) != esperado for campo, esperado in (
            ("accepted", 800), ("extracted", 800), ("pending", 0)
        )):
            raise SystemExit(f"ERROR: contadores en corrida {numero}")

        if metricas["accepted"] != 800 or metricas["extracted"] != 800:
            raise SystemExit(f"ERROR: discrepancia CSV en corrida {numero}")

        # Extraer valores reales, incluyendo las latencias del CSV.
        fila = {
            "corrida": numero,
            "escenario": config["escenario"],
            "capacidad": int(config["capacidad"]),
            "repeticion": int(config["repeticion"]),
            "media_productor_ms": float(config["media_productor_ms"]),
            "media_consumidor_ms": float(config["media_consumidor_ms"]),
            "accepted": 800,
            "extracted": 800,
            "throughput_msg_s": float(resumen["throughput_msg_s"]),
            "latency_mean_ns": float(metricas["latency_mean_ns"]),
            "latency_p95_ns": float(metricas["latency_p95_ns"]),
            "closing_duration_ns": int(resumen["closing_duration_ns"]),
            "estado": "VALIDA",
        }

        individuales.append(fila)

        # Agrupar las cinco repeticiones de cada configuración.
        clave = (fila["escenario"], fila["capacidad"])
        grupos[clave].append(fila)

    # Preparar un resumen para las nueve configuraciones.
    agregados = []

    for (escenario, capacidad), filas in sorted(grupos.items()):
        if len(filas) != 5:
            raise SystemExit(
                f"ERROR: {escenario}, capacidad {capacidad}: "
                f"se esperaban cinco repeticiones"
            )

        if sorted(f["repeticion"] for f in filas) != [1, 2, 3, 4, 5]:
            raise SystemExit("ERROR: repeticiones duplicadas o faltantes")

        throughput = [f["throughput_msg_s"] for f in filas]
        latencias = [f["latency_mean_ns"] for f in filas]
        p95 = [f["latency_p95_ns"] for f in filas]

        agregados.append({
            "escenario": escenario,
            "capacidad": capacidad,
            "repeticiones": len(filas),
            "throughput_promedio": statistics.mean(throughput),
            "throughput_desviacion": statistics.stdev(throughput),
            "latency_mean_ns_promedio": statistics.mean(latencias),
            "latency_mean_ns_desviacion": statistics.stdev(latencias),
            "latency_p95_ns_promedio": statistics.mean(p95),
        })

    # Guardar ambos archivos dentro de la sesión original.
    guardar_csv(
        carpeta / "resultados_individuales.csv",
        individuales,
        list(individuales[0]),
    )
    guardar_csv(
        carpeta / "resultados_agrupados.csv",
        agregados,
        list(agregados[0]),
    )

    print("OK: 45 corridas válidas consolidadas.")
    print("OK: 9 configuraciones con 5 repeticiones cada una.")
    print("\n=== RENDIMIENTO PROMEDIO POR CONFIGURACIÓN ===")

    for fila in agregados:
        print(
            f"{fila['escenario']:20s} "
            f"capacidad={fila['capacidad']:2d} "
            f"promedio={fila['throughput_promedio']:.2f} "
            f"desviación={fila['throughput_desviacion']:.2f} msg/s"
        )

    print(f"\nResultados guardados en: {carpeta}")


if __name__ == "__main__":
    main()
