"""Pruebas de integración entre métricas P2-08 y verificador P2-11."""

import csv
import tempfile
import unittest
from pathlib import Path

from scripts.metricas import calcular_metricas_csv
from scripts.verificar_csv import COLUMNAS, generar_payload


def evento(rol, participante, tipo, tiempo, productor,
           secuencia, orden_insercion, orden_extraccion,
           tiempo_insercion, ocupacion, payload):
    """Construye una fila conforme al contrato CSV v1."""
    return {
        "schema_version": "1",
        "role": rol,
        "participant_id": str(participante),
        "event": tipo,
        "timestamp_ns": str(tiempo),
        "state": "OPEN",
        "producer_id": str(productor),
        "sequence": str(secuencia),
        "enqueue_seq": str(orden_insercion),
        "dequeue_seq": (
            str(orden_extraccion) if orden_extraccion is not None else ""
        ),
        "enqueue_time_ns": str(tiempo_insercion),
        "occupancy": str(ocupacion),
        "payload_hex": payload,
    }


class TestMetricasCSV(unittest.TestCase):

    def test_integracion_correcta(self):
        with tempfile.TemporaryDirectory() as directorio:
            base = Path(directorio)
            productor_csv = base / "productor.csv"
            consumidor_csv = base / "consumidor.csv"

            payload = generar_payload(1, 1)

            insercion = evento(
                "producer", 1, "ENQUEUE", 100,
                1, 1, 1, None, 100, 1, payload
            )

            extraccion = evento(
                "consumer", 1, "DEQUEUE", 150,
                1, 1, 1, 1, 100, 0, payload
            )

            for ruta, filas in (
                (productor_csv, [
                    {
                        **insercion,
                        "event": "JOIN",
                        "timestamp_ns": "90",
                        "producer_id": "",
                        "sequence": "",
                        "enqueue_seq": "",
                        "dequeue_seq": "",
                        "enqueue_time_ns": "",
                        "payload_hex": "",
                        "occupancy": "0",
                    },
                    insercion,
                    {
                        **insercion,
                        "event": "LEAVE",
                        "timestamp_ns": "110",
                        "producer_id": "",
                        "sequence": "",
                        "enqueue_seq": "",
                        "dequeue_seq": "",
                        "enqueue_time_ns": "",
                        "payload_hex": "",
                    },
                ]),
                (consumidor_csv, [
                    {
                        **extraccion,
                        "event": "JOIN",
                        "timestamp_ns": "95",
                        "producer_id": "",
                        "sequence": "",
                        "enqueue_seq": "",
                        "dequeue_seq": "",
                        "enqueue_time_ns": "",
                        "payload_hex": "",
                        "occupancy": "1",
                    },
                    extraccion,
                    {
                        **extraccion,
                        "event": "LEAVE",
                        "timestamp_ns": "160",
                        "producer_id": "",
                        "sequence": "",
                        "enqueue_seq": "",
                        "dequeue_seq": "",
                        "enqueue_time_ns": "",
                        "payload_hex": "",
                    },
                ]),
            ):
                with ruta.open("w", newline="", encoding="utf-8") as archivo:
                    escritor = csv.DictWriter(archivo, fieldnames=COLUMNAS)
                    escritor.writeheader()
                    escritor.writerows(filas)

            resultado = calcular_metricas_csv(
                [productor_csv, consumidor_csv]
            )

            self.assertEqual(resultado["accepted"], 1)
            self.assertEqual(resultado["extracted"], 1)
            self.assertEqual(resultado["latency_mean_ns"], 50)
            self.assertEqual(resultado["latency_p95_ns"], 50)

    def test_csv_inexistente(self):
        with self.assertRaises(OSError):
            calcular_metricas_csv(
                ["/ruta/inexistente/archivo.csv"]
            )


    def test_payload_corrupto(self):
        """Rechaza datos corruptos antes de calcular métricas."""
        with tempfile.TemporaryDirectory() as directorio:
            ruta = Path(directorio) / "eventos.csv"

            fila = evento(
                "producer", 1, "ENQUEUE", 100,
                1, 1, 1, None, 100, 1, "0" * 64
            )

            with ruta.open("w", newline="", encoding="utf-8") as archivo:
                escritor = csv.DictWriter(archivo, fieldnames=COLUMNAS)
                escritor.writeheader()
                escritor.writerow(fila)

            with self.assertRaises(ValueError) as contexto:
                calcular_metricas_csv([ruta])

            self.assertIn("Payload inválido", str(contexto.exception))



    def test_rechaza_operaciones_sin_join(self):
        """No debe calcular métricas de participantes sin JOIN."""
        with tempfile.TemporaryDirectory() as directorio:
            base = Path(directorio)
            productor = base / "productor.csv"
            consumidor = base / "consumidor.csv"

            payload = generar_payload(1, 1)

            filas = (
                (productor, [
                    evento("producer", 1, "ENQUEUE", 100,
                           1, 1, 1, None, 100, 1, payload)
                ]),
                (consumidor, [
                    evento("consumer", 1, "DEQUEUE", 150,
                           1, 1, 1, 1, 100, 0, payload)
                ]),
            )

            for archivo_csv, registros in filas:
                with archivo_csv.open(
                    "w", newline="", encoding="utf-8"
                ) as archivo:
                    escritor = csv.DictWriter(
                        archivo, fieldnames=COLUMNAS
                    )
                    escritor.writeheader()
                    escritor.writerows(registros)

            with self.assertRaisesRegex(ValueError, "sin JOIN"):
                calcular_metricas_csv([productor, consumidor])


if __name__ == "__main__":
    unittest.main()
