
"""Pruebas de integración del ciclo de vida del P2-11."""

import csv
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROYECTO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROYECTO / "scripts"))

from verificar_csv import generar_payload  # noqa: E402

VERIFICADOR = PROYECTO / "scripts" / "verificar_csv.py"

COLUMNAS = [
    "schema_version", "role", "participant_id", "event",
    "timestamp_ns", "state", "producer_id", "sequence",
    "enqueue_seq", "dequeue_seq", "enqueue_time_ns",
    "occupancy", "payload_hex",
]


def crear_evento(rol, participante, tipo, tiempo):
    """Crear un evento de registro o retirada."""
    return {
        "schema_version": "1",
        "role": rol,
        "participant_id": str(participante),
        "event": tipo,
        "timestamp_ns": str(tiempo),
        "state": "OPEN",
        "producer_id": "",
        "sequence": "",
        "enqueue_seq": "",
        "dequeue_seq": "",
        "enqueue_time_ns": "",
        "occupancy": "0",
        "payload_hex": "",
    }


def crear_mensaje(rol, participante, tipo, tiempo, ocupacion):
    """Crear un mensaje válido de un productor y un consumidor."""
    fila = crear_evento(rol, participante, tipo, tiempo)
    fila.update({
        "producer_id": "1",
        "sequence": "1",
        "enqueue_seq": "1",
        "dequeue_seq": "1" if tipo == "DEQUEUE" else "",
        "enqueue_time_ns": "200",
        "occupancy": str(ocupacion),
        "payload_hex": generar_payload(1, 1),
    })
    return fila


class TestIntegracionCiclo(unittest.TestCase):

    def ejecutar(self, productor, consumidor):
        """Guardar dos CSV diferentes y ejecutar el verificador real."""
        with tempfile.TemporaryDirectory() as temporal:
            rutas = []

            for nombre, filas in (
                ("productor.csv", productor),
                ("consumidor.csv", consumidor),
            ):
                ruta = Path(temporal) / nombre

                with ruta.open("w", encoding="utf-8", newline="") as archivo:
                    escritor = csv.DictWriter(archivo, fieldnames=COLUMNAS)
                    escritor.writeheader()
                    escritor.writerows(filas)

                rutas.append(str(ruta))

            return subprocess.run(
                [
                    sys.executable,
                    str(VERIFICADOR),
                    "--validar-ciclo",
                    *rutas,
                ],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_ciclo_completo_valido(self):
        """Ambos procesos se registran, operan y se retiran."""
        productor = [
            crear_evento("producer", 1, "JOIN", 100),
            crear_mensaje("producer", 1, "ENQUEUE", 200, 1),
            crear_evento("producer", 1, "LEAVE", 300),
        ]

        consumidor = [
            crear_evento("consumer", 1, "JOIN", 110),
            crear_mensaje("consumer", 1, "DEQUEUE", 250, 0),
            crear_evento("consumer", 1, "LEAVE", 350),
        ]

        resultado = self.ejecutar(productor, consumidor)
        self.assertEqual(resultado.returncode, 0, resultado.stderr)

    def test_operacion_sin_registro(self):
        """Un productor no puede insertar sin JOIN."""
        productor = [
            crear_mensaje("producer", 1, "ENQUEUE", 200, 1),
        ]

        consumidor = [
            crear_evento("consumer", 1, "JOIN", 110),
            crear_mensaje("consumer", 1, "DEQUEUE", 250, 0),
            crear_evento("consumer", 1, "LEAVE", 350),
        ]

        resultado = self.ejecutar(productor, consumidor)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("Operación sin JOIN", resultado.stderr)

    def test_operacion_despues_de_retirada(self):
        """No se permite insertar después de LEAVE."""
        productor = [
            crear_evento("producer", 1, "JOIN", 100),
            crear_evento("producer", 1, "LEAVE", 150),
            crear_mensaje("producer", 1, "ENQUEUE", 200, 1),
        ]

        consumidor = [
            crear_evento("consumer", 1, "JOIN", 110),
            crear_mensaje("consumer", 1, "DEQUEUE", 250, 0),
            crear_evento("consumer", 1, "LEAVE", 350),
        ]

        resultado = self.ejecutar(productor, consumidor)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("Operación después de LEAVE", resultado.stderr)


if __name__ == "__main__":
    unittest.main()
