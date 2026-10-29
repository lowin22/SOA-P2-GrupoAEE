#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INSTANCE_NAME_MAX_LENGTH 255

typedef enum {
    PROGRAM_CREATOR,
    PROGRAM_PRODUCER,
    PROGRAM_CONSUMER,
    PROGRAM_FINISHER
} program_t;

typedef struct {
    const char *name;
    uint32_t capacity;
    uint32_t id;
    double mean_ms;
    uint64_t seed;
    bool has_message_limit;
    uint64_t messages;
    const char *log_path;
    const char *summary_path;
} options_t;

typedef enum {
    OPTIONS_OK,
    OPTIONS_HELP,
    OPTIONS_ERROR
} options_result_t;

/*
 * argv contiene argc argumentos accesibles y no se modifica
 * Solo actualiza out cuando todos los argumentos son validos
 * Las cadenas de salida pertenecen a argv y no se liberan
 * El diagnostico se trunca si el buffer es pequeno
 * No imprime ni abre archivos o recursos compartidos
 */
options_result_t options_parse(
    int argc,
    char *argv[],
    program_t program,
    options_t *out,
    char *error,
    size_t error_size
);

#endif
