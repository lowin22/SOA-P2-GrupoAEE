
#!/usr/bin/env python3
"""Verificador inicial de registros CSV del Proyecto 2 de SOA.

Issue P2-11.
Comprueba estructura, correspondencia de mensajes y orden FIFO.
"""

import argparse
import csv
import json
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

        # La marca de inserción viaja con el mensaje y no debe cambiar.
        if entrada["enqueue_time_ns"] != salida["enqueue_time_ns"]:
            errores.append(
                f"enqueue_time_ns alterada para {identidad}"
            )

        # Además de comprobar SplitMix64 por fila, contrastar ambas copias.
        if entrada["payload_hex"] != salida["payload_hex"]:
            errores.append(
                f"payload_hex diferente entre ENQUEUE y DEQUEUE: {identidad}"
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



def cargar_manifiesto(ruta):
    """Obtiene rutas relativas declaradas en un manifiesto auxiliar v1.

    El manifiesto no sustituye el resumen del finalizador: únicamente verifica
    la presencia de los archivos que alguien declaró de forma independiente.
    """
    with ruta.open("r", encoding="utf-8") as archivo:
        documento = json.load(archivo)

    if (not isinstance(documento, dict)
            or set(documento) != {"schema_version", "files"}
            or type(documento["schema_version"]) is not int
            or documento["schema_version"] != 1
            or not isinstance(documento["files"], list)
            or not documento["files"]):
        raise ValueError(f"{ruta}: manifiesto inválido (versión o lista)")

    base = ruta.resolve().parent
    archivos = []
    vistos = set()
    for nombre in documento["files"]:
        if not isinstance(nombre, str) or not nombre or "\\" in nombre:
            raise ValueError(f"{ruta}: ruta de CSV inválida: {nombre!r}")
        relativa = Path(nombre)
        if relativa.is_absolute() or ".." in relativa.parts or relativa.suffix != ".csv":
            raise ValueError(f"{ruta}: ruta de CSV no permitida: {nombre!r}")
        destino = (base / relativa).resolve()
        if not destino.is_relative_to(base):
            raise ValueError(f"{ruta}: CSV fuera del directorio del manifiesto")
        if destino in vistos:
            raise ValueError(f"{ruta}: archivo duplicado en manifiesto: {nombre}")
        vistos.add(destino)
        if not destino.is_file():
            raise ValueError(f"{ruta}: archivo CSV faltante: {nombre}")
        archivos.append(destino)
    return archivos





def verificar_corte_cierre(registros, closing_start_ns,
                           release_complete_ns=None):
    """Audita eventos respecto al corte y la liberación."""

    errores = []

    for fila in registros:
        evento = fila["event"]
        estado = fila["state"]
        tiempo = int(fila["timestamp_ns"])

        # JOIN: solamente antes del cierre y durante OPEN.
        if evento == "JOIN":
            if estado != "OPEN" or tiempo >= closing_start_ns:
                errores.append(
                    f"JOIN posterior al inicio del cierre o fuera de OPEN: "
                    f"participante {fila['participant_id']}"
                )

        # ENQUEUE: ninguna inserción después del corte.
        elif evento == "ENQUEUE":
            insercion = int(fila["enqueue_time_ns"])

            if estado != "OPEN":
                errores.append(
                    f"ENQUEUE registrado fuera de OPEN: "
                    f"productor {fila['producer_id']}, "
                    f"mensaje {fila['sequence']}"
                )

            if insercion >= closing_start_ns:
                errores.append(
                    f"ENQUEUE posterior al inicio del cierre: "
                    f"productor {fila['producer_id']}, "
                    f"mensaje {fila['sequence']}"
                )

        # DEQUEUE: permitido en OPEN y CLOSING.
        elif evento == "DEQUEUE":
            if estado not in {"OPEN", "CLOSING"}:
                errores.append("DEQUEUE en estado no permitido")

        # LEAVE: permitido antes o durante el cierre.
        elif evento == "LEAVE":
            if estado not in {"OPEN", "CLOSING"}:
                errores.append("LEAVE en estado no permitido")

        # Ningún evento debe registrarse después de la liberación.
        if (release_complete_ns is not None
                and tiempo > release_complete_ns):
            errores.append(
                f"{evento} posterior a la liberación de la instancia"
            )

    return errores



def obtener_inicio_cierre(ruta):
    """Obtiene la marca de cierre desde el resumen CSV."""

    with ruta.open(newline="", encoding="utf-8") as archivo:
        lector = csv.DictReader(archivo)

        if lector.fieldnames is None:
            raise ValueError("Resumen sin encabezado")

        if "closing_start_ns" not in lector.fieldnames:
            raise ValueError("Resumen sin closing_start_ns")

        filas = list(lector)

    if len(filas) != 1:
        raise ValueError("El resumen debe contener una sola fila")

    return validar_entero(
        filas[0]["closing_start_ns"],
        "closing_start_ns", ruta, 2,
        minimo=1, maximo=MASCARA_64
    )




def verificar_resumen_cierre(ruta, aceptados, extraidos):
    """Valida las 14 columnas del resumen y su coherencia."""

    import math

    columnas = [
        "schema_version", "instance_name", "capacity",
        "accepted", "extracted", "pending",
        "first_worker_start_ns", "last_dequeue_ns",
        "closing_start_ns", "release_complete_ns",
        "throughput_msg_s", "latency_mean_ns",
        "latency_p95_ns", "closing_duration_ns",
    ]

    # Leer el encabezado y exigir exactamente una fila.
    with ruta.open(newline="", encoding="utf-8") as archivo:
        lector = csv.DictReader(archivo)

        if lector.fieldnames != columnas:
            raise ValueError("Encabezado del resumen incorrecto")

        filas = list(lector)

    if len(filas) != 1:
        raise ValueError("El resumen debe contener una sola fila")

    fila = filas[0]

    # Rechazar filas truncadas o con columnas adicionales.
    if None in fila or any(valor is None for valor in fila.values()):
        raise ValueError("Cantidad de columnas incorrecta")

    if fila["schema_version"] != "1":
        raise ValueError("Versión del resumen incorrecta")

    nombre = fila["instance_name"]

    # La instancia debe tener un nombre POSIX sencillo.
    if (not nombre.startswith("/")
            or len(nombre) < 2
            or "/" in nombre[1:]):
        raise ValueError("Nombre de instancia inválido")

    def entero(campo, minimo=0):
        # Leer un entero sin signo de 64 bits.
        return validar_entero(
            fila[campo], campo, ruta, 2,
            minimo=minimo, maximo=MASCARA_64
        )

    def entero_opcional(campo):
        # Un campo vacío representa una medición no disponible.
        if fila[campo] == "":
            return None
        return entero(campo)

    def decimal_opcional(campo):
        # Permitir ausencia de medición, pero no NaN ni infinito.
        if fila[campo] == "":
            return None
        try:
            valor = float(fila[campo])
        except ValueError as error:
            raise ValueError(
                f"{campo} no es un número válido"
            ) from error

        if not math.isfinite(valor) or valor < 0:
            raise ValueError(f"{campo} fuera de rango")

        return valor

    capacidad = entero("capacity", 1)
    a = entero("accepted")
    e = entero("extracted")
    pendientes = entero("pending")

    primero = entero_opcional("first_worker_start_ns")
    ultimo = entero_opcional("last_dequeue_ns")
    inicio_cierre = entero("closing_start_ns", 1)
    fin_cierre = entero("release_complete_ns")

    rendimiento = decimal_opcional("throughput_msg_s")
    latencia = decimal_opcional("latency_mean_ns")
    percentil = decimal_opcional("latency_p95_ns")
    duracion = entero("closing_duration_ns")

    errores = []

    if capacidad > 1024:
        errores.append("capacity fuera de rango")

    if a != aceptados:
        errores.append("accepted no coincide con los CSV")

    if e != extraidos:
        errores.append("extracted no coincide con los CSV")

    if a != e or e > a:
        errores.append("El cierre no conserva todos los mensajes")

    if pendientes != 0 or pendientes != a - e:
        errores.append("El resumen presenta mensajes pendientes")

    if fin_cierre < inicio_cierre:
        errores.append("Liberación anterior al inicio del cierre")
    elif duracion != fin_cierre - inicio_cierre:
        errores.append("closing_duration_ns incorrecta")

    if primero is not None and primero > inicio_cierre:
        errores.append("Inicio de trabajadores posterior al cierre")

    if ultimo is not None:
        if primero is None or ultimo < primero:
            errores.append("Marcas temporales de extracción incorrectas")
        if ultimo > fin_cierre:
            errores.append("Extracción posterior a liberación")

    if (latencia is None) != (percentil is None):
        errores.append("Las dos métricas de latencia deben coexistir")

    if e == 0:
        if ultimo is not None or rendimiento is not None:
            errores.append("Métricas incompatibles con cero extracciones")
        if latencia is not None:
            errores.append("Latencia sin mensajes extraídos")
    else:
        if primero is None or ultimo is None:
            errores.append("Faltan marcas temporales de mensajes")
        elif ultimo > primero:
            esperado = e * 1_000_000_000 / (ultimo - primero)

            if rendimiento is None or not math.isclose(
                rendimiento, esperado, rel_tol=1e-8, abs_tol=1e-9
            ):
                errores.append("throughput_msg_s inconsistente")
        elif rendimiento is not None:
            errores.append("Rendimiento sin intervalo positivo")

    return errores


def main():
    """Ejecuta las verificaciones seleccionadas desde la terminal."""
    parser = argparse.ArgumentParser(
        description="Verifica los registros CSV de productores y consumidores."
    )
    parser.add_argument(
        "archivos",
        nargs="*",
        type=Path,
        help="Archivos CSV de una misma instancia (opcional con --manifiesto)",
    )
    parser.add_argument(
        "--validar-ciclo",
        action="store_true",
        help="Comprueba JOIN, LEAVE y operaciones de participantes",
    )
    parser.add_argument(
        "--manifiesto",
        type=Path,
        help="JSON auxiliar que declara los CSV esperados de la instancia",
    )
    parser.add_argument(
        "--summary",
        type=Path,
        help="CSV del resumen final para comprobar conservación y cierre",
    )
    args = parser.parse_args()

    try:
        if args.manifiesto is not None:
            rutas = cargar_manifiesto(args.manifiesto)
            # Si también se proporcionan rutas, su conjunto debe coincidir
            # exactamente con los CSV declarados en el manifiesto.
            if args.archivos:
                suministrados = [ruta.resolve() for ruta in args.archivos]
                if (len(suministrados) != len(set(suministrados))
                        or set(suministrados) != set(rutas)):
                    raise ValueError(
                        "Los archivos indicados no coinciden con el manifiesto"
                    )
        else:
            rutas = args.archivos
            if not rutas:
                parser.error("se requieren CSV o la opción --manifiesto")

        registros = []

        # Leer todos los CSV para verificar los mensajes conjuntamente.
        for ruta in rutas:
            registros.extend(leer_csv(ruta))

        errores, aceptados, extraidos = verificar_mensajes(registros)

        # Modo provisional: no es una auditoría integral del cierre.
        if args.validar_ciclo:
            errores.extend(verificar_ciclo_vida(registros))

        if args.summary is not None:
            errores.extend(
                verificar_resumen_cierre(
                    args.summary, aceptados, extraidos
                )
            )

            # Auditar incorporaciones e inserciones respecto al cierre.
            inicio_cierre = obtener_inicio_cierre(args.summary)

            # Leer la marca final de liberación del resumen.
            with args.summary.open(
                newline="", encoding="utf-8"
            ) as archivo:
                fila_resumen = next(csv.DictReader(archivo))

            fin_cierre = validar_entero(
                fila_resumen["release_complete_ns"],
                "release_complete_ns",
                args.summary,
                2,
                maximo=MASCARA_64
            )

            # Auditar eventos respecto al corte y la liberación.
            errores.extend(
                verificar_corte_cierre(
                    registros, inicio_cierre, fin_cierre
                )
            )

        print(f"Mensajes aceptados: {aceptados}")
        print(f"Mensajes extraídos: {extraidos}")

        if errores:
            for error in errores:
                print(f"ERROR: {error}", file=sys.stderr)
            return 1

        print("Verificación inicial correcta.")
        return 0

    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
