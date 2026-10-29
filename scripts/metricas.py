#!/usr/bin/env python3
"""Cálculos de métricas del Proyecto 2 de SOA (Issue P2-08)."""

from math import ceil

MAX_UINT64 = (1 << 64) - 1


def validar_latencias(latencias_ns):
    """Valida una colección de duraciones expresadas en nanosegundos."""
    valores = list(latencias_ns)

    for latencia in valores:
        if type(latencia) is not int:
            raise ValueError("La latencia debe ser un entero")

        if not 0 <= latencia <= MAX_UINT64:
            raise ValueError("Latencia fuera del rango uint64")

    return valores


def calcular_latencia_promedio(latencias_ns):
    """Calcula el promedio; retorna None cuando no existen datos."""
    valores = validar_latencias(latencias_ns)

    if not valores:
        return None

    return sum(valores) / len(valores)


def calcular_percentil_95(latencias_ns):
    """Calcula el percentil 95 mediante nearest rank."""
    valores = validar_latencias(latencias_ns)

    if not valores:
        return None

    ordenadas = sorted(valores)
    posicion = ceil(0.95 * len(ordenadas))

    return ordenadas[posicion - 1]


def calcular_throughput(extraidos, inicio_ns, ultima_extraccion_ns):
    """Calcula mensajes extraídos por segundo.

    Retorna None si no hubo extracciones o el intervalo es cero.
    """
    for valor in (extraidos, inicio_ns, ultima_extraccion_ns):
        if type(valor) is not int or not 0 <= valor <= MAX_UINT64:
            raise ValueError("Contador o tiempo fuera del rango uint64")

    if extraidos == 0:
        return None

    duracion_ns = ultima_extraccion_ns - inicio_ns

    if duracion_ns < 0:
        raise ValueError("La última extracción precede al inicio")

    if duracion_ns == 0:
        return None

    return extraidos * 1_000_000_000 / duracion_ns



def calcular_duracion_cierre(inicio_cierre_ns, liberacion_ns):
    """Calcula la duración del cierre en nanosegundos."""
    for valor in (inicio_cierre_ns, liberacion_ns):
        if type(valor) is not int or not 0 <= valor <= MAX_UINT64:
            raise ValueError("Marca temporal fuera del rango uint64")

    if liberacion_ns < inicio_cierre_ns:
        raise ValueError("La liberación precede al inicio del cierre")

    return liberacion_ns - inicio_cierre_ns


def extraer_latencias(registros):
    """Extrae las latencias de los eventos DEQUEUE.

    Los registros deben haber sido validados previamente
    mediante el verificador CSV del P2-11.
    """
    latencias = []

    for fila in registros:
        if fila["event"] != "DEQUEUE":
            continue

        tiempo_extraccion = int(fila["timestamp_ns"])
        tiempo_insercion = int(fila["enqueue_time_ns"])

        validar_latencias([tiempo_extraccion, tiempo_insercion])

        if tiempo_extraccion < tiempo_insercion:
            raise ValueError(
                "La extracción precede a la inserción del mensaje"
            )

        latencias.append(tiempo_extraccion - tiempo_insercion)

    return latencias
