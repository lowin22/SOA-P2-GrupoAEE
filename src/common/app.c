#include "soa/app.h"

#include <stdio.h>
#include <stdlib.h>

int soa_app_main(int argc, char *argv[], program_t program, const char *name,
                 const char *description, const char *options)
{
    options_t parsed = {0};
    char error[256];
    options_result_t result = options_parse(
        argc, argv, program, &parsed, error, sizeof error);

    if (result == OPTIONS_HELP) {
        printf("%s: %s\n\nUso previsto: ./%s %s\n\n"
               "Opciones completas --opcion valor, sin repeticiones.\n"
               "--help se usa solo y retorna exito.\n"
               "--name: / seguido de letras ASCII, digitos, _ o -; "
               "maximo 255 bytes en total.\n",
               name, description, name, options);
        if (program == PROGRAM_CREATOR) {
            puts("--capacity: entero decimal de 1 a 1024.");
        }
        if (program == PROGRAM_PRODUCER || program == PROGRAM_CONSUMER) {
            puts("--id: entero decimal de 1 a 1024; unicidad comprobada al registrar.\n"
                 "--mean-ms: decimal finito de 0 a 1000; cero desactiva pausas.\n"
                 "--seed: entero decimal de 0 a 18446744073709551615.\n"
                 "--log: ruta no vacia del CSV propio; no se abre al validar.");
        }
        if (program == PROGRAM_PRODUCER) {
            puts("--messages: cuota de 1 a 18446744073709551615; "
                 "omitir significa producir hasta cierre.");
        }
        if (program == PROGRAM_FINISHER) {
            puts("--summary: ruta no vacia del resumen; no se abre al validar.");
        }
        puts("Todas las opciones mostradas son obligatorias salvo --messages.\n"
             "Ver docs/OPCIONES.md para el contrato completo.\n\n"
             "Estado: cáscara inicial; funcionalidad pendiente de implementación.\n"
             "La validacion de argumentos esta implementada. No se crean recursos IPC.");
        return EXIT_SUCCESS;
    }

    if (result == OPTIONS_ERROR) {
        fprintf(stderr, "%s: %s Consulte ./%s --help.\n", name, error, name);
        return EXIT_FAILURE;
    }

    /* La operacion se integrara despues de validar los argumentos */
    fprintf(stderr, "%s: argumentos validos; funcionalidad pendiente de implementación.\n",
            name);
    return EXIT_FAILURE;
}
