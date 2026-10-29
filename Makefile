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

test-options: all $(OPTIONS_TEST)
	./$(OPTIONS_TEST)
	bash tests/arguments.sh

# Ejecuta las pruebas de argumentos y las pruebas Python disponibles
test: test-options smoke test-python test-csv-writer
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
