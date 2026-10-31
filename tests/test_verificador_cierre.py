"""Pruebas de consistencia del resumen final del P2-11."""

import csv
import tempfile
import unittest
from pathlib import Path

from scripts.verificar_csv import verificar_resumen_cierre


class TestVerificadorCierre(unittest.TestCase):

    def setUp(self):
        # Crear una carpeta temporal independiente.
        self.temporal = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporal.cleanup)
        self.ruta = Path(self.temporal.name) / "resumen.csv"

    def escribir(self, **cambios):
        # Preparar un cierre correcto de diez mensajes.
        fila = {
            "schema_version": "1",
            "instance_name": "/soa_prueba_cierre",
            "accepted": "10",
            "extracted": "10",
            "pending": "0",
            "capacity": "8",
            "first_worker_start_ns": "100",
            "last_dequeue_ns": "200",
            "throughput_msg_s": "100000000",
            "latency_mean_ns": "",
            "latency_p95_ns": "",
            "closing_duration_ns": "500",
            "closing_start_ns": "1000",
            "release_complete_ns": "1500",
        }

        # Permitir modificar campos para simular errores.
        fila.update(cambios)

        with self.ruta.open(
            "w", newline="", encoding="utf-8"
        ) as archivo:
            escritor = csv.DictWriter(
                archivo, fieldnames=['schema_version', 'instance_name', 'capacity', 'accepted', 'extracted', 'pending', 'first_worker_start_ns', 'last_dequeue_ns', 'closing_start_ns', 'release_complete_ns', 'throughput_msg_s', 'latency_mean_ns', 'latency_p95_ns', 'closing_duration_ns']
            )
            escritor.writeheader()
            escritor.writerow(fila)

    def test_cierre_correcto(self):
        self.escribir()
        self.assertEqual(
            verificar_resumen_cierre(self.ruta, 10, 10), []
        )

    def test_aceptados_incorrectos(self):
        self.escribir(accepted="9")
        errores = verificar_resumen_cierre(self.ruta, 10, 10)
        self.assertTrue(any("accepted" in e for e in errores))

    def test_extraidos_incorrectos(self):
        self.escribir(extracted="9")
        errores = verificar_resumen_cierre(self.ruta, 10, 10)
        self.assertTrue(any("extracted" in e for e in errores))

    def test_mensajes_pendientes(self):
        self.escribir(pending="1")
        errores = verificar_resumen_cierre(self.ruta, 10, 10)
        self.assertTrue(any("pendientes" in e for e in errores))

    def test_marcas_temporales_incorrectas(self):
        self.escribir(release_complete_ns="500")
        errores = verificar_resumen_cierre(self.ruta, 10, 10)
        self.assertTrue(any("Liberación" in e for e in errores))

    def test_resumen_incompleto(self):
        self.escribir()
        with self.ruta.open("a", encoding="utf-8") as archivo:
            archivo.write("1,/otra_instancia,1,1,0,100,200\n")
        with self.assertRaises(ValueError):
            verificar_resumen_cierre(self.ruta, 10, 10)


if __name__ == "__main__":
    unittest.main()
