#!/usr/bin/env python3

# Prepara los comandos de los experimentos P2-15.
# Por seguridad, todavía no ejecuta procesos.

import argparse
import csv
import shlex
from pathlib import Path

# Directorio principal del proyecto.
RAIZ = Path(__file__).resolve().parent.parent

# Matriz experimental generada previamente.
CONFIGURACIONES = RAIZ / "results/experimentos/configuraciones.csv"

# Carpeta donde se guardarán las futuras evidencias.
RESULTADOS = RAIZ / "results/experimentos/corridas"


def construir_comandos(fila):
    # Obtener los parámetros de una corrida.
    instancia = fila["instancia"]
    capacidad = fila["capacidad"]
    media_p = fila["media_productor_ms"]
    media_c = fila["media_consumidor_ms"]
    mensajes = fila["mensajes_por_productor"]
    numero = int(fila["corrida"])

    # Crear un directorio único para esta corrida.
    carpeta = RESULTADOS / f"corrida_{numero:02d}"

    # Preparar la creación de memoria compartida.
    comandos = [
        [
            str(RAIZ / "creator"),
            "--name", instancia,
            "--capacity", capacidad,
        ]
    ]

    # Recuperar semillas individuales de productores.
    semillas_p = fila["semillas_productores"].split(";")

    # Recuperar semillas individuales de consumidores.
    semillas_c = fila["semillas_consumidores"].split(";")

    if len(semillas_p) != 4 or len(semillas_c) != 4:
        raise ValueError("Cada corrida debe tener cuatro semillas por rol")

    # Preparar cuatro consumidores.
    for identificador in range(1, 5):
        comandos.append([
            str(RAIZ / "consumer"),
            "--name", instancia,
            "--id", str(identificador),
            "--mean-ms", media_c,
            "--seed", semillas_c[identificador - 1],
            "--log", str(carpeta / f"consumer_{identificador}.csv"),
        ])

    # Preparar cuatro productores.
    for identificador in range(1, 5):
        comandos.append([
            str(RAIZ / "producer"),
            "--name", instancia,
            "--id", str(identificador),
            "--mean-ms", media_p,
            "--seed", semillas_p[identificador - 1],
            "--messages", mensajes,
            "--log", str(carpeta / f"producer_{identificador}.csv"),
        ])

    # Preparar el cierre final.
    comandos.append([
        str(RAIZ / "finisher"),
        "--name", instancia,
        "--summary", str(carpeta / "resumen.csv"),
    ])

    return comandos


def main():
    # Procesar las opciones del planificador.
    parser = argparse.ArgumentParser(
        description="Planificador de experimentos P2-15"
    )

    parser.add_argument(
        "--corrida",
        type=int,
        help="Mostrar una corrida concreta (1 a 45)"
    )

    argumentos = parser.parse_args()

    # Leer la matriz experimental.
    with CONFIGURACIONES.open(
        newline="", encoding="utf-8"
    ) as archivo:
        filas = list(csv.DictReader(archivo))

    if len(filas) != 45:
        raise ValueError("Se esperaban exactamente 45 configuraciones")

    # Filtrar la corrida solicitada.
    if argumentos.corrida is not None:
        filas = [
            fila for fila in filas
            if int(fila["corrida"]) == argumentos.corrida
        ]

        if not filas:
            parser.error("La corrida solicitada no existe")

    # Mostrar los comandos sin ejecutarlos.
    for fila in filas:
        print(
            f"\n=== CORRIDA {fila['corrida']} "
            f"({fila['escenario']}, capacidad={fila['capacidad']}) ==="
        )

        for comando in construir_comandos(fila):
            print(shlex.join(comando))

    print(f"\nOK: {len(filas)} corridas planificadas.")
    print("INFO: no se inició ningún proceso.")


if __name__ == "__main__":
    main()
