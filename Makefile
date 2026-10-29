CC = gcc
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2 -g
override CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Werror -pthread
override LDLIBS += -pthread

PYTHON ?= python3
PROGRAMS := creator producer consumer finisher
COMMON_OBJECTS := build/common/app.o build/common/options.o
APP_OBJECTS := $(PROGRAMS:%=build/apps/%.o)
OBJECTS := $(COMMON_OBJECTS) $(APP_OBJECTS) build/tests/options_test.o
OPTIONS_TEST := build/tests/options_test
WORKER_METRICS_TEST := build/tests/worker_metrics_test
WORKER_METRICS_INVALID_TEST := build/tests/worker_metrics_invalid_test
WORKER_METRICS_FULL_TEST := build/tests/worker_metrics_full_test
CSV_WRITER_TEST := build/tests/csv_writer_test
CSV_WRITER_INVALID_TEST := build/tests/csv_writer_invalid_test
CSV_WRITER_IO_TEST := build/tests/csv_writer_io_test
CSV_WRITER_STATE_TEST := build/tests/csv_writer_state_test
CSV_WRITER_FULL_TEST := build/tests/csv_writer_full_test

.PHONY: all clean smoke test test-options test-python help
all: $(PROGRAMS)

$(PROGRAMS): %: build/apps/%.o $(COMMON_OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

build/%.o: src/%.c
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

smoke: all
	bash tests/smoke.sh

build/tests/%.o: tests/%.c
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(OPTIONS_TEST): build/tests/options_test.o build/common/options.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(CSV_WRITER_TEST): build/tests/csv_writer_test.o build/common/csv_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(CSV_WRITER_INVALID_TEST): build/tests/csv_writer_invalid_test.o build/common/csv_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(CSV_WRITER_IO_TEST): build/tests/csv_writer_io_test.o build/common/csv_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(CSV_WRITER_STATE_TEST): build/tests/csv_writer_state_test.o build/common/csv_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(CSV_WRITER_FULL_TEST): build/tests/csv_writer_full_test.o build/common/csv_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

.PHONY: test-csv-writer
test-csv-writer: $(CSV_WRITER_TEST) $(CSV_WRITER_INVALID_TEST) $(CSV_WRITER_IO_TEST) $(CSV_WRITER_STATE_TEST) $(CSV_WRITER_FULL_TEST)
	mkdir -p results/runs
	./$(CSV_WRITER_TEST) results/runs/prueba_productor.csv results/runs/prueba_consumidor.csv
	$(PYTHON) scripts/verificar_csv.py results/runs/prueba_productor.csv results/runs/prueba_consumidor.csv --validar-ciclo
	./$(CSV_WRITER_INVALID_TEST) results/runs/prueba_csv_invalido.csv
	./$(CSV_WRITER_IO_TEST)
	./$(CSV_WRITER_FULL_TEST)
	./$(CSV_WRITER_STATE_TEST) results/runs/prueba_csv_estado.csv
	$(PYTHON) -c 'from pathlib import Path; p = Path("results/runs/prueba_csv_invalido.csv"); assert len(p.read_text().splitlines()) == 1, "CSV invalido contiene filas parciales"'

$(WORKER_METRICS_TEST): build/tests/worker_metrics_test.o build/common/worker_metrics.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(WORKER_METRICS_INVALID_TEST): build/tests/worker_metrics_invalid_test.o build/common/worker_metrics.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(WORKER_METRICS_FULL_TEST): build/tests/worker_metrics_full_test.o build/common/worker_metrics.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

.PHONY: test-worker-metrics
test-worker-metrics: $(WORKER_METRICS_TEST) $(WORKER_METRICS_INVALID_TEST) $(WORKER_METRICS_FULL_TEST)
	mkdir -p results/runs
	./$(WORKER_METRICS_TEST) results/runs/prueba_worker_metrics.csv
	./$(WORKER_METRICS_INVALID_TEST) results/runs/prueba_worker_metrics_invalido.csv
	./$(WORKER_METRICS_FULL_TEST)
	$(PYTHON) -c 'import csv; from pathlib import Path; p=Path("results/runs/prueba_worker_metrics.csv"); r=csv.DictReader(p.open()); rows=list(r); assert len(r.fieldnames)==8 and len(rows)==2'
	$(PYTHON) -c 'from pathlib import Path; p=Path("results/runs/prueba_worker_metrics_invalido.csv"); assert len(p.read_text().splitlines())==1'

test-options: all $(OPTIONS_TEST)
	./$(OPTIONS_TEST)
	bash tests/arguments.sh

# Ejecuta las pruebas de argumentos y las pruebas Python disponibles
test: test-options smoke test-python test-csv-writer test-worker-metrics test-summary-writer
	@echo 'OK: pruebas disponibles ejecutadas.'
	@echo 'PENDIENTE: pruebas funcionales C del buffer, concurrencia y cierre.'

test-python:
	@if find tests -maxdepth 1 -type f -name 'test_*.py' -print -quit | grep -q .; then \
		command -v "$(PYTHON)" >/dev/null 2>&1 || { \
			echo 'ERROR: no se encontró el intérprete Python: $(PYTHON)' >&2; exit 1; }; \
		echo 'Ejecutando pruebas Python...'; \
		"$(PYTHON)" -m unittest discover -s tests -p 'test_*.py' -v; \
	else \
		echo 'INFO: no hay pruebas Python fuente en esta rama; se omiten.'; \
	fi

clean:
	rm -rf build
	rm -f $(PROGRAMS)

help:
	@echo 'all: compilar los cuatro ejecutables con validacion de argumentos'
	@echo 'smoke: comprobar la interfaz inicial de la cáscara'
	@echo 'test: ejecutar argumentos, smoke y pruebas Python disponibles'
	@echo 'test-options: comprobar el parser y los argumentos de los cuatro ejecutables'
	@echo 'test-python: ejecutar los test_*.py disponibles sin necesitar los ejecutables C'
	@echo 'clean: eliminar objetos y ejecutables generados'

-include $(OBJECTS:.o=.d) \
    build/common/csv_writer.d \
    build/tests/csv_writer_test.d \
    build/tests/csv_writer_invalid_test.d \
    build/tests/csv_writer_io_test.d \
    build/tests/csv_writer_state_test.d \
    build/tests/csv_writer_full_test.d

-include build/common/worker_metrics.d \
    build/tests/worker_metrics_test.d \
    build/tests/worker_metrics_invalid_test.d \
    build/tests/worker_metrics_full_test.d


SUMMARY_WRITER_TESTS := \
    build/tests/summary_writer_test \
    build/tests/summary_writer_empty_test \
    build/tests/summary_writer_invalid_test \
    build/tests/summary_writer_full_test

build/tests/summary_writer_test: build/tests/summary_writer_test.o build/common/summary_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -lm -o "$@"

build/tests/summary_writer_empty_test: build/tests/summary_writer_empty_test.o build/common/summary_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -lm -o "$@"

build/tests/summary_writer_invalid_test: build/tests/summary_writer_invalid_test.o build/common/summary_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -lm -o "$@"

build/tests/summary_writer_full_test: build/tests/summary_writer_full_test.o build/common/summary_writer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -lm -o "$@"

.PHONY: test-summary-writer
test-summary-writer: $(SUMMARY_WRITER_TESTS)
	mkdir -p results/runs
	./build/tests/summary_writer_test results/runs/prueba_summary.csv
	./build/tests/summary_writer_empty_test results/runs/prueba_summary_vacio.csv
	./build/tests/summary_writer_invalid_test results/runs/prueba_summary_invalido.csv
	./build/tests/summary_writer_full_test
	$(PYTHON) -c 'import csv; from pathlib import Path; p=Path("results/runs/prueba_summary.csv"); r=csv.DictReader(p.open()); rows=list(r); assert len(r.fieldnames)==14 and len(rows)==1 and rows[0]["accepted"]=="10"'
	$(PYTHON) -c 'import csv; from pathlib import Path; p=Path("results/runs/prueba_summary_vacio.csv"); rows=list(csv.DictReader(p.open())); assert len(rows)==1 and rows[0]["throughput_msg_s"]=="" and rows[0]["latency_mean_ns"]==""'
	$(PYTHON) -c 'from pathlib import Path; p=Path("results/runs/prueba_summary_invalido.csv"); assert len(p.read_text().splitlines())==1'

-include build/common/summary_writer.d \
    build/tests/summary_writer_test.d \
    build/tests/summary_writer_empty_test.d \
    build/tests/summary_writer_invalid_test.d \
    build/tests/summary_writer_full_test.d
