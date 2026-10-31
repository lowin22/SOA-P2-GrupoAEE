#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "soa/instance.h"
#include "soa/types.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                                \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while (0)

static void make_name(char *buffer, size_t size, const char *suffix)
{
    int written = snprintf(buffer, size, "/soa_p2_%ld_%s", (long)getpid(), suffix);
    CHECK(written > 0 && (size_t)written < size);
    (void)shm_unlink(buffer);
}

static int run_creator(const char *name, const char *capacity)
{
    pid_t child = fork();
    int status;

    CHECK(child != -1);
    if (child == 0) {
        execl("./creator", "creator", "--name", name, "--capacity", capacity,
              (char *)NULL);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

static void test_create_open_duplicate(void)
{
    char name[128];
    soa_instance_t *instance = NULL;
    struct stat before;
    struct stat after;
    int fd;

    make_name(name, sizeof(name), "basic");
    CHECK(soa_instance_create(name, 8) == SOA_OK);
    fd = shm_open(name, O_RDWR, 0);
    CHECK(fd != -1);
    CHECK(fstat(fd, &before) == 0);
    CHECK((before.st_mode & 0777) == 0600);
    CHECK(close(fd) == 0);
    CHECK(soa_instance_create(name, 9) == SOA_EXISTS);
    fd = shm_open(name, O_RDWR, 0);
    CHECK(fd != -1);
    CHECK(fstat(fd, &after) == 0);
    CHECK(after.st_size == before.st_size);
    CHECK(close(fd) == 0);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(instance != NULL);
    CHECK(soa_instance_close(&instance) == SOA_OK);
    CHECK(instance == NULL);
    CHECK(shm_unlink(name) == 0);
}

static void test_validation_and_invalid_objects(void)
{
    char zero_name[128];
    char small_name[128];
    char missing_name[128];
    soa_instance_t *instance = (soa_instance_t *)(uintptr_t)1;
    int fd;

    CHECK(soa_instance_create(NULL, 1) == SOA_INVALID_ARGUMENT);
    CHECK(soa_instance_create("without_slash", 1) == SOA_INVALID_ARGUMENT);
    CHECK(soa_instance_create("/two/slashes", 1) == SOA_INVALID_ARGUMENT);
    CHECK(soa_instance_create("/capacity", 0) == SOA_INVALID_ARGUMENT);
    CHECK(soa_instance_create("/capacity", 1025) == SOA_INVALID_ARGUMENT);
    make_name(missing_name, sizeof(missing_name), "missing");
    CHECK(soa_instance_open(missing_name, &instance) == SOA_NOT_FOUND);
    CHECK(instance == NULL);

    make_name(zero_name, sizeof(zero_name), "zero");
    fd = shm_open(zero_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd != -1);
    CHECK(close(fd) == 0);
    CHECK(soa_instance_open(zero_name, &instance) == SOA_NOT_READY);
    CHECK(instance == NULL);
    CHECK(shm_unlink(zero_name) == 0);

    make_name(small_name, sizeof(small_name), "small");
    fd = shm_open(small_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd != -1);
    CHECK(ftruncate(fd, 8) == 0);
    CHECK(close(fd) == 0);
    CHECK(soa_instance_open(small_name, &instance) == SOA_INCOMPATIBLE_INSTANCE);
    CHECK(instance == NULL);
    CHECK(shm_unlink(small_name) == 0);
}

static void test_isolation(void)
{
    char first[128];
    char second[128];
    soa_instance_t *one = NULL;
    soa_instance_t *two = NULL;

    make_name(first, sizeof(first), "one");
    make_name(second, sizeof(second), "two");
    CHECK(soa_instance_create(first, 1) == SOA_OK);
    CHECK(soa_instance_create(second, 1024) == SOA_OK);
    CHECK(soa_instance_open(first, &one) == SOA_OK);
    CHECK(soa_instance_open(second, &two) == SOA_OK);
    CHECK(soa_instance_close(&one) == SOA_OK);
    CHECK(soa_instance_close(&two) == SOA_OK);
    CHECK(shm_unlink(first) == 0);
    CHECK(shm_unlink(second) == 0);
}

static void test_concurrent_creation(void)
{
    enum { CHILDREN = 8 };
    char name[128];
    int successes = 0;
    int duplicates = 0;
    int index;

    make_name(name, sizeof(name), "race");
    for (index = 0; index < CHILDREN; index++) {
        pid_t child = fork();
        CHECK(child != -1);
        if (child == 0) {
            soa_status_t result = soa_instance_create(name, 16);
            _exit(result == SOA_OK ? 0 : (result == SOA_EXISTS ? 1 : 2));
        }
    }
    for (index = 0; index < CHILDREN; index++) {
        int status;
        CHECK(wait(&status) > 0);
        CHECK(WIFEXITED(status));
        if (WEXITSTATUS(status) == 0) {
            successes++;
        } else if (WEXITSTATUS(status) == 1) {
            duplicates++;
        }
    }
    CHECK(successes == 1);
    CHECK(duplicates == CHILDREN - 1);
    CHECK(shm_unlink(name) == 0);
}

static void test_creator_cli(void)
{
    char name[128];

    make_name(name, sizeof(name), "cli");
    CHECK(run_creator(name, "4") == 0);
    CHECK(run_creator(name, "5") != 0);
    CHECK(shm_unlink(name) == 0);
    CHECK(run_creator("invalid", "4") != 0);
}

int main(void)
{
    test_create_open_duplicate();
    test_validation_and_invalid_objects();
    test_isolation();
    test_concurrent_creation();
    test_creator_cli();
    puts("OK: memoria compartida, publicación y creator (P2-05).");
    return EXIT_SUCCESS;
}
