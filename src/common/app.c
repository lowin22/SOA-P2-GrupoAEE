#include "soa/app.h"

#include <stdio.h>
#include <stdlib.h>

#include "soa/instance.h"
#include "soa/worker.h"
#include "soa/types.h"

static const char *instance_error(soa_status_t status)
{
    switch (status) {
    case SOA_INVALID_ARGUMENT:
        return "nombre o capacidad invalidos";
    case SOA_EXISTS:
        return "ya existe una instancia con ese nombre";
    case SOA_SYSTEM_ERROR:
        return "fallo una operacion del sistema";
    default:
        return "error inesperado";
    }
}

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
              "Ver docs/OPCIONES.md para el contrato completo.");
        if (program == PROGRAM_CREATOR) {
            puts("\nEstado: creación y publicación de memoria compartida implementadas.");
        } else {
            puts("\nEstado: funcionalidad pendiente de implementación para este programa.");
        }
        return EXIT_SUCCESS;
    }

    if (result == OPTIONS_ERROR) {
        fprintf(stderr, "%s: %s Consulte ./%s --help.\n", name, error, name);
        return EXIT_FAILURE;
    }

    if (program == PROGRAM_CREATOR) {
        soa_status_t status = soa_instance_create(parsed.name, parsed.capacity);

        if (status != SOA_OK) {
            fprintf(stderr, "%s: %s (%s).\n", name, instance_error(status),
                    parsed.name);
            return EXIT_FAILURE;
        }
        printf("Instancia %s creada con capacidad %u.\n", parsed.name,
               parsed.capacity);
        return EXIT_SUCCESS;
    }

    /* Ejecutar el trabajador según su función. */
    if (program == PROGRAM_PRODUCER || program == PROGRAM_CONSUMER) {
        soa_role_t role = program == PROGRAM_PRODUCER
            ? SOA_ROLE_PRODUCER : SOA_ROLE_CONSUMER;
        return worker_run(&parsed, role);
    }

    /* El finalizador todavía necesita su integración. */
    fprintf(stderr, "%s: argumentos validos; funcionalidad pendiente de implementación.\n",
            name);
    return EXIT_FAILURE;
}
