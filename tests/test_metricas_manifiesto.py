"""Pruebas del cálculo de métricas mediante manifiestos P2-08."""

import csv
import json
import tempfile
import unittest
from pathlib import Path

from scripts.metricas import calcular_metricas_manifiesto
from scripts.verificar_csv import COLUMNAS, generar_payload


class TestMetricasManifiesto(unittest.TestCase):

    def crear_manifiesto(self, base, nombres):
        ruta = base / "manifiesto.json"
        ruta.write_text(
            json.dumps({"schema_version": 1, "files": nombres}),
            encoding="utf-8",
        )
        return ruta

    def test_manifiesto_valido(self):
        with tempfile.TemporaryDirectory() as directorio:
            base = Path(directorio)
            ruta_csv = base / "productor.csv"

            # Una ejecución válida sin mensajes: JOIN y LEAVE.
            comun = {
                campo: "" for campo in COLUMNAS
            }
            comun.update({
                "schema_version": "1",
                "role": "producer",
                "participant_id": "1",
                "state": "OPEN",
                "occupancy": "0",
            })

            filas = [
                {**comun, "event": "JOIN", "timestamp_ns": "100"},
                {**comun, "event": "LEAVE", "timestamp_ns": "200"},
            ]

            with ruta_csv.open("w", newline="", encoding="utf-8") as archivo:
                escritor = csv.DictWriter(archivo, fieldnames=COLUMNAS)
                escritor.writeheader()
                escritor.writerows(filas)

            manifiesto = self.crear_manifiesto(base, ["productor.csv"])
            resultado = calcular_metricas_manifiesto(manifiesto)

            self.assertEqual(resultado["accepted"], 0)
            self.assertEqual(resultado["extracted"], 0)
            self.assertIsNone(resultado["latency_mean_ns"])
            self.assertIsNone(resultado["latency_p95_ns"])

    def test_archivo_faltante(self):
        with tempfile.TemporaryDirectory() as directorio:
            base = Path(directorio)
            manifiesto = self.crear_manifiesto(
                base, ["productor.csv", "consumidor.csv"]
            )

            with self.assertRaisesRegex(ValueError, "CSV faltante"):
                calcular_metricas_manifiesto(manifiesto)

    def test_ruta_no_permitida(self):
        with tempfile.TemporaryDirectory() as directorio:
            base = Path(directorio)
            manifiesto = self.crear_manifiesto(
                base, ["../otro_directorio/archivo.csv"]
            )

            with self.assertRaisesRegex(ValueError, "no permitida"):
                calcular_metricas_manifiesto(manifiesto)


if __name__ == "__main__":
    unittest.main()
