CC = gcc
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -O2 -g
override CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Werror -pthread
override LDLIBS += -pthread

PROGRAMS := creator producer consumer finisher
COMMON_OBJECTS := build/common/app.o
APP_OBJECTS := $(PROGRAMS:%=build/apps/%.o)
OBJECTS := $(COMMON_OBJECTS) $(APP_OBJECTS)

.PHONY: all clean smoke test help
all: $(PROGRAMS)

$(PROGRAMS): %: build/apps/%.o $(COMMON_OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o "$@"

build/%.o: src/%.c
	mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

smoke: all
	bash tests/smoke.sh

test:
	@echo 'Suite funcional pendiente: esta versión solo implementa la cáscara inicial.' >&2
	@echo 'Ejecute make smoke para comprobar la infraestructura.' >&2
	@exit 1

clean:
	rm -rf build
	rm -f $(PROGRAMS)

help:
	@echo 'all: compilar los cuatro ejecutables iniciales'
	@echo 'smoke: comprobar ayuda y rechazo de operaciones no implementadas'
	@echo 'test: reservado para la suite funcional; actualmente falla explícitamente'
	@echo 'clean: eliminar objetos y ejecutables generados'

-include $(OBJECTS:.o=.d)
