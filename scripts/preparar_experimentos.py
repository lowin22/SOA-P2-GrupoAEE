#!/usr/bin/env python3

# Genera las configuraciones del experimento P2-15.
# No inicia procesos ni ejecuta experimentos.

import csv
from itertools import product
from pathlib import Path

# Capacidades del buffer.
capacidades = [1, 8, 64]

# Escenarios: nombre, pausa del productor y pausa del consumidor.
escenarios = [
    ("produccion_rapida", 2, 8),
    ("consumo_rapido", 8, 2),
    ("equilibrado", 5, 5),
]

# Cinco repeticiones por combinación.
repeticiones = range(1, 6)

# Parámetros fijos del experimento.
productores = 4
consumidores = 4
mensajes_por_productor = 200

# Ubicación del archivo de configuraciones.
salida = Path("results/experimentos/configuraciones.csv")
salida.parent.mkdir(parents=True, exist_ok=True)

columnas = [
    "corrida",
    "instancia",
    "escenario",
    "capacidad",
    "media_productor_ms",
    "media_consumidor_ms",
    "repeticion",
    "productores",
    "consumidores",
    "mensajes_por_productor",
    "semillas_productores",
    "semillas_consumidores",
]

with salida.open("w", newline="", encoding="utf-8") as archivo:
    escritor = csv.DictWriter(archivo, fieldnames=columnas, lineterminator="\n")
    escritor.writeheader()

    for numero, (capacidad, escenario, repeticion) in enumerate(
        product(capacidades, escenarios, repeticiones),
        start=1,
    ):
        nombre, media_p, media_c = escenario

        # Nombre único, compatible con las opciones del proyecto.
        instancia = f"/soa_exp_{numero:02d}"

        # Semillas individuales y deterministas por corrida.
        semillas_p = [
            numero * 100 + identificador
            for identificador in range(1, productores + 1)
        ]

        semillas_c = [
            numero * 100 + 10 + identificador
            for identificador in range(1, consumidores + 1)
        ]

        escritor.writerow({
            "corrida": numero,
            "instancia": instancia,
            "escenario": nombre,
            "capacidad": capacidad,
            "media_productor_ms": media_p,
            "media_consumidor_ms": media_c,
            "repeticion": repeticion,
            "productores": productores,
            "consumidores": consumidores,
            "mensajes_por_productor": mensajes_por_productor,
            "semillas_productores": ";".join(map(str, semillas_p)),
            "semillas_consumidores": ";".join(map(str, semillas_c)),
        })

print(f"OK: 45 configuraciones generadas en {salida}")
