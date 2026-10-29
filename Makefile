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

test-options: all $(OPTIONS_TEST)
	./$(OPTIONS_TEST)
	bash tests/arguments.sh

# Ejecuta las pruebas de argumentos y las pruebas Python disponibles
test: test-options smoke test-python
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

-include $(OBJECTS:.o=.d)
