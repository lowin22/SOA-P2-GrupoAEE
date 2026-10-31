#!/usr/bin/env python3
"""Genera gráficas de los resultados experimentales P2-15."""

import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")

import matplotlib.pyplot as plt

RAIZ = Path(__file__).resolve().parent.parent


def main():
    # Identificar la sesión que queremos analizar.
    parser = argparse.ArgumentParser()
    parser.add_argument("--sesion", required=True)
    args = parser.parse_args()

    carpeta = (
        RAIZ / "results/experimentos/corridas" / args.sesion
    )

    entrada = carpeta / "resultados_agrupados.csv"

    # Leer las nueve configuraciones.
    with entrada.open(newline="", encoding="utf-8") as archivo:
        filas = list(csv.DictReader(archivo))

    if len(filas) != 9:
        raise ValueError("Se esperaban nueve configuraciones")

    # Las tres situaciones estudiadas.
    escenarios = [
        "produccion_rapida",
        "consumo_rapido",
        "equilibrado",
    ]

    nombres = {
        "produccion_rapida": "Producción rápida",
        "consumo_rapido": "Consumo rápido",
        "equilibrado": "Equilibrado",
    }

    capacidades = [1, 8, 64]

    # Preparar las métricas y los nombres de archivo.
    graficas = [
        (
            "throughput_promedio",
            "throughput_desviacion",
            "Rendimiento promedio por capacidad",
            "Mensajes por segundo",
            "rendimiento.png",
        ),
        (
            "latency_mean_ns_promedio",
            "latency_mean_ns_desviacion",
            "Latencia promedio por capacidad",
            "Nanosegundos",
            "latencia_promedio.png",
        ),
        (
            "latency_p95_ns_promedio",
            None,
            "Percentil 95 de latencia",
            "Nanosegundos",
            "latencia_p95.png",
        ),
    ]

    for metrica, desviacion, titulo, eje_y, nombre_archivo in graficas:

        # Crear una figura independiente para cada métrica.
        fig, ax = plt.subplots(figsize=(9, 5))

        for escenario in escenarios:
            # Recuperar los valores ordenados por capacidad.
            seleccion = {
                int(f["capacidad"]): f
                for f in filas
                if f["escenario"] == escenario
            }

            valores = [
                float(seleccion[c][metrica])
                for c in capacidades
            ]

            # Mostrar dispersión cuando existe.
            errores = (
                [
                    float(seleccion[c][desviacion])
                    for c in capacidades
                ]
                if desviacion else None
            )

            ax.errorbar(
                capacidades,
                valores,
                yerr=errores,
                marker="o",
                capsize=4,
                label=nombres[escenario],
            )

        ax.set_title(titulo)
        ax.set_xlabel("Capacidad del buffer (mensajes)")
        ax.set_ylabel(eje_y)
        ax.set_xticks(capacidades)
        ax.set_xscale("log", base=2)
        ax.grid(True, alpha=0.3)
        ax.legend()

        fig.tight_layout()
        destino = carpeta / nombre_archivo
        fig.savefig(destino, dpi=180)
        plt.close(fig)

        print(f"OK: {destino.name}")

    print("OK: tres gráficas generadas desde datos reales.")


if __name__ == "__main__":
    main()
