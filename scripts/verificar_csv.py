
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


def validar_entero(valor, campo, ruta, linea, minimo=0, maximo=None):
    """Comprueba que un campo sea un entero decimal dentro del rango."""
    if not valor or not valor.isascii() or not valor.isdecimal():
        raise ValueError(
            f"{ruta}:{linea}: {campo} debe ser un entero decimal"
        )

    numero = int(valor)

    if numero < minimo or (maximo is not None and numero > maximo):
        raise ValueError(
            f"{ruta}:{linea}: {campo} fuera del rango permitido"
        )

    return numero


def validar_fila(fila, ruta, linea):
    """Valida tipos, rangos y campos obligatorios del CSV."""
    validar_entero(
        fila["participant_id"], "participant_id",
        ruta, linea, minimo=1, maximo=1024
    )

    tiempo = validar_entero(
        fila["timestamp_ns"], "timestamp_ns",
        ruta, linea, maximo=(1 << 64) - 1
    )

    validar_entero(
        fila["occupancy"], "occupancy",
        ruta, linea, maximo=1024
    )

    if fila["event"] in {"ENQUEUE", "DEQUEUE"}:
        productor = validar_entero(
            fila["producer_id"], "producer_id",
            ruta, linea, minimo=1, maximo=1024
        )
        secuencia = validar_entero(
            fila["sequence"], "sequence",
            ruta, linea, minimo=1, maximo=(1 << 64) - 1
        )
        validar_entero(
            fila["enqueue_seq"], "enqueue_seq",
            ruta, linea, minimo=1, maximo=(1 << 64) - 1
        )

        insercion = validar_entero(
            fila["enqueue_time_ns"], "enqueue_time_ns",
            ruta, linea, maximo=(1 << 64) - 1
        )

        if insercion > tiempo:
            raise ValueError(
                f"{ruta}:{linea}: inserción posterior al evento"
            )

        if fila["event"] == "DEQUEUE":
            validar_entero(
                fila["dequeue_seq"], "dequeue_seq",
                ruta, linea, minimo=1, maximo=(1 << 64) - 1
            )
        elif fila["dequeue_seq"] != "":
            raise ValueError(
                f"{ruta}:{linea}: dequeue_seq no aplica a ENQUEUE"
            )

        payload = fila["payload_hex"]

        if len(payload) != 64 or any(
            caracter not in "0123456789abcdef"
            for caracter in payload
        ):
            raise ValueError(
                f"{ruta}:{linea}: payload_hex inválido"
            )

        if fila["event"] == "ENQUEUE" and fila["role"] != "producer":
            raise ValueError(
                f"{ruta}:{linea}: ENQUEUE requiere role=producer"
            )

        if fila["event"] == "DEQUEUE" and fila["role"] != "consumer":
            raise ValueError(
                f"{ruta}:{linea}: DEQUEUE requiere role=consumer"
            )

    else:
        for campo in (
            "producer_id", "sequence", "enqueue_seq",
            "dequeue_seq", "enqueue_time_ns", "payload_hex"
        ):
            if fila[campo] != "":
                raise ValueError(
                    f"{ruta}:{linea}: {campo} debe estar vacío"
                )

    return tiempo



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

            validar_fila(fila, ruta, numero_linea)

            registros.append(fila)

    if not registros:
        raise ValueError(
            f"{ruta}: CSV sin registros; evidencia insuficiente"
        )

    return registros


def verificar_ciclo_vida(registros):
    """Comprueba JOIN, LEAVE y operaciones de cada participante.

    La entrada debe conservar el orden original de los eventos dentro
    de cada archivo de trabajador. No se presupone un orden global
    de escritura entre procesos.
    """
    errores = []

    # Cada participante se identifica por rol e ID.
    participantes = {}

    for fila in registros:
        rol = fila["role"]
        identificador = int(fila["participant_id"])
        evento = fila["event"]
        clave = (rol, identificador)

        estado = participantes.setdefault(
            clave,
            {"registrado": False, "retirado": False}
        )

        if evento == "JOIN":
            if estado["registrado"] or estado["retirado"]:
                errores.append(
                    f"JOIN duplicado o ID reutilizado: {clave}"
                )
            else:
                estado["registrado"] = True

        elif evento == "LEAVE":
            if not estado["registrado"]:
                errores.append(
                    f"LEAVE sin JOIN: {clave}"
                )
            elif estado["retirado"]:
                errores.append(
                    f"LEAVE duplicado: {clave}"
                )
            else:
                estado["retirado"] = True

        elif evento in {"ENQUEUE", "DEQUEUE"}:
            if not estado["registrado"]:
                errores.append(
                    f"Operación sin JOIN: {clave}"
                )
            elif estado["retirado"]:
                errores.append(
                    f"Operación después de LEAVE: {clave}"
                )

    # Comprobar que todos los participantes se retiraron.
    for clave, estado in participantes.items():
        if estado["registrado"] and not estado["retirado"]:
            errores.append(
                f"Participante sin LEAVE: {clave}"
            )

    return errores



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
    """Ejecuta las verificaciones seleccionadas desde la terminal."""
    parser = argparse.ArgumentParser(
        description="Verifica los registros CSV de productores y consumidores."
    )
    parser.add_argument(
        "archivos",
        nargs="+",
        type=Path,
        help="Archivos CSV de una misma instancia",
    )
    parser.add_argument(
        "--validar-ciclo",
        action="store_true",
        help="Comprueba JOIN, LEAVE y operaciones de participantes",
    )
    args = parser.parse_args()

    try:
        registros = []

        # Leer todos los CSV para verificar los mensajes conjuntamente.
        for ruta in args.archivos:
            registros.extend(leer_csv(ruta))

        errores, aceptados, extraidos = verificar_mensajes(registros)

        # Modo provisional: no es una auditoría integral del cierre.
        if args.validar_ciclo:
            errores.extend(verificar_ciclo_vida(registros))

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
