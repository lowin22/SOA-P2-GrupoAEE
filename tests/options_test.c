#include "soa/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: fallo: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static size_t cases;
static char fixture_name[80];

static void remove_fixture(void)
{
    if (fixture_name[0] != '\0') {
        shm_unlink(fixture_name);
    }
}

static int base_args(program_t program, char *args[])
{
    int n = 0;
    args[n++] = "test";
    args[n++] = "--name";
    args[n++] = "/grupo_aee";
    if (program == PROGRAM_CREATOR) {
        args[n++] = "--capacity";
        args[n++] = "8";
    } else if (program == PROGRAM_FINISHER) {
        args[n++] = "--summary";
        args[n++] = "results/summary.csv";
    } else {
        args[n++] = "--id";
        args[n++] = "1";
        args[n++] = "--mean-ms";
        args[n++] = "2.5";
        args[n++] = "--seed";
        args[n++] = "0";
        args[n++] = "--log";
        args[n++] = "results/worker.csv";
    }
    args[n] = NULL;
    return n;
}

static options_t check_parse(program_t program, int argc, char *argv[],
                             options_result_t expected, const char *diagnostic)
{
    options_t out = {.name = "unchanged", .capacity = 77, .messages = 91};
    unsigned char before[sizeof out];
    memcpy(before, &out, sizeof out);
    char *saved[32];
    char contents[32][512];
    CHECK(argc <= 32);
    for (int i = 0; i < argc; ++i) {
        saved[i] = argv[i];
        CHECK(strlen(argv[i]) < sizeof contents[i]);
        strcpy(contents[i], argv[i]);
    }
    char error[256];
    memset(error, 'X', sizeof error);
    options_result_t result = options_parse(argc, argv, program, &out,
                                            error, sizeof error);
    if (result != expected) {
        fprintf(stderr, "program=%d argc=%d resultado=%d esperado=%d: %s\n",
                program, argc, result, expected, error);
    }
    CHECK(result == expected);
    CHECK(memchr(error, '\0', sizeof error) != NULL);
    if (expected != OPTIONS_OK) {
        CHECK(memcmp(before, &out, sizeof out) == 0);
    }
    if (expected == OPTIONS_ERROR) {
        CHECK(error[0] != '\0');
        if (diagnostic != NULL) {
            CHECK(strstr(error, diagnostic) != NULL);
        }
    } else {
        CHECK(error[0] == '\0');
    }
    for (int i = 0; i < argc; ++i) {
        CHECK(argv[i] == saved[i]);
        CHECK(strcmp(argv[i], contents[i]) == 0);
    }
    ++cases;
    return out;
}

static int value_index(int argc, char *argv[], const char *option)
{
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], option) == 0) {
            return i + 1;
        }
    }
    CHECK(false);
    return 0;
}

static void check_values(program_t program, const char *option,
                         const char *const values[], size_t count,
                         options_result_t expected)
{
    for (size_t i = 0; i < count; ++i) {
        char *args[32];
        int n = base_args(program, args);
        int index;
        if (strcmp(option, "--messages") == 0) {
            args[n++] = "--messages";
            index = n++;
        } else {
            index = value_index(n, args, option);
        }
        args[index] = (char *)values[i];
        args[n] = NULL;
        check_parse(program, n, args, expected,
                    expected == OPTIONS_ERROR ? option : NULL);
    }
}

#define VALUES(program, option, array, expected) \
    check_values(program, option, array, sizeof array / sizeof array[0], expected)

/* Comprueba que los argumentos invalidos dejan intacto un objeto compartido */
static void check_instance_untouched(void)
{
    char candidate[80];
    snprintf(candidate, sizeof candidate, "/p2_arguments_test_%ld", (long)getpid());
    int fd = shm_open(candidate, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    strcpy(fixture_name, candidate);
    CHECK(atexit(remove_fixture) == 0);
    const char marker[] = "P2-04 existing shared object: preserve these bytes";
    CHECK(ftruncate(fd, (off_t)sizeof marker) == 0);
    CHECK(pwrite(fd, marker, sizeof marker, 0) == (ssize_t)sizeof marker);

    const char *programs[] = {"./creator", "./producer", "./consumer", "./finisher"};
    for (program_t p = PROGRAM_CREATOR; p <= PROGRAM_FINISHER; ++p) {
        char *args[32];
        int n = base_args(p, args);
        args[0] = (char *)programs[p];
        args[2] = fixture_name;
        if (p == PROGRAM_CREATOR) {
            args[value_index(n, args, "--capacity")] = "0";
        } else if (p == PROGRAM_FINISHER) {
            args[value_index(n, args, "--summary")] = "";
        } else {
            args[value_index(n, args, "--mean-ms")] = "NaN";
        }

        pid_t child = fork();
        CHECK(child >= 0);
        if (child == 0) {
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd < 0 || dup2(null_fd, STDERR_FILENO) < 0) {
                _exit(127);
            }
            close(null_fd);
            close(fd);
            execv(programs[p], args);
            _exit(127);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE);

        int probe = shm_open(fixture_name, O_RDONLY, 0);
        CHECK(probe >= 0);
        struct stat info;
        CHECK(fstat(probe, &info) == 0 && info.st_size == (off_t)sizeof marker);
        char observed[sizeof marker];
        CHECK(pread(probe, observed, sizeof observed, 0) == (ssize_t)sizeof observed);
        CHECK(memcmp(observed, marker, sizeof marker) == 0);
        CHECK(close(probe) == 0);
    }
    CHECK(close(fd) == 0);
    CHECK(shm_unlink(fixture_name) == 0);
    fixture_name[0] = '\0';
    puts("OK: objeto POSIX existente intacto tras argumentos invalidos de los cuatro CLI.");
}

