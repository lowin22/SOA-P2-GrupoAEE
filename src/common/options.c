#include "soa/options.h"
#include "soa/types.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OPTION_NAME,
    OPTION_CAPACITY,
    OPTION_ID,
    OPTION_MEAN_MS,
    OPTION_SEED,
    OPTION_MESSAGES,
    OPTION_LOG,
    OPTION_SUMMARY,
    OPTION_COUNT
} option_key_t;

static const char *const option_names[OPTION_COUNT] = {
    [OPTION_NAME] = "--name",
    [OPTION_CAPACITY] = "--capacity",
    [OPTION_ID] = "--id",
    [OPTION_MEAN_MS] = "--mean-ms",
    [OPTION_SEED] = "--seed",
    [OPTION_MESSAGES] = "--messages",
    [OPTION_LOG] = "--log",
    [OPTION_SUMMARY] = "--summary"
};

static option_key_t find_option(const char *text)
{
    if (text == NULL) {
        return OPTION_COUNT;
    }

    for (size_t i = 0; i < OPTION_COUNT; ++i) {
        if (strcmp(text, option_names[i]) == 0) {
            return (option_key_t)i;
        }
    }
    return OPTION_COUNT;
}

static bool parse_uint64(const char *text, uint64_t min, uint64_t max, uint64_t *out)
{
    if (text == NULL || out == NULL || min > max || text[0] == '\0') {
        return false;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }

    errno = 0;
    char *end = NULL;
    uintmax_t value = strtoumax(text, &end, 10);

    if (errno == ERANGE || end == text || *end != '\0') {
        return false;
    }

    if (value < min || value > max) {
        return false;
    }

    *out = (uint64_t)value;
    return true;
}

static bool parse_mean_ms(const char *text, double *out)
{
    if (text == NULL || out == NULL || text[0] == '\0') {
        return false;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        bool digit = *p >= '0' && *p <= '9';

        if (!digit && *p != '.' && *p != 'e'
            && *p != 'E' && *p != '+' && *p != '-') {
            return false;
        }
    }

    errno = 0;
    char *end = NULL;
    double value = strtod(text, &end);

    if (errno == ERANGE || end == text || *end != '\0') {
        return false;
    }

    if (!isfinite(value) || value < 0.0 || value > 1000.0) {
        return false;
    }

    *out = value;
    return true;
}

static bool valid_instance_name(const char *text)
{
    if (text == NULL || text[0] != '/' || text[1] == '\0') {
        return false;
    }

    if (strlen(text) > INSTANCE_NAME_MAX_LENGTH) {
        return false;
    }

    for (const char *p = text + 1; *p != '\0'; ++p) {
        bool letter = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z');
        bool digit = *p >= '0' && *p <= '9';

        if (!letter && !digit && *p != '_' && *p != '-') {
            return false;
        }
    }

    return true;
}

static bool option_allowed(program_t program, option_key_t key)
{
    if (key == OPTION_NAME) {
        return true;
    }
    switch (program) {
    case PROGRAM_CREATOR:
        return key == OPTION_CAPACITY;
    case PROGRAM_FINISHER:
        return key == OPTION_SUMMARY;
    case PROGRAM_PRODUCER:
    case PROGRAM_CONSUMER:
        return key == OPTION_ID || key == OPTION_MEAN_MS || key == OPTION_SEED
            || key == OPTION_LOG
            || (program == PROGRAM_PRODUCER && key == OPTION_MESSAGES);
    }
    return false;
}

/* Guarda referencias al texto sin abrir archivos ni instancias */
static options_result_t collect_option_values(
    int argc, char *argv[], program_t program,
    const char *values[OPTION_COUNT], char *error, size_t error_size)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        return OPTIONS_HELP;
    }
    if (argc <= 1) {
        snprintf(error, error_size, "Faltan argumentos.");
        return OPTIONS_ERROR;
    }
    for (int i = 1; i < argc; i += 2) {
        option_key_t key = find_option(argv[i]);
        if (key == OPTION_COUNT) {
            snprintf(error, error_size,
                     "Opcion desconocida o fuera de lugar: %s. "
                     "--help debe usarse solo.", argv[i]);
            return OPTIONS_ERROR;
        }
        if (!option_allowed(program, key)) {
            snprintf(error, error_size, "Opcion no permitida: %s.", argv[i]);
            return OPTIONS_ERROR;
        }
        if (values[key] != NULL) {
            snprintf(error, error_size, "Opcion repetida: %s.", argv[i]);
            return OPTIONS_ERROR;
        }
        if (i + 1 >= argc || strncmp(argv[i + 1], "--", 2) == 0) {
            snprintf(error, error_size, "Falta el valor de %s.", argv[i]);
            return OPTIONS_ERROR;
        }
        if (argv[i + 1][0] == '\0') {
            snprintf(error, error_size, "El valor de %s esta vacio.", argv[i]);
            return OPTIONS_ERROR;
        }
        values[key] = argv[i + 1];
    }
    return OPTIONS_OK;
}

