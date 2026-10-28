
"""Pruebas iniciales del verificador CSV — Issue P2-11."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from importlib.util import module_from_spec, spec_from_file_location


# Ruta del verificador, independiente del directorio desde el
# que se ejecuten las pruebas.
PROYECTO = Path(__file__).resolve().parents[1]
VERIFICADOR = PROYECTO / "scripts" / "verificar_csv.py"


# Cargar la función del verificador sin ejecutar su interfaz CLI.
spec = spec_from_file_location("verificar_csv", VERIFICADOR)
modulo = module_from_spec(spec)
spec.loader.exec_module(modulo)
generar_payload = modulo.generar_payload


COLUMNAS = (
    "schema_version,role,participant_id,event,timestamp_ns,"
    "state,producer_id,sequence,enqueue_seq,dequeue_seq,"
    "enqueue_time_ns,occupancy,payload_hex"
)



def evento(rol, identificador, tipo, tiempo, productor,
           secuencia, insercion, extraccion, ocupacion):
    """Construye un evento con payload determinista válido."""
    payload = generar_payload(productor, secuencia)

    return (
        f"1,{rol},{identificador},{tipo},{tiempo},OPEN,"
        f"{productor},{secuencia},{insercion},{extraccion},"
        f"{tiempo},{ocupacion},{payload}"
    )


def evento_ciclo(rol, identificador, tipo, tiempo):
    """Genera un registro JOIN o LEAVE válido según el contrato CSV."""
    return {
        "schema_version": "1",
        "role": rol,
        "participant_id": str(identificador),
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


def evento_operacion(rol, identificador, tipo, tiempo):
    """Crea un evento mínimo para probar exclusivamente el ciclo de vida."""
    fila = evento_ciclo(rol, identificador, tipo, tiempo)
    return fila



class TestVerificadorCSV(unittest.TestCase):


    def test_ciclo_vida_correcto(self):
        """Un participante puede registrarse, operar y retirarse."""
        registros = [
            evento_ciclo("producer", 1, "JOIN", 100),
            evento_operacion("producer", 1, "ENQUEUE", 200),
            evento_ciclo("producer", 1, "LEAVE", 300),
        ]

        errores = modulo.verificar_ciclo_vida(registros)
        self.assertEqual(errores, [])

    def test_join_duplicado(self):
        """No se permite registrar dos veces el mismo ID."""
        registros = [
            evento_ciclo("producer", 1, "JOIN", 100),
            evento_ciclo("producer", 1, "JOIN", 200),
            evento_ciclo("producer", 1, "LEAVE", 300),
        ]

        errores = modulo.verificar_ciclo_vida(registros)
        self.assertTrue(any("JOIN duplicado" in e for e in errores))

    def test_leave_sin_join(self):
        """No se puede retirar un participante no registrado."""
        registros = [
            evento_ciclo("consumer", 2, "LEAVE", 100),
        ]

        errores = modulo.verificar_ciclo_vida(registros)
        self.assertTrue(any("LEAVE sin JOIN" in e for e in errores))

    def test_operacion_sin_join(self):
        """No se puede insertar antes del registro."""
        registros = [
            evento_operacion("producer", 3, "ENQUEUE", 100),
        ]

        errores = modulo.verificar_ciclo_vida(registros)
        self.assertTrue(any("Operación sin JOIN" in e for e in errores))

    def test_operacion_despues_leave(self):
        """No se permiten operaciones después de la retirada."""
        registros = [
            evento_ciclo("consumer", 1, "JOIN", 100),
            evento_ciclo("consumer", 1, "LEAVE", 200),
            evento_operacion("consumer", 1, "DEQUEUE", 300),
        ]

        errores = modulo.verificar_ciclo_vida(registros)
        self.assertTrue(
            any("Operación después de LEAVE" in e for e in errores)
        )


    def test_csv_sin_registros(self):
        """Un archivo con encabezado, pero sin eventos, debe rechazarse."""
        resultado = self.ejecutar([])

        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("CSV sin registros", resultado.stderr)


    def ejecutar(self, filas):
        """Crea un CSV temporal y ejecuta el verificador."""
        with tempfile.TemporaryDirectory() as directorio:
            ruta = Path(directorio) / "eventos.csv"
            ruta.write_text(
                COLUMNAS + "\n" + "\n".join(filas) + "\n",
                encoding="utf-8",
            )

            return subprocess.run(
                [sys.executable, str(VERIFICADOR), str(ruta)],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_mensajes_correctos(self):
        """Dos mensajes insertados y extraídos en orden FIFO."""
        filas = [
            evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1),
            evento("producer", 1, "ENQUEUE", 200, 1, 2, 2, "", 2),
            evento("consumer", 1, "DEQUEUE", 300, 1, 1, 1, 1, 1),
            evento("consumer", 1, "DEQUEUE", 400, 1, 2, 2, 2, 0),
        ]

        resultado = self.ejecutar(filas)
        self.assertEqual(resultado.returncode, 0, resultado.stderr)
        self.assertIn("Verificación inicial correcta", resultado.stdout)

    def test_mensaje_perdido(self):
        """Un mensaje aceptado que nunca aparece como extraído."""
        filas = [
            evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1),
        ]

        resultado = self.ejecutar(filas)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("sin extracción", resultado.stderr)

    def test_insercion_duplicada(self):
        """El mismo mensaje aparece insertado dos veces."""
        fila = evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1)

        resultado = self.ejecutar([fila, fila])
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("duplicada", resultado.stderr)

    def test_extraccion_sin_insercion(self):
        """Un consumidor extrae un mensaje inexistente."""
        filas = [
            evento("consumer", 1, "DEQUEUE", 200, 1, 1, 1, 1, 0),
        ]

        resultado = self.ejecutar(filas)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("sin inserción", resultado.stderr)

    def test_violacion_fifo(self):
        """Los mensajes se extraen en orden contrario."""
        filas = [
            evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1),
            evento("producer", 1, "ENQUEUE", 200, 1, 2, 2, "", 2),
            evento("consumer", 1, "DEQUEUE", 300, 1, 2, 2, 1, 1),
            evento("consumer", 1, "DEQUEUE", 400, 1, 1, 1, 2, 0),
        ]

        resultado = self.ejecutar(filas)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("FIFO", resultado.stderr)

    def test_encabezado_incorrecto(self):
        """Un CSV con columnas inválidas debe rechazarse."""
        with tempfile.TemporaryDirectory() as directorio:
            ruta = Path(directorio) / "invalido.csv"
            ruta.write_text("columna1,columna2\n1,2\n", encoding="utf-8")

            resultado = subprocess.run(
                [sys.executable, str(VERIFICADOR), str(ruta)],
                capture_output=True,
                text=True,
                check=False,
            )

        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("encabezado CSV incorrecto", resultado.stderr)


    def test_enqueue_seq_duplicada(self):
        """Dos mensajes distintos reciben el mismo número global."""
        filas = [
            evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1),
            evento("producer", 1, "ENQUEUE", 200, 1, 2, 1, "", 2),
            evento("consumer", 1, "DEQUEUE", 300, 1, 1, 1, 1, 1),
            evento("consumer", 1, "DEQUEUE", 400, 1, 2, 1, 2, 0),
        ]

        resultado = self.ejecutar(filas)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("enqueue_seq duplicada", resultado.stderr)

    def test_enqueue_seq_con_salto(self):
        """Una secuencia global no puede saltar del 1 al 3."""
        filas = [
            evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1),
            evento("producer", 1, "ENQUEUE", 200, 1, 2, 3, "", 2),
            evento("consumer", 1, "DEQUEUE", 300, 1, 1, 1, 1, 1),
            evento("consumer", 1, "DEQUEUE", 400, 1, 2, 3, 2, 0),
        ]

        resultado = self.ejecutar(filas)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("enqueue_seq no consecutiva", resultado.stderr)

    def test_dequeue_seq_duplicada(self):
        """Dos extracciones no pueden tener el mismo número global."""
        filas = [
            evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1),
            evento("producer", 1, "ENQUEUE", 200, 1, 2, 2, "", 2),
            evento("consumer", 1, "DEQUEUE", 300, 1, 1, 1, 1, 1),
            evento("consumer", 1, "DEQUEUE", 400, 1, 2, 2, 1, 0),
        ]

        resultado = self.ejecutar(filas)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("dequeue_seq duplicada", resultado.stderr)

    def test_enqueue_seq_alterada_en_extraccion(self):
        """Una extracción debe conservar el número original del mensaje."""
        filas = [
            evento("producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1),
            evento("consumer", 1, "DEQUEUE", 200, 1, 1, 2, 1, 0),
        ]

        resultado = self.ejecutar(filas)
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("enqueue_seq alterada", resultado.stderr)


    def test_payload_alterado(self):
        """El verificador debe rechazar un mensaje corrupto."""
        fila_entrada = evento(
            "producer", 1, "ENQUEUE", 100, 1, 1, 1, "", 1
        )
        fila_salida = evento(
            "consumer", 1, "DEQUEUE", 200, 1, 1, 1, 1, 0
        )

        # Alteramos un byte del payload de la extracción.
        columnas = fila_salida.split(",")
        columnas[-1] = "00" * 32
        fila_salida = ",".join(columnas)

        resultado = self.ejecutar([fila_entrada, fila_salida])

        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("Payload inválido", resultado.stderr)


    def test_identificador_fuera_de_rango(self):
        """Rechaza productores con ID superior a 1024."""
        fila = evento(
            "producer", 1, "ENQUEUE", 100,
            1025, 1, 1, "", 1
        )

        resultado = self.ejecutar([fila])
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("producer_id fuera del rango", resultado.stderr)

    def test_payload_incompleto(self):
        """Rechaza payloads que no contienen exactamente 32 bytes."""
        fila = evento(
            "producer", 1, "ENQUEUE", 100,
            1, 1, 1, "", 1
        )

        columnas = fila.split(",")
        columnas[-1] = "abcd"
        fila = ",".join(columnas)

        resultado = self.ejecutar([fila])
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("payload_hex inválido", resultado.stderr)

    def test_tiempo_de_insercion_posterior(self):
        """Rechaza extracciones anteriores a la inserción registrada."""
        entrada = evento(
            "producer", 1, "ENQUEUE", 100,
            1, 1, 1, "", 1
        )
        salida = evento(
            "consumer", 1, "DEQUEUE", 200,
            1, 1, 1, 1, 0
        )

        columnas = salida.split(",")
        columnas[10] = "300"
        salida = ",".join(columnas)

        resultado = self.ejecutar([entrada, salida])
        self.assertNotEqual(resultado.returncode, 0)
        self.assertIn("inserción posterior al evento", resultado.stderr)



if __name__ == "__main__":
    unittest.main()
