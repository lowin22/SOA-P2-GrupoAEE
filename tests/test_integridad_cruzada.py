"""Pruebas adicionales de integridad cruzada, Issue P2-11."""
import csv
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
VERIFICADOR = RAIZ / "scripts" / "verificar_csv.py"
spec = importlib.util.spec_from_file_location("verificador_integridad", VERIFICADOR)
modulo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(modulo)


def fila(rol, participante, evento, tiempo, identidad, global_seq, extraccion, insercion):
    productor, secuencia = identidad
    return dict(
        schema_version="1", role=rol, participant_id=str(participante),
        event=evento, timestamp_ns=str(tiempo), state="OPEN",
        producer_id=str(productor), sequence=str(secuencia),
        enqueue_seq=str(global_seq), dequeue_seq=str(extraccion) if extraccion else "",
        enqueue_time_ns=str(insercion), occupancy="1" if evento == "ENQUEUE" else "0",
        payload_hex=modulo.generar_payload(productor, secuencia),
    )


class TestIntegridadCruzada(unittest.TestCase):
    def ejecutar(self, productores, consumidores, invertir_archivos=False):
        with tempfile.TemporaryDirectory() as temporal:
            rutas = []
            for nombre, filas in (("productor.csv", productores), ("consumidor.csv", consumidores)):
                ruta = Path(temporal) / nombre
                with ruta.open("w", encoding="utf-8", newline="") as archivo:
                    escritor = csv.DictWriter(archivo, fieldnames=modulo.COLUMNAS)
                    escritor.writeheader()
                    escritor.writerows(filas)
                rutas.append(str(ruta))
            if invertir_archivos:
                rutas.reverse()
            return subprocess.run([sys.executable, str(VERIFICADOR), *rutas],
                                  capture_output=True, text=True, check=False)

    def test_marca_insercion_alterada(self):
        productor = [fila("producer", 1, "ENQUEUE", 100, (1, 1), 1, None, 100)]
        consumidor = [fila("consumer", 1, "DEQUEUE", 300, (1, 1), 1, 1, 200)]
        resultado = self.ejecutar(productor, consumidor)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("enqueue_time_ns alterada", resultado.stderr)

    def test_orden_archivos_no_altera_fifo(self):
        productor = [fila("producer", 1, "ENQUEUE", 100, (1, 1), 1, None, 100),
                     fila("producer", 1, "ENQUEUE", 200, (1, 2), 2, None, 200)]
        consumidor = [fila("consumer", 1, "DEQUEUE", 300, (1, 1), 1, 1, 100),
                      fila("consumer", 1, "DEQUEUE", 400, (1, 2), 2, 2, 200)]
        resultado = self.ejecutar(productor, consumidor, invertir_archivos=True)
        self.assertEqual(resultado.returncode, 0, resultado.stderr)


if __name__ == "__main__":
    unittest.main()
