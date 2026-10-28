"""Pruebas de integración de manifiestos auxiliares del P2-11."""
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
SCRIPT = RAIZ / "scripts" / "verificar_csv.py"
COLUMNAS = ["schema_version", "role", "participant_id", "event", "timestamp_ns",
            "state", "producer_id", "sequence", "enqueue_seq", "dequeue_seq",
            "enqueue_time_ns", "occupancy", "payload_hex"]


class TestManifiesto(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.csv = self.base / "p1.csv"
        with self.csv.open("w", encoding="utf-8", newline="") as archivo:
            escritor = csv.DictWriter(archivo, fieldnames=COLUMNAS)
            escritor.writeheader()
            for tipo, tiempo in [("JOIN", "10"), ("LEAVE", "20")]:
                fila = dict.fromkeys(COLUMNAS, "")
                fila.update(schema_version="1", role="producer", participant_id="1",
                            event=tipo, timestamp_ns=tiempo, state="OPEN", occupancy="0")
                escritor.writerow(fila)
        self.manifiesto = self.base / "manifiesto.json"

    def guardar(self, archivos):
        self.manifiesto.write_text(json.dumps({"schema_version": 1, "files": archivos}),
                                  encoding="utf-8")

    def correr(self, *extra):
        return subprocess.run([sys.executable, str(SCRIPT), "--manifiesto",
                               str(self.manifiesto), *extra], capture_output=True, text=True)

    def test_manifiesto_valido(self):
        self.guardar(["p1.csv"])
        resultado = self.correr("--validar-ciclo")
        self.assertEqual(resultado.returncode, 0, resultado.stderr)

    def test_archivo_faltante(self):
        self.guardar(["p1.csv", "c1.csv"])
        resultado = self.correr()
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("CSV faltante", resultado.stderr)

    def test_archivo_no_declarado_en_argumentos(self):
        self.guardar(["p1.csv"])
        extra = self.base / "otro.csv"
        extra.write_text(self.csv.read_text(encoding="utf-8"), encoding="utf-8")
        resultado = self.correr(str(self.csv), str(extra))
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("no coinciden", resultado.stderr)

    def test_ruta_peligrosa(self):
        self.guardar(["../fuera.csv"])
        resultado = self.correr()
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("no permitida", resultado.stderr)

    def test_entrada_duplicada(self):
        self.guardar(["p1.csv", "p1.csv"])
        resultado = self.correr()
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("duplicado", resultado.stderr)
