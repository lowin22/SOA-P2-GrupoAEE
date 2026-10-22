
# Compilador de C que utilizaremos.
CC := gcc

# Opciones de compilación exigidas por el profesor.
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror -pthread

# Habilitar las funciones de POSIX.
CPPFLAGS := -D_POSIX_C_SOURCE=200809L

# Opciones para enlazar programas que utilizan pthread.
LDLIBS := -pthread

# Nombres de los cuatro programas del proyecto.
PROGRAMS := creator producer consumer finisher

# Identificar cuáles programas ya tienen un archivo fuente.
AVAILABLE := $(foreach program,$(PROGRAMS),$(if $(wildcard src/$(program).c),$(program)))

# La compilación y la limpieza no representan archivos físicos.
.PHONY: all test clean

# Compilar únicamente los programas cuyos archivos existen.
all: $(AVAILABLE)
	@echo "Compilacion inicial finalizada."
	@echo "Programas encontrados: $(AVAILABLE)"
	@echo "La integracion completa sigue pendiente."

# Compilar un programa cuando exista su archivo fuente.
$(PROGRAMS): %: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDLIBS)

# No declarar exitosas pruebas que todavia no existen.
test:
	@echo "ERROR: la suite automatizada aun no esta implementada."
	@echo "Se completara en el Issue P2-12."
	@exit 1

# Eliminar los ejecutables y objetos generados.
clean:
	rm -f $(PROGRAMS) src/*.o
	@echo "Archivos generados eliminados."