int main(void)
{
    const char *bad_integers[] = {
        "", "0", "1025", "-1", "+1", "1x", "1.0", " 1", "1 ", "0x1",
        "18446744073709551616", "999999999999999999999999999999999999"
    };
    const char *good_integers[] = {"1", "1024", "0008"};
    const char *bad_means[] = {
        "", "-1", "1000.1", "NaN", "nan(1)", "inf", "infinity", "-inf",
        "2ms", "2..5", ".", "1e", " 2", "2 ", "0x1p2", "1e309", "1e-9999"
    };
    const char *good_means[] = {"0", "1000", "2.5", ".5", "1e2", "+1", "-0"};
    const char *bad_seeds[] = {"", "-1", "+1", "1x", "1.5", "0x1",
                               "18446744073709551616"};
    const char *good_seeds[] = {"0", "1", "18446744073709551615"};
    const char *bad_messages[] = {"", "0", "-1", "+1", "1x", "1.5",
                                  "18446744073709551616"};
    const char *good_messages[] = {"1", "200", "18446744073709551615"};
    const char *bad_names[] = {"", "/", "grupo", "/a/b", "//a", "/a b",
                               "/a.b", "/á", "/a\n"};

    for (program_t p = PROGRAM_CREATOR; p <= PROGRAM_FINISHER; ++p) {
        char *args[32];
        int n = base_args(p, args);
        options_t out = check_parse(p, n, args, OPTIONS_OK, NULL);
        CHECK(out.name == args[2]);
        CHECK(!out.has_message_limit && out.messages == 0);
        if (p == PROGRAM_CREATOR) {
            CHECK(out.capacity == 8);
        } else if (p == PROGRAM_FINISHER) {
            CHECK(strcmp(out.summary_path, "results/summary.csv") == 0);
        } else {
            CHECK(out.id == 1 && out.seed == 0 && out.mean_ms == 2.5);
            CHECK(strcmp(out.log_path, "results/worker.csv") == 0);
        }
        char *help[] = {"test", "--help", NULL};
        check_parse(p, 2, help, OPTIONS_HELP, NULL);
        check_parse(p, 1, help, OPTIONS_ERROR, "Faltan");
        char *mixed_help[] = {"test", "--help", "--name", "/a", NULL};
        check_parse(p, 4, mixed_help, OPTIONS_ERROR, "--help");

        /* Prueba cada opcion obligatoria ausente y repetida */
        for (int i = 1; i < n; i += 2) {
            char *missing[32];
            int k = 0;
            for (int j = 0; j < n; ++j) {
                if (j != i && j != i + 1) {
                    missing[k++] = args[j];
                }
            }
            missing[k] = NULL;
            check_parse(p, k, missing, OPTIONS_ERROR, args[i]);
            args[n] = args[i];
            args[n + 1] = args[i + 1];
            args[n + 2] = NULL;
            check_parse(p, n + 2, args, OPTIONS_ERROR, "repetida");
        }
        const char *extra[] = {"--unknown", "--nam", "--name=/a", "posicional",
                               "--help", "--"};
        for (size_t i = 0; i < sizeof extra / sizeof extra[0]; ++i) {
            args[n] = (char *)extra[i];
            args[n + 1] = NULL;
            check_parse(p, n + 1, args, OPTIONS_ERROR, NULL);
        }
        args[n] = "--name";
        args[n + 1] = "/duplicate";
        args[n + 2] = NULL;
        check_parse(p, n + 2, args, OPTIONS_ERROR, "repetida");

        /* Prueba valores ausentes y otra opcion en lugar del valor */
        char *no_value[] = {"test", "--name", NULL};
        check_parse(p, 2, no_value, OPTIONS_ERROR, "valor");
        char *next_option[] = {"test", "--name", "--help", NULL};
        check_parse(p, 3, next_option, OPTIONS_ERROR, "valor");
        VALUES(p, "--name", bad_names, OPTIONS_ERROR);
        char name[INSTANCE_NAME_MAX_LENGTH + 2];
        memset(name, 'a', sizeof name);
        name[0] = '/';
        name[INSTANCE_NAME_MAX_LENGTH] = '\0';
        args[2] = name;
        check_parse(p, n, args, OPTIONS_OK, NULL);
        name[INSTANCE_NAME_MAX_LENGTH] = 'a';
        name[INSTANCE_NAME_MAX_LENGTH + 1] = '\0';
        check_parse(p, n, args, OPTIONS_ERROR, "--name");

        /* Rechaza opciones de otros ejecutables */
        n = base_args(p, args);
        args[n] = p == PROGRAM_CREATOR ? "--id" : "--capacity";
        args[n + 1] = "1";
        args[n + 2] = NULL;
        check_parse(p, n + 2, args, OPTIONS_ERROR, "no permitida");
    }

    VALUES(PROGRAM_CREATOR, "--capacity", bad_integers, OPTIONS_ERROR);
    VALUES(PROGRAM_CREATOR, "--capacity", good_integers, OPTIONS_OK);
    for (program_t p = PROGRAM_PRODUCER; p <= PROGRAM_CONSUMER; ++p) {
        VALUES(p, "--id", bad_integers, OPTIONS_ERROR);
        VALUES(p, "--id", good_integers, OPTIONS_OK);
        VALUES(p, "--mean-ms", bad_means, OPTIONS_ERROR);
        VALUES(p, "--mean-ms", good_means, OPTIONS_OK);
        VALUES(p, "--seed", bad_seeds, OPTIONS_ERROR);
        VALUES(p, "--seed", good_seeds, OPTIONS_OK);
        const char *empty_path[] = {""};
        const char *paths[] = {"results/worker.csv", "ruta con espacios.csv", "./--log"};
        VALUES(p, "--log", empty_path, OPTIONS_ERROR);
        VALUES(p, "--log", paths, OPTIONS_OK);
    }
    VALUES(PROGRAM_PRODUCER, "--messages", bad_messages, OPTIONS_ERROR);
    VALUES(PROGRAM_PRODUCER, "--messages", good_messages, OPTIONS_OK);
    const char *empty_path[] = {""};
    VALUES(PROGRAM_FINISHER, "--summary", empty_path, OPTIONS_ERROR);

    char *args[32];
    int n = base_args(PROGRAM_PRODUCER, args);
    args[n++] = "--messages";
    args[n++] = "18446744073709551615";
    args[n] = NULL;
    options_t out = check_parse(PROGRAM_PRODUCER, n, args, OPTIONS_OK, NULL);
    CHECK(out.has_message_limit && out.messages == UINT64_MAX);
    args[n++] = "--messages";
    args[n++] = "1";
    check_parse(PROGRAM_PRODUCER, n, args, OPTIONS_ERROR, "repetida");
    n = base_args(PROGRAM_CONSUMER, args);
    args[n++] = "--messages";
    args[n++] = "1";
    check_parse(PROGRAM_CONSUMER, n, args, OPTIONS_ERROR, "no permitida");

    char *reverse[] = {"creator", "--capacity", "1024", "--name", "/reordered", NULL};
    out = check_parse(PROGRAM_CREATOR, 5, reverse, OPTIONS_OK, NULL);
    CHECK(out.capacity == 1024 && out.name == reverse[4]);

    /* Comprueba parametros invalidos y buffers pequenos */
    char error[256];
    char tiny[2] = {'X', 'Y'};
    CHECK(options_parse(1, reverse, PROGRAM_CREATOR, &out, tiny, 1) == OPTIONS_ERROR);
    CHECK(tiny[0] == '\0' && tiny[1] == 'Y');
    CHECK(options_parse(5, reverse, PROGRAM_CREATOR, NULL, error, sizeof error)
          == OPTIONS_ERROR);
    CHECK(options_parse(5, reverse, PROGRAM_CREATOR, &out, NULL, 0) == OPTIONS_ERROR);
    CHECK(options_parse(5, reverse, PROGRAM_CREATOR, &out, tiny, 0) == OPTIONS_ERROR);
    CHECK(options_parse(0, NULL, PROGRAM_CREATOR, &out, error, sizeof error)
          == OPTIONS_ERROR);
    CHECK(options_parse(5, reverse, (program_t)99, &out, error, sizeof error)
          == OPTIONS_ERROR);
    char *null_arg[] = {"creator", NULL};
    CHECK(options_parse(2, null_arg, PROGRAM_CREATOR, &out, error, sizeof error)
          == OPTIONS_ERROR);
    check_instance_untouched();
    printf("OK: %zu casos del parser publico y comprobaciones de contrato.\n", cases);
    return EXIT_SUCCESS;
}
