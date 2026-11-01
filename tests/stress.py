import csv
from contextlib import ExitStack
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
WORKERS = 32
MESSAGES = 1000


def interrupted(signum, frame):
    raise RuntimeError("Se agotó el tiempo de la prueba de estrés")


def run_case(capacity):
    runs = ROOT / "results/runs"
    runs.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix=f"stress_{capacity}_", dir=runs))
    name = f"/stress_{uuid.uuid4().hex}"
    processes = []
    created = False

    with ExitStack() as files:
        commands = files.enter_context((directory / "commands.txt").open("w"))

        def start(label, arguments):
            commands.write(shlex.join(arguments) + "\n")
            commands.flush()
            output = files.enter_context((directory / f"{label}.txt").open("w"))
            process = subprocess.Popen(arguments, cwd=ROOT, stdout=output,
                                       stderr=subprocess.STDOUT)
            processes.append(process)
            return process

        def finish(process):
            if process.wait() != 0:
                raise RuntimeError(f"Un proceso falló, revisar {directory}")

        def worker(role, participant):
            arguments = [str(ROOT / role), "--name", name,
                         "--id", str(participant), "--mean-ms", "0",
                         "--seed", str(participant), "--log",
                         str(directory / f"{role}_{participant}.csv")]
            if role == "producer":
                arguments += ["--messages", str(MESSAGES)]
            return start(f"{role}_{participant}", arguments)

        def wait_registered(role):
            finish(start(f"registered_{role}", [
                str(ROOT / "build/tests/worker_control"),
                f"wait-{role}", name, str(WORKERS)]))

        try:
            finish(start("creator", [str(ROOT / "creator"), "--name", name,
                                     "--capacity", str(capacity)]))
            created = True
            producers = [worker("producer", i) for i in range(1, WORKERS + 1)]
            wait_registered("producers")
            consumers = [worker("consumer", i) for i in range(1, WORKERS + 1)]
            wait_registered("consumers")
            for process in producers:
                finish(process)
            summary = directory / "summary.csv"
            finish(start("finisher", [str(ROOT / "finisher"), "--name", name,
                                      "--summary", str(summary)]))
            for process in consumers:
                finish(process)

            logs = []
            for role in ("producer", "consumer"):
                for participant in range(1, WORKERS + 1):
                    path = directory / f"{role}_{participant}.csv"
                    logs.append(str(path))
                    with path.open() as file:
                        rows = list(csv.DictReader(file))
                    if not rows or rows[0]["event"] != "JOIN" or rows[-1]["event"] != "LEAVE":
                        raise RuntimeError(f"Registro o retirada incompletos en {path}")
                    if any(row["role"] != role or int(row["participant_id"]) != participant
                           or not 0 <= int(row["occupancy"]) <= capacity for row in rows):
                        raise RuntimeError(f"Identidad u ocupación incorrecta en {path}")
                    if role == "producer":
                        sequences = [int(row["sequence"]) for row in rows
                                     if row["event"] == "ENQUEUE"]
                        if sequences != list(range(1, MESSAGES + 1)):
                            raise RuntimeError(f"Cuota o secuencia incorrecta en {path}")

            finish(start("verifier", [sys.executable, str(ROOT / "scripts/verificar_csv.py"),
                                      "--validar-ciclo", "--summary", str(summary), *logs]))
            with summary.open() as file:
                row = next(csv.DictReader(file))
            if (int(row["accepted"]) != WORKERS * MESSAGES
                    or int(row["extracted"]) != WORKERS * MESSAGES
                    or int(row["pending"]) != 0):
                raise RuntimeError("El resumen no conserva todos los mensajes")
            if Path("/dev/shm", name[1:]).exists():
                raise RuntimeError("La instancia sigue publicada después del cierre")
            print(f"OK estrés capacidad {capacity}, 32 productores y 32 consumidores, "
                  f"{WORKERS * MESSAGES} mensajes verificados", flush=True)
            print(f"Evidencia {directory.relative_to(ROOT)}", flush=True)
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
            for process in processes:
                process.wait()
            if created:
                Path("/dev/shm", name[1:]).unlink(missing_ok=True)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupted)
    try:
        for capacity in (1, 8):
            run_case(capacity)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"ERROR {error}", file=sys.stderr)
        sys.exit(1)
