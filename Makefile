CC = gcc
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2 -g
override CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Werror -pthread
override LDLIBS += -pthread -lm

PYTHON ?= python3
PROGRAMS := creator producer consumer finisher
COMMON_OBJECTS := build/common/app.o build/common/options.o \
	build/common/instance.o build/common/participants.o build/common/buffer.o \
	build/common/message.o build/common/random.o build/common/timing.o
APP_OBJECTS := $(PROGRAMS:%=build/apps/%.o)
OBJECTS := $(COMMON_OBJECTS) $(APP_OBJECTS) build/tests/options_test.o \
	build/tests/buffer_test.o build/tests/buffer_integration.o
BUFFER_TEST := build/tests/buffer_test
BUFFER_INTEGRATION := build/tests/buffer_integration
OPTIONS_TEST := build/tests/options_test
INSTANCE_TEST := build/tests/test_instance
PARTICIPANTS_TEST := build/tests/test_participants

.PHONY: all clean smoke test test-options test-instance test-participants \
	test-buffer test-workers test-random test-python help
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

$(INSTANCE_TEST): tests/test_instance.c build/common/instance.o
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o "$@"

test-instance: all $(INSTANCE_TEST)
	./$(INSTANCE_TEST)

$(PARTICIPANTS_TEST): tests/test_participants.c build/common/instance.o \
		build/common/participants.o
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o "$@"

test-participants: all $(PARTICIPANTS_TEST)
	./$(PARTICIPANTS_TEST)

$(BUFFER_TEST): build/tests/buffer_test.o build/common/buffer.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

$(BUFFER_INTEGRATION): build/tests/buffer_integration.o build/common/buffer.o \
		build/common/instance.o build/common/participants.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

test-buffer: all $(BUFFER_TEST) $(BUFFER_INTEGRATION)
	./$(BUFFER_TEST)
	bash tests/buffer_integration.sh "$(PYTHON)"

# Ejecuta las pruebas de argumentos y las pruebas Python disponibles
test: test-options test-instance test-participants test-buffer smoke test-python test-random
	@echo 'OK: pruebas disponibles ejecutadas.'
	@echo 'Pendiente integrar los trabajadores y el cierre completo de la instancia'

test-python: all build/tests/worker_control
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
	@echo 'all: compilar los cuatro ejecutables iniciales'
	@echo 'smoke: comprobar la interfaz inicial de la cáscara'
	@echo 'test: ejecutar argumentos, memoria compartida, smoke y pruebas Python'
	@echo 'test-options: comprobar el parser y los argumentos de los cuatro ejecutables'
	@echo 'test-instance: comprobar memoria compartida y creator'
	@echo 'test-participants: comprobar registro, IDs y retirada'
	@echo 'test-workers requiere la integracion pendiente con P2-08'
	@echo 'test-buffer comprueba FIFO e integracion con memoria y registro'
	@echo 'test-python: ejecutar los test_*.py disponibles sin necesitar los ejecutables C'
	@echo 'clean: eliminar objetos y ejecutables generados'

-include $(OBJECTS:.o=.d)

WORKER_TEST_OBJECTS := build/tests/random_test.o build/tests/worker_control.o
-include $(WORKER_TEST_OBJECTS:.o=.d)

build/tests/random_test: build/tests/random_test.o build/common/random.o build/common/message.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

build/tests/worker_control: build/tests/worker_control.o build/common/instance.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

test-random: build/tests/random_test
	./build/tests/random_test

test-workers:
	@echo 'Pendiente integrar P2-08 y activar los trabajadores con su escritor CSV' >&2
	@exit 1
