
#!/usr/bin/env python3
"""Verificador inicial de registros CSV del Proyecto 2 de SOA.

Issue P2-11.
Comprueba estructura, correspondencia de mensajes y orden FIFO.
"""

import argparse
import csv
import sys
from pathlib import Path


COLUMNAS = [
    "schema_version", "role", "participant_id", "event",
    "timestamp_ns", "state", "producer_id", "sequence",
    "enqueue_seq", "dequeue_seq", "enqueue_time_ns",
    "occupancy", "payload_hex",
]

EVENTOS = {"JOIN", "ENQUEUE", "DEQUEUE", "LEAVE"}
ROLES = {"producer", "consumer"}
ESTADOS = {"OPEN", "CLOSING", "CLOSED"}


# Máscara para representar enteros de 64 bits sin signo.
MASCARA_64 = (1 << 64) - 1


def generar_payload(producer_id, sequence):
    """Reconstruye los 32 bytes del mensaje mediante SplitMix64.

    La semilla inicial combina el ID del productor y su secuencia.
    Se producen cuatro bloques de 64 bits en orden little-endian.
    """
    estado = ((producer_id << 32) ^ sequence) & MASCARA_64
    resultado = bytearray()

    for _ in range(4):
        estado = (estado + 0x9E3779B97F4A7C15) & MASCARA_64

        valor = estado
        valor = ((valor ^ (valor >> 30))
                 * 0xBF58476D1CE4E5B9) & MASCARA_64
        valor = ((valor ^ (valor >> 27))
                 * 0x94D049BB133111EB) & MASCARA_64
        valor = valor ^ (valor >> 31)

        resultado.extend(valor.to_bytes(8, byteorder="little"))

    return resultado.hex()



def leer_csv(ruta):
    """Lee un archivo CSV y valida su estructura básica."""
    registros = []

    with ruta.open("r", encoding="utf-8", newline="") as archivo:
        lector = csv.DictReader(archivo)

        if lector.fieldnames != COLUMNAS:
            raise ValueError(f"{ruta}: encabezado CSV incorrecto")

        for numero_linea, fila in enumerate(lector, start=2):
            if None in fila or any(valor is None for valor in fila.values()):
                raise ValueError(
                    f"{ruta}:{numero_linea}: cantidad de columnas incorrecta"
                )

            if fila["schema_version"] != "1":
                raise ValueError(f"{ruta}:{numero_linea}: versión no válida")

            if fila["role"] not in ROLES:
                raise ValueError(f"{ruta}:{numero_linea}: rol inválido")

            if fila["event"] not in EVENTOS:
                raise ValueError(f"{ruta}:{numero_linea}: evento inválido")

            if fila["state"] not in ESTADOS:
                raise ValueError(f"{ruta}:{numero_linea}: estado inválido")

            registros.append(fila)

    return registros



def verificar_mensajes(registros):
    """Comprueba identidad, conservación, secuencias globales y FIFO."""
    inserciones = {}
    extracciones = {}
    errores = []

    secuencias_insercion = {}
    secuencias_extraccion = {}

    for fila in registros:
        evento = fila["event"]

        if evento not in {"ENQUEUE", "DEQUEUE"}:
            continue

        try:
            identidad = (
                int(fila["producer_id"]),
                int(fila["sequence"]),
            )
            orden_insercion = int(fila["enqueue_seq"])
        except ValueError:
            errores.append("Identidad o secuencia de inserción inválida")
            continue

        if (
            not 1 <= identidad[0] <= 1024
            or identidad[1] <= 0
            or orden_insercion <= 0
        ):
            errores.append(
                f"Identidad o secuencia fuera de rango: {identidad}"
            )
            continue

# Validar que los 32 bytes correspondan a la identidad
# del mensaje y al algoritmo determinista acordado.
        payload_recibido = fila["payload_hex"]
        payload_esperado = generar_payload(
            identidad[0], identidad[1]
        )

        if payload_recibido != payload_esperado:
            errores.append(
                f"Payload inválido para el mensaje {identidad}"
            )


        if evento == "ENQUEUE":
            if identidad in inserciones:
                errores.append(f"Inserción duplicada: {identidad}")
            else:
                inserciones[identidad] = fila

            if orden_insercion in secuencias_insercion:
                errores.append(
                    f"enqueue_seq duplicada: {orden_insercion}"
                )
            else:
                secuencias_insercion[orden_insercion] = identidad

        elif evento == "DEQUEUE":
            try:
                orden_extraccion = int(fila["dequeue_seq"])
            except ValueError:
                errores.append(
                    f"dequeue_seq inválida para {identidad}"
                )
                continue

            if orden_extraccion <= 0:
                errores.append(
                    f"dequeue_seq fuera de rango: {orden_extraccion}"
                )
                continue

            if identidad in extracciones:
                errores.append(f"Extracción duplicada: {identidad}")
            else:
                extracciones[identidad] = fila

            if orden_extraccion in secuencias_extraccion:
                errores.append(
                    f"dequeue_seq duplicada: {orden_extraccion}"
                )
            else:
                secuencias_extraccion[orden_extraccion] = identidad

    # Verificar que los números globales sean consecutivos.
    def comprobar_continuidad(nombre, secuencias):
        numeros = sorted(secuencias)
        esperados = list(range(1, len(numeros) + 1))

        if numeros != esperados:
            errores.append(
                f"{nombre} no consecutiva: "
                f"obtenido {numeros}, esperado {esperados}"
            )

    comprobar_continuidad("enqueue_seq", secuencias_insercion)
    comprobar_continuidad("dequeue_seq", secuencias_extraccion)

    # Comprobar conservación de mensajes.
    faltantes = set(inserciones) - set(extracciones)
    inesperados = set(extracciones) - set(inserciones)

    if faltantes:
        errores.append(
            f"Mensajes sin extracción: {sorted(faltantes)}"
        )

    if inesperados:
        errores.append(
            f"Extracciones sin inserción: {sorted(inesperados)}"
        )

    # El consumidor debe conservar el número de inserción
    # original que viaja dentro del mensaje.
    for identidad in set(inserciones) & set(extracciones):
        entrada = inserciones[identidad]
        salida = extracciones[identidad]

        if entrada["enqueue_seq"] != salida["enqueue_seq"]:
            errores.append(
                f"enqueue_seq alterada para {identidad}"
            )

    # Reconstruir ambos órdenes por los números globales,
    # nunca por el orden de las filas de los archivos CSV.
    orden_entrada = [
        secuencias_insercion[numero]
        for numero in sorted(secuencias_insercion)
    ]

    orden_salida = [
        secuencias_extraccion[numero]
        for numero in sorted(secuencias_extraccion)
    ]

    if orden_entrada != orden_salida:
        errores.append(
            "Violación de FIFO: el orden de extracción es diferente"
        )

    return errores, len(inserciones), len(extracciones)



def main():
    parser = argparse.ArgumentParser(
        description="Verifica los registros CSV de productores y consumidores."
    )
    parser.add_argument(
        "archivos",
        nargs="+",
        type=Path,
        help="Archivos CSV de una misma instancia",
    )
    args = parser.parse_args()

    try:
        registros = []

        for ruta in args.archivos:
            registros.extend(leer_csv(ruta))

        errores, aceptados, extraidos = verificar_mensajes(registros)

        print(f"Mensajes aceptados: {aceptados}")
        print(f"Mensajes extraídos: {extraidos}")

        if errores:
            for error in errores:
                print(f"ERROR: {error}", file=sys.stderr)
            return 1

        print("Verificación inicial correcta.")
        return 0

    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