static options_result_t invalid_value(option_key_t key, const char *expected,
                                     char *error, size_t error_size)
{
    snprintf(error, error_size, "Valor invalido para %s: %s.",
             option_names[key], expected);
    return OPTIONS_ERROR;
}

options_result_t options_parse(int argc, char *argv[], program_t program,
                              options_t *out, char *error, size_t error_size)
{
    if (error == NULL || error_size == 0) {
        return OPTIONS_ERROR;
    }
    error[0] = '\0';
    if (out == NULL || argv == NULL || argc < 1
        || program < PROGRAM_CREATOR || program > PROGRAM_FINISHER) {
        snprintf(error, error_size, "Parametros invalidos del parser.");
        return OPTIONS_ERROR;
    }
    for (int i = 0; i < argc; ++i) {
        if (argv[i] == NULL) {
            snprintf(error, error_size, "Argumento nulo en la posicion %d.", i);
            return OPTIONS_ERROR;
        }
    }

    const char *values[OPTION_COUNT] = {0};
    options_result_t result = collect_option_values(
        argc, argv, program, values, error, error_size);
    if (result != OPTIONS_OK) {
        return result;
    }
    for (option_key_t key = OPTION_NAME; key < OPTION_COUNT; ++key) {
        if (option_allowed(program, key) && key != OPTION_MESSAGES
            && values[key] == NULL) {
            snprintf(error, error_size, "Falta la opcion obligatoria %s.",
                     option_names[key]);
            return OPTIONS_ERROR;
        }
    }

    /* La salida solo cambia cuando todos los valores son validos */
    options_t parsed = {0};
    if (!valid_instance_name(values[OPTION_NAME])) {
        return invalid_value(OPTION_NAME,
            "use / seguido de letras ASCII, digitos, _ o -; maximo 255 bytes",
            error, error_size);
    }
    parsed.name = values[OPTION_NAME];

    uint64_t integer;
    if (values[OPTION_CAPACITY] != NULL) {
        if (!parse_uint64(values[OPTION_CAPACITY], SOA_MIN_CAPACITY,
                          SOA_MAX_CAPACITY, &integer)) {
            return invalid_value(OPTION_CAPACITY, "entero decimal entre 1 y 1024",
                                 error, error_size);
        }
        parsed.capacity = (uint32_t)integer;
    }
    if (values[OPTION_ID] != NULL) {
        if (!parse_uint64(values[OPTION_ID], SOA_MIN_PARTICIPANT_ID,
                          SOA_MAX_PARTICIPANT_ID, &integer)) {
            return invalid_value(OPTION_ID, "entero decimal entre 1 y 1024",
                                 error, error_size);
        }
        parsed.id = (uint32_t)integer;
    }
    if (values[OPTION_MEAN_MS] != NULL
        && !parse_mean_ms(values[OPTION_MEAN_MS], &parsed.mean_ms)) {
        return invalid_value(OPTION_MEAN_MS, "numero decimal finito entre 0 y 1000",
                             error, error_size);
    }
    if (values[OPTION_SEED] != NULL
        && !parse_uint64(values[OPTION_SEED], 0, UINT64_MAX, &parsed.seed)) {
        return invalid_value(OPTION_SEED,
            "entero decimal entre 0 y 18446744073709551615", error, error_size);
    }
    if (values[OPTION_MESSAGES] != NULL) {
        if (!parse_uint64(values[OPTION_MESSAGES], 1, UINT64_MAX, &parsed.messages)) {
            return invalid_value(OPTION_MESSAGES,
                "entero decimal entre 1 y 18446744073709551615", error, error_size);
        }
        parsed.has_message_limit = true;
    }
    parsed.log_path = values[OPTION_LOG];
    parsed.summary_path = values[OPTION_SUMMARY];
    *out = parsed;
    return OPTIONS_OK;
}
