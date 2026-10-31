#include <stdio.h>

#include "soa/csv_writer.h"

int main(void)
{
    soa_csv_writer_t writer = {0};

    /* Caso 1: cerrar sin haber abierto. */
    if (soa_csv_writer_close(&writer) != -1) {
        fprintf(stderr, "ERROR: cierre sin apertura aceptado\n");
        return 1;
    }

    /* Caso 2: argumentos nulos. */
    if (soa_csv_writer_open(NULL, "archivo.csv") != -1) {
        fprintf(stderr, "ERROR: escritor NULL aceptado\n");
        return 1;
    }

    if (soa_csv_writer_open(&writer, NULL) != -1) {
        fprintf(stderr, "ERROR: ruta NULL aceptada\n");
        return 1;
    }

    /* Caso 3: intentar crear un archivo en un directorio inexistente. */
    if (soa_csv_writer_open(
            &writer,
            "/directorio_inexistente_p2_08/archivo.csv") != -1) {
        fprintf(stderr, "ERROR: ruta inexistente aceptada\n");
        soa_csv_writer_close(&writer);
        return 1;
    }

    if (writer.failed != 1 || writer.file != NULL) {
        fprintf(stderr, "ERROR: estado de fallo incorrecto\n");
        return 1;
    }

    puts("OK: apertura y cierre manejan correctamente los errores.");
    return 0;
}
