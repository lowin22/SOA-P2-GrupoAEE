#!/usr/bin/env python3
"""Planificador y ejecutor reproducible de experimentos P2-15."""

import argparse
import csv
import json
import platform
import shlex
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path

# Ubicación de nuestro proyecto y sus resultados.
RAIZ = Path(__file__).resolve().parent.parent
CONFIGURACIONES = RAIZ / "results/experimentos/configuraciones.csv"
RESULTADOS = RAIZ / "results/experimentos/corridas"

# Permitir importar los módulos del proyecto.
sys.path.insert(0, str(RAIZ))

from scripts.metricas import calcular_metricas_csv


def construir_comandos(fila, instancia=None, carpeta=None):
    """Preparar los diez comandos de una corrida."""

    numero = int(fila["corrida"])
    instancia = instancia or fila["instancia"]
    carpeta = carpeta or RESULTADOS / f"corrida_{numero:02d}"

    # Comenzamos creando la memoria compartida.
    comandos = [[
        str(RAIZ / "creator"),
        "--name", instancia,
        "--capacity", fila["capacidad"],
    ]]

    # Recuperar las semillas individuales.
    semillas_p = fila["semillas_productores"].split(";")
    semillas_c = fila["semillas_consumidores"].split(";")

    if len(semillas_p) != 4 or len(semillas_c) != 4:
        raise ValueError("Se requieren cuatro semillas por rol")

    # Preparar los consumidores.
    for i in range(4):
        comandos.append([
            str(RAIZ / "consumer"),
            "--name", instancia,
            "--id", str(i + 1),
            "--mean-ms", fila["media_consumidor_ms"],
            "--seed", semillas_c[i],
            "--log", str(carpeta / f"consumer_{i + 1}.csv"),
        ])

    # Preparar los productores con cuota definida.
    for i in range(4):
        comandos.append([
            str(RAIZ / "producer"),
            "--name", instancia,
            "--id", str(i + 1),
            "--mean-ms", fila["media_productor_ms"],
            "--seed", semillas_p[i],
            "--messages", fila["mensajes_por_productor"],
            "--log", str(carpeta / f"producer_{i + 1}.csv"),
        ])

    # El finalizador debe ejecutarse después de los productores.
    comandos.append([
        str(RAIZ / "finisher"),
        "--name", instancia,
        "--summary", str(carpeta / "resumen.csv"),
    ])

    return comandos


