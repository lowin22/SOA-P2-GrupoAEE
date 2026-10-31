import csv
import itertools
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
SERIAL = itertools.count()


@unittest.skipUnless((ROOT / "include/soa/csv_writer.h").is_file(),
                     "Pendiente integrar el escritor CSV de P2-08 y activar los trabajadores")
class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.name = f"/worker_test_{os.getpid()}_{next(SERIAL)}"
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.processes = []
        subprocess.run([str(ROOT / "creator"), "--name", self.name,
                        "--capacity", "1"], check=True, capture_output=True)

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if process.stderr:
                process.stderr.close()
        Path("/dev/shm", self.name[1:]).unlink(missing_ok=True)
        self.temp.cleanup()

    def start(self, role, mean="0", seed="1", messages=None, log=None):
        path = log or self.directory / f"{role}.csv"
        args = [str(ROOT / role), "--name", self.name, "--id", "1",
                "--mean-ms", mean, "--seed", seed, "--log", str(path)]
        if messages is not None:
            args += ["--messages", str(messages)]
        process = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True)
        self.processes.append(process)
        return process

    def control(self, command="inspect"):
        result = subprocess.run([str(ROOT / "build/tests/worker_control"),
                                 command, self.name], check=True,
                                capture_output=True, text=True, timeout=5)
        return {key: int(value) for key, value in
                (entry.split("=") for entry in result.stdout.split())}

    def wait_state(self, predicate):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            state = self.control()
            if predicate(state):
                return state
            time.sleep(0.005)
        self.fail("No se alcanzo el estado esperado")

    def finish(self, process, success=True):
        result = process.wait(timeout=10)
        error = process.stderr.read()
        if success:
            self.assertEqual(result, 0, error)
        else:
            self.assertNotEqual(result, 0)

    def verify_logs(self):
        result = subprocess.run([
            sys.executable, str(ROOT / "scripts/verificar_csv.py"), "--validar-ciclo",
            str(self.directory / "producer.csv"), str(self.directory / "consumer.csv")
        ], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def exchange(self, mean):
        consumer = self.start("consumer", mean=mean)
        self.wait_state(lambda s: s["consumers"] == 1)
        self.assertIsNone(consumer.poll())
        producer = self.start("producer", mean=mean, messages=100)
        self.finish(producer)
        self.control("close")
        self.finish(consumer)
        state = self.control()
        self.assertEqual(state["accepted"], 100)
        self.assertEqual(state["extracted"], 100)
        self.assertEqual(state["produced"], 100)
        self.assertEqual(state["consumed"], 100)
        self.assertEqual(state["producers"] + state["consumers"], 0)
        self.assertGreater(state["mutex_wait"], 0)
        self.assertGreater(state["condition_wait"], 0)
        self.verify_logs()

    def test_exchange_without_pauses(self):
        self.exchange("0")

    def test_exchange_with_pauses(self):
        self.exchange("1")

    def test_continuous_producer(self):
        consumer = self.start("consumer")
        self.wait_state(lambda s: s["consumers"] == 1)
        producer = self.start("producer")
        self.wait_state(lambda s: s["accepted"] >= 20)
        self.control("close")
        self.finish(producer)
        self.finish(consumer)
        state = self.control()
        self.assertEqual(state["accepted"], state["extracted"])
        self.assertEqual(state["occupancy"], 0)
        self.verify_logs()

    def test_consumer_waits_in_empty_buffer(self):
        consumer = self.start("consumer")
        self.wait_state(lambda s: s["consumers"] == 1)
        self.assertIsNone(consumer.poll())
        self.control("close")
        self.finish(consumer)
        self.assertEqual(self.control()["extracted"], 0)


    def test_p212_reanudacion_buffer_vacio(self):
        """El consumidor continúa cuando llega un productor."""

        # Iniciar el consumidor sin mensajes disponibles.
        consumer = self.start("consumer", mean="0")

        # Confirmar que está registrado y que el buffer está vacío.
        self.wait_state(
            lambda s: s["consumers"] == 1 and s["occupancy"] == 0
        )
        self.assertIsNone(consumer.poll())

        # Incorporar un productor con una cuota de dos mensajes.
        producer = self.start("producer", mean="0", messages=2)
        self.finish(producer)

        # Comprobar que ambos mensajes fueron extraídos.
        self.wait_state(
            lambda s: s["accepted"] == 2 and s["extracted"] == 2
        )

        # Solicitar el cierre y esperar al consumidor.
        self.control("close")
        self.finish(consumer)

        # Comprobar conservación y registros CSV.
        estado = self.control()
        self.assertEqual(estado["occupancy"], 0)
        self.assertEqual(estado["producers"] + estado["consumers"], 0)
        self.assertGreater(estado["condition_wait"], 0)
        self.verify_logs()

    def test_p212_reanudacion_buffer_lleno(self):
        """El productor continúa cuando llega un consumidor."""

        # Iniciar dos inserciones con capacidad de un mensaje.
        producer = self.start("producer", mean="0", messages=2)

        # Confirmar que el primer mensaje ocupa todo el buffer.
        self.wait_state(
            lambda s: s["producers"] == 1
            and s["occupancy"] == 1
            and s["accepted"] == 1
        )

        # El productor todavía no puede terminar su cuota.
        self.assertIsNone(producer.poll())

        # Incorporar un consumidor para liberar espacio.
        consumer = self.start("consumer", mean="0")

        # Comprobar que el productor logra terminar.
        self.finish(producer)

        # Comprobar que ambos mensajes fueron extraídos.
        self.wait_state(
            lambda s: s["accepted"] == 2 and s["extracted"] == 2
        )

        # Cerrar la instancia y esperar al consumidor.
        self.control("close")
        self.finish(consumer)

        # Verificar que todos los mensajes se conservaron.
        estado = self.control()
        self.assertEqual(estado["occupancy"], 0)
        self.assertEqual(estado["producers"] + estado["consumers"], 0)
        self.assertGreater(estado["condition_wait"], 0)
        self.verify_logs()

    def test_close_interrupts_producer_pause(self):
        consumer = self.start("consumer")
        self.wait_state(lambda s: s["consumers"] == 1)
        producer = self.start("producer", mean="1000", seed="3")
        self.wait_state(lambda s: s["producers"] == 1 and s["accepted"] == 0)
        start = time.monotonic()
        self.control("close")
        self.finish(producer)
        self.assertLess(time.monotonic() - start, 1.0)
        self.finish(consumer)
        self.assertEqual(self.control()["accepted"], 0)
        self.verify_logs()

    def test_notifications_do_not_restart_producer_pause(self):
        consumer = self.start("consumer")
        self.wait_state(lambda s: s["consumers"] == 1)
        producer = self.start("producer", mean="1000", seed="3", messages=1)
        self.wait_state(lambda s: s["producers"] == 1)
        deadline = time.monotonic() + 2.5
        while producer.poll() is None and time.monotonic() < deadline:
            self.control("wake")
            time.sleep(0.05)
        self.finish(producer)
        self.control("close")
        self.finish(consumer)
        with (self.directory / "producer.csv").open() as file:
            rows = list(csv.DictReader(file))
        joined = next(row for row in rows if row["event"] == "JOIN")
        inserted = next(row for row in rows if row["event"] == "ENQUEUE")
        elapsed = int(inserted["timestamp_ns"]) - int(joined["timestamp_ns"])
        self.assertGreater(elapsed, 2_000_000_000)
        self.assertLess(elapsed, 3_500_000_000)
        self.verify_logs()

    def test_consumer_finishes_processing_after_close(self):
        consumer = self.start("consumer", mean="1000", seed="3")
        self.wait_state(lambda s: s["consumers"] == 1)
        producer = self.start("producer", messages=1)
        self.finish(producer)
        self.wait_state(lambda s: s["extracted"] == 1 and s["consumers"] == 1)
        state = self.control("close")
        self.assertEqual(state["consumers"], 1)
        self.finish(consumer)
        with (self.directory / "consumer.csv").open() as file:
            rows = list(csv.DictReader(file))
        taken = next(row for row in rows if row["event"] == "DEQUEUE")
        left = next(row for row in rows if row["event"] == "LEAVE")
        self.assertGreater(int(left["timestamp_ns"]) - int(taken["timestamp_ns"]),
                           500_000_000)
        self.verify_logs()

    def test_log_error_does_not_leave_active_participant(self):
        producer = self.start("producer", messages=1, log=Path("/dev/full"))
        self.finish(producer, success=False)
        self.assertEqual(self.control()["producers"], 0)

    def test_log_open_error_before_registration(self):
        producer = self.start("producer", messages=1,
                              log=self.directory / "missing" / "log.csv")
        self.finish(producer, success=False)
        self.assertEqual(self.control()["producers"], 0)


if __name__ == "__main__":
    unittest.main()
