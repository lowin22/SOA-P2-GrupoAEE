#ifndef SOA_APP_H
#define SOA_APP_H

#include "soa/options.h"

/* Valida argumentos antes de ejecutar la operacion */
int soa_app_main(int argc, char *argv[], program_t program, const char *name,
                 const char *description, const char *options);

#endif
