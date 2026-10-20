#include "soa/app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int soa_app_main(int argc, char *argv[], const char *name,
                 const char *description, const char *options)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("%s: %s\n\nUso previsto: ./%s %s\n\n"
               "Estado: cáscara inicial; funcionalidad pendiente de implementación.\n"
               "Solo --help está implementado. No se crean recursos IPC.\n",
               name, description, name, options);
        return EXIT_SUCCESS;
    }

    if (argc == 1) {
        fprintf(stderr, "%s: faltan argumentos. Consulte ./%s --help.\n",
                name, name);
    } else {
        fprintf(stderr, "%s: funcionalidad pendiente de implementación. "
                "Consulte ./%s --help.\n", name, name);
    }
    return EXIT_FAILURE;
}
