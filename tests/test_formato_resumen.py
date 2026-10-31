
"""Pruebas del formato completo del resumen P2-11."""

import csv
import tempfile
import unittest
from pathlib import Path

from scripts.verificar_csv import verificar_resumen_cierre


COLUMNAS = [
    "schema_version", "instance_name", "capacity",
    "accepted", "extracted", "pending",
    "first_worker_start_ns", "last_dequeue_ns",
    "closing_start_ns", "release_complete_ns",
    "throughput_msg_s", "latency_mean_ns",
    "latency_p95_ns", "closing_duration_ns",
]


class TestFormatoResumen(unittest.TestCase):

    def setUp(self):
        # Crear un directorio temporal para cada prueba.
        self.temporal = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporal.cleanup)
        self.ruta = Path(self.temporal.name) / "resumen.csv"

    def escribir(self, columnas=None, **cambios):
        # Construir un resumen válido de diez mensajes.
        fila = {
            "schema_version": "1",
            "instance_name": "/prueba_resumen",
            "capacity": "8",
            "accepted": "10",
            "extracted": "10",
            "pending": "0",
            "first_worker_start_ns": "100",
            "last_dequeue_ns": "200",
            "closing_start_ns": "300",
            "release_complete_ns": "400",
            "throughput_msg_s": "100000000",
            "latency_mean_ns": "",
            "latency_p95_ns": "",
            "closing_duration_ns": "100",
        }

        # Aplicar una alteración para cada prueba.
        fila.update(cambios)
        campos = columnas or COLUMNAS

        with self.ruta.open("w", newline="", encoding="utf-8") as archivo:
            escritor = csv.DictWriter(
                archivo, fieldnames=campos, extrasaction="ignore"
            )
            escritor.writeheader()
            escritor.writerow(fila)

    def test_resumen_completo_correcto(self):
        # El resumen correcto debe aceptarse.
        self.escribir()
        self.assertEqual(
            verificar_resumen_cierre(self.ruta, 10, 10), []
        )

    def test_columna_obligatoria_ausente(self):
        # Eliminar una columna del formato oficial.
        columnas = [
            campo for campo in COLUMNAS
            if campo != "closing_duration_ns"
        ]
        self.escribir(columnas=columnas)

        with self.assertRaises(ValueError):
            verificar_resumen_cierre(self.ruta, 10, 10)

    def test_capacidad_fuera_de_rango(self):
        # La capacidad cero no es válida.
        self.escribir(capacity="0")
        with self.assertRaisesRegex(ValueError, "capacity"):
            verificar_resumen_cierre(self.ruta, 10, 10)

    def test_duracion_de_cierre_incorrecta(self):
        # La duración debe coincidir con las marcas temporales.
        self.escribir(closing_duration_ns="999")
        errores = verificar_resumen_cierre(self.ruta, 10, 10)
        self.assertTrue(errores)

    def test_rendimiento_inconsistente(self):
        # El rendimiento debe concordar con tiempo y mensajes.
        self.escribir(throughput_msg_s="1")
        errores = verificar_resumen_cierre(self.ruta, 10, 10)
        self.assertTrue(errores)


if __name__ == "__main__":
    unittest.main()