def ejecutar_corrida(fila, sesion, limite):
    """Ejecutar una corrida y validar sus resultados reales."""

    numero = int(fila["corrida"])

    # Cada sesión recibe una carpeta y un nombre de instancia exclusivos.
    carpeta = RESULTADOS / sesion / f"corrida_{numero:02d}"
    carpeta.mkdir(parents=True, exist_ok=False)

    instancia = f"/soa_{sesion}_{numero:02d}"
    comandos = construir_comandos(fila, instancia, carpeta)

    procesos = []
    archivos_abiertos = []
    salidas = {}
    inicio = time.monotonic()
    plazo = inicio + limite

    # Registrar todos los parámetros utilizados.
    manifiesto = {
        "corrida": numero,
        "sesion": sesion,
        "instancia": instancia,
        "configuracion": fila,
        "fecha_utc": datetime.now(timezone.utc).isoformat(),
        "sistema": platform.platform(),
        "python": platform.python_version(),
        "comandos": [shlex.join(c) for c in comandos],
        "salidas": salidas,
        "estado": "FALLIDA",
    }

    def tiempo_restante():
        """Evitar que la corrida supere el tiempo permitido."""
        restante = plazo - time.monotonic()
        if restante <= 0:
            raise TimeoutError("Se agotó el tiempo de la corrida")
        return restante

    def iniciar(nombre, comando):
        """Iniciar un participante y guardar su salida."""
        archivo = (carpeta / f"{nombre}.txt").open("w")
        archivos_abiertos.append(archivo)

        proceso = subprocess.Popen(
            comando,
            stdout=archivo,
            stderr=subprocess.STDOUT,
        )

        procesos.append((nombre, proceso))
        return proceso

    def ejecutar_programa(nombre, comando):
        """Ejecutar creator o finisher y registrar su resultado."""
        resultado = subprocess.run(
            comando,
            capture_output=True,
            text=True,
            timeout=tiempo_restante(),
        )

        (carpeta / f"{nombre}.txt").write_text(
            resultado.stdout + resultado.stderr
        )

        salidas[nombre] = resultado.returncode

        if resultado.returncode != 0:
            raise RuntimeError(
                f"{nombre} terminó con código {resultado.returncode}"
            )

    try:
        # Comprobar que existen los cuatro ejecutables.
        for programa in ("creator", "consumer", "producer", "finisher"):
            if not (RAIZ / programa).is_file():
                raise FileNotFoundError(
                    f"No existe {programa}. Ejecutar make antes."
                )

        # Crear la instancia compartida.
        ejecutar_programa("creator", comandos[0])

        # Iniciar los cuatro consumidores.
        for i in range(4):
            iniciar(f"consumer_{i + 1}", comandos[1 + i])

        # Permitir que se registren antes de comenzar la producción.
        time.sleep(0.35)

        # Detectar consumidores que fallaron al iniciar.
        for nombre, proceso in procesos:
            if proceso.poll() is not None:
                raise RuntimeError(
                    f"{nombre} terminó antes de iniciar productores"
                )

        # Iniciar los cuatro productores.
        for i in range(4):
            iniciar(f"producer_{i + 1}", comandos[5 + i])

        # Esperar a TODOS los productores antes de solicitar el cierre.
        for nombre, proceso in procesos:
            if not nombre.startswith("producer_"):
                continue

            codigo = proceso.wait(timeout=tiempo_restante())
            salidas[nombre] = codigo

            if codigo != 0:
                raise RuntimeError(f"{nombre} falló: código {codigo}")

        # Cerrar y liberar la instancia después de la producción.
        ejecutar_programa("finisher", comandos[-1])

        # Esperar a que finalicen los consumidores.
        for nombre, proceso in procesos:
            codigo = proceso.wait(timeout=tiempo_restante())
            salidas[nombre] = codigo

            if codigo != 0:
                raise RuntimeError(f"{nombre} falló: código {codigo}")

        # Cerrar archivos para asegurar que todos los datos estén escritos.
        for archivo in archivos_abiertos:
            archivo.close()
        archivos_abiertos.clear()

        # Comprobar que existen los ocho registros.
        rutas = [
            carpeta / f"{rol}_{i}.csv"
            for rol in ("producer", "consumer")
            for i in range(1, 5)
        ]

        if not all(ruta.is_file() for ruta in rutas):
            raise RuntimeError("Faltan archivos CSV de participantes")

        # Nuestro módulo de métricas incluye validación de los eventos.
        metricas = calcular_metricas_csv(rutas)

        # Leer el resumen generado por el finalizador.
        with (carpeta / "resumen.csv").open(
            newline="", encoding="utf-8"
        ) as archivo:
            resumen = list(csv.DictReader(archivo))

        if len(resumen) != 1:
            raise RuntimeError("El resumen debe tener exactamente una fila")

        datos = resumen[0]

        # Comprobar los 800 mensajes y la ausencia de pendientes.
        esperados = (
            int(fila["productores"]) *
            int(fila["mensajes_por_productor"])
        )

        for campo in ("accepted", "extracted"):
            if int(datos[campo]) != esperados:
                raise RuntimeError(
                    f"{campo}: se esperaban {esperados} mensajes"
                )

            if metricas[campo] != esperados:
                raise RuntimeError(
                    f"{campo}: el CSV no coincide con lo esperado"
                )

        if int(datos["pending"]) != 0:
            raise RuntimeError("Quedaron mensajes pendientes")

        # Guardar las mediciones reales.
        manifiesto["resumen"] = datos
        manifiesto["metricas_csv"] = metricas
        manifiesto["estado"] = "VALIDA"

        print(
            f"OK corrida {numero:02d}: {esperados} mensajes, "
            f"throughput={datos['throughput_msg_s']} msg/s"
        )

        return True

    except Exception as error:
        # Registrar la causa; nunca presentar esta corrida como válida.
        manifiesto["error"] = str(error)
        print(f"ERROR corrida {numero:02d}: {error}")
        return False

    finally:
        # Detener solamente los procesos que inició este ejecutor.
        for nombre, proceso in procesos:
            if proceso.poll() is None:
                proceso.terminate()

        for nombre, proceso in procesos:
            try:
                proceso.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proceso.kill()
                proceso.wait()

            salidas[nombre] = proceso.returncode

        # Cerrar los archivos de salida.
        for archivo in archivos_abiertos:
            archivo.close()

        # Guardar la duración y las evidencias.
        manifiesto["duracion_real_s"] = round(
            time.monotonic() - inicio, 3
        )

        (carpeta / "manifiesto.json").write_text(
            json.dumps(manifiesto, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

        # Si hubo un error, conservar los archivos para diagnóstico.
        # No eliminar automáticamente objetos de memoria compartida:
        # primero se debe comprobar su estado.


def main():
    """Permitir planificación y ejecución mediante argumentos."""

    parser = argparse.ArgumentParser(
        description="Experimentos P2-15 del Proyecto 2 de SOA"
    )

    parser.add_argument("--corrida", type=int)
    parser.add_argument("--ejecutar", action="store_true")
    parser.add_argument("--todas", action="store_true")
    parser.add_argument("--timeout", type=int, default=90)

    args = parser.parse_args()

    # Evitar iniciar por accidente todas las corridas.
    if args.ejecutar and (args.corrida is None) == (not args.todas):
        parser.error("Use --ejecutar con --corrida N o --todas")

    if args.timeout < 1:
        parser.error("--timeout debe ser positivo")

    # Cargar la matriz experimental original.
    with CONFIGURACIONES.open(
        newline="", encoding="utf-8"
    ) as archivo:
        filas = list(csv.DictReader(archivo))

    if len(filas) != 45:
        raise ValueError("Se esperaban 45 configuraciones")

    if args.corrida is not None:
        filas = [
            fila for fila in filas
            if int(fila["corrida"]) == args.corrida
        ]

        if not filas:
            parser.error("La corrida solicitada no existe")

    # Mantener el comportamiento original sin --ejecutar.
    if not args.ejecutar:
        for fila in filas:
            print(f"\n=== CORRIDA {fila['corrida']} ===")
            for comando in construir_comandos(fila):
                print(shlex.join(comando))

        print(f"\nOK: {len(filas)} corridas planificadas.")
        print("INFO: no se inició ningún proceso.")
        return

    # Identificador exclusivo de sesión, sin sobrescribir evidencias.
    sesion = uuid.uuid4().hex[:10]
    validas = 0

    print(f"Sesión experimental: {sesion}", flush=True)

    # Ejecutar secuencialmente para no mezclar experimentos.
    for fila in filas:
        if ejecutar_corrida(fila, sesion, args.timeout):
            validas += 1

    print(f"\nRESULTADO: {validas}/{len(filas)} corridas válidas.")
    print(f"Evidencias: {RESULTADOS / sesion}")

    # Informar un error si alguna corrida fue inválida.
    if validas != len(filas):
        sys.exit(1)


if __name__ == "__main__":
    main()
