#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "soa/instance.h"
#include "soa/participants.h"
#include "soa/types.h"
#include "../src/common/instance_internal.h"

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
    int written = snprintf(buffer, size, "/soa_p2_p07_%ld_%s",
                           (long)getpid(), suffix);
    CHECK(written > 0 && (size_t)written < size);
    (void)shm_unlink(buffer);
}

static void test_registration_lifecycle(void)
{
    char name[128];
    soa_instance_t *instance = NULL;
    soa_event_t join;
    soa_event_t leave;
    uint64_t used_mask = UINT64_C(1) << 7U;

    make_name(name, sizeof(name), "lifecycle");
    CHECK(soa_instance_create(name, 4) == SOA_OK);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 7, &join) ==
          SOA_OK);
    CHECK(join.kind == SOA_EVENT_JOIN);
    CHECK(join.role == SOA_ROLE_PRODUCER && join.participant_id == 7);
    CHECK(join.state == SOA_STATE_OPEN && join.occupancy == 0);
    CHECK(join.timestamp_ns > 0);
    CHECK(instance->shared->active_producers == 1);
    CHECK((instance->shared->used_producer_ids[0] & used_mask) != 0);
    CHECK(instance->shared->producer_metrics[7].active == 1);
    CHECK(instance->shared->first_worker_start_ns == join.timestamp_ns);
    CHECK(soa_instance_close(&instance) == SOA_INVALID_ARGUMENT);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 8, &join) ==
          SOA_INVALID_ARGUMENT);

    CHECK(soa_participant_unregister(instance, &leave) == SOA_OK);
    CHECK(leave.kind == SOA_EVENT_LEAVE);
    CHECK(leave.role == SOA_ROLE_PRODUCER && leave.participant_id == 7);
    CHECK(leave.timestamp_ns >= join.timestamp_ns);
    CHECK(instance->shared->active_producers == 0);
    CHECK((instance->shared->used_producer_ids[0] & used_mask) != 0);
    CHECK(instance->shared->producer_metrics[7].active == 0);
    CHECK(soa_participant_unregister(instance, &leave) == SOA_INVALID_ARGUMENT);
    CHECK(soa_instance_close(&instance) == SOA_OK);

    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 7, &join) ==
          SOA_DUPLICATE_ID);
    CHECK(soa_participant_register(instance, SOA_ROLE_CONSUMER, 7, &join) ==
          SOA_OK);
    CHECK(instance->shared->active_consumers == 1);
    CHECK(soa_participant_unregister(instance, &leave) == SOA_OK);
    CHECK(soa_participant_register(instance, SOA_ROLE_CONSUMER, 1024, &join) ==
          SOA_OK);
    CHECK(soa_participant_unregister(instance, &leave) == SOA_OK);
    CHECK(soa_instance_close(&instance) == SOA_OK);
    CHECK(shm_unlink(name) == 0);
}

static void test_invalid_arguments(void)
{
    char name[128];
    soa_instance_t *instance = NULL;
    soa_event_t event;

    make_name(name, sizeof(name), "invalid");
    CHECK(soa_instance_create(name, 2) == SOA_OK);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_participant_register(NULL, SOA_ROLE_PRODUCER, 1, &event) ==
          SOA_INVALID_ARGUMENT);
    CHECK(soa_participant_register(instance, (soa_role_t)99, 1, &event) ==
          SOA_INVALID_ARGUMENT);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 0, &event) ==
          SOA_INVALID_ID);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 1025, &event) ==
          SOA_INVALID_ID);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 1, NULL) ==
          SOA_INVALID_ARGUMENT);
    CHECK(soa_participant_unregister(instance, &event) == SOA_INVALID_ARGUMENT);
    CHECK(soa_participant_unregister(instance, NULL) == SOA_INVALID_ARGUMENT);
    CHECK(soa_instance_close(&instance) == SOA_OK);
    CHECK(shm_unlink(name) == 0);
}

static void test_concurrent_duplicate(void)
{
    enum { CHILDREN = 12 };
    char name[128];
    int successes = 0;
    int duplicates = 0;
    int index;

    make_name(name, sizeof(name), "duplicate");
    CHECK(soa_instance_create(name, 2) == SOA_OK);
    for (index = 0; index < CHILDREN; index++) {
        pid_t child = fork();
        CHECK(child != -1);
        if (child == 0) {
            soa_instance_t *instance = NULL;
            soa_event_t event;
            soa_status_t result = soa_instance_open(name, &instance);

            if (result == SOA_OK) {
                result = soa_participant_register(
                    instance, SOA_ROLE_PRODUCER, 25, &event);
            }
            if (result == SOA_OK) {
                CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
            }
            if (instance != NULL) {
                CHECK(soa_instance_close(&instance) == SOA_OK);
            }
            _exit(result == SOA_OK ? 0 : (result == SOA_DUPLICATE_ID ? 1 : 2));
        }
    }
    for (index = 0; index < CHILDREN; index++) {
        int status;
        CHECK(wait(&status) > 0 && WIFEXITED(status));
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

static void write_byte(int fd, char value)
{
    ssize_t result;

    do {
        result = write(fd, &value, 1);
    } while (result == -1 && errno == EINTR);
    CHECK(result == 1);
}

static void read_bytes(int fd, char *buffer, size_t count)
{
    size_t total = 0;

    while (total < count) {
        ssize_t result = read(fd, buffer + total, count - total);
        if (result == -1 && errno == EINTR) {
            continue;
        }
        CHECK(result > 0);
        total += (size_t)result;
    }
}

static void test_32_per_role_simultaneously(void)
{
    enum { PER_ROLE = 32, CHILDREN = PER_ROLE * 2 };
    char name[128];
    char signals[CHILDREN];
    int ready[2];
    int release[2];
    int index;

    make_name(name, sizeof(name), "simultaneous");
    CHECK(soa_instance_create(name, 8) == SOA_OK);
    CHECK(pipe(ready) == 0 && pipe(release) == 0);
    for (index = 0; index < CHILDREN; index++) {
        pid_t child = fork();
        CHECK(child != -1);
        if (child == 0) {
            soa_instance_t *instance = NULL;
            soa_event_t event;
            soa_role_t role = index < PER_ROLE ? SOA_ROLE_PRODUCER
                                               : SOA_ROLE_CONSUMER;
            uint32_t id = (uint32_t)(index % PER_ROLE) + 1U;
            char release_signal;
            soa_status_t result;

            (void)close(ready[0]);
            (void)close(release[1]);
            result = soa_instance_open(name, &instance);
            if (result == SOA_OK) {
                result = soa_participant_register(instance, role, id, &event);
            }
            write_byte(ready[1], result == SOA_OK ? 'S' : 'F');
            read_bytes(release[0], &release_signal, 1);
            if (result == SOA_OK) {
                CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
                CHECK(soa_instance_close(&instance) == SOA_OK);
            }
            _exit(result == SOA_OK ? 0 : 1);
        }
    }
    CHECK(close(ready[1]) == 0 && close(release[0]) == 0);
    read_bytes(ready[0], signals, CHILDREN);
    for (index = 0; index < CHILDREN; index++) {
        CHECK(signals[index] == 'S');
        write_byte(release[1], 'R');
    }
    CHECK(close(ready[0]) == 0 && close(release[1]) == 0);
    for (index = 0; index < CHILDREN; index++) {
        int status;
        CHECK(wait(&status) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(shm_unlink(name) == 0);
}

static void test_connection_during_closing(void)
{
    char name[128];
    soa_instance_t *instance = NULL;
    soa_event_t event;

    make_name(name, sizeof(name), "closing");
    CHECK(soa_instance_create(name, 2) == SOA_OK);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(pthread_mutex_lock(&instance->shared->mutex) == 0);
    instance->shared->state = SOA_STATE_CLOSING;
    CHECK(pthread_mutex_unlock(&instance->shared->mutex) == 0);
    CHECK(soa_participant_register(instance, SOA_ROLE_CONSUMER, 1, &event) ==
          SOA_CLOSED);
    CHECK(soa_instance_close(&instance) == SOA_OK);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 1, &event) ==
          SOA_CLOSED);
    CHECK(soa_instance_close(&instance) == SOA_OK);
    CHECK(shm_unlink(name) == 0);
}

typedef struct {
    soa_shared_instance_t *shared;
    int ready_fd;
    int wait_result;
    int unlock_result;
} waiter_args_t;

static void *wait_for_registration(void *opaque)
{
    waiter_args_t *args = opaque;

    args->wait_result = pthread_mutex_lock(&args->shared->mutex);
    if (args->wait_result != 0) {
        return NULL;
    }
    write_byte(args->ready_fd, 'R');
    while (args->shared->active_producers == 0) {
        args->wait_result = pthread_cond_wait(&args->shared->state_changed,
                                              &args->shared->mutex);
        if (args->wait_result != 0) {
            break;
        }
    }
    args->unlock_result = pthread_mutex_unlock(&args->shared->mutex);
    return NULL;
}

static void test_finalizer_notification(void)
{
    char name[128];
    char ready_signal;
    int ready[2];
    pthread_t waiter;
    waiter_args_t args = {0};
    soa_instance_t *instance = NULL;
    soa_event_t event;

    make_name(name, sizeof(name), "notification");
    CHECK(soa_instance_create(name, 2) == SOA_OK);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(pipe(ready) == 0);
    args.shared = instance->shared;
    args.ready_fd = ready[1];
    CHECK(pthread_create(&waiter, NULL, wait_for_registration, &args) == 0);
    read_bytes(ready[0], &ready_signal, 1);
    CHECK(ready_signal == 'R');
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 1, &event) ==
          SOA_OK);
    CHECK(pthread_join(waiter, NULL) == 0);
    CHECK(args.wait_result == 0 && args.unlock_result == 0);
    CHECK(close(ready[0]) == 0 && close(ready[1]) == 0);
    CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
    CHECK(soa_instance_close(&instance) == SOA_OK);
    CHECK(shm_unlink(name) == 0);
}

static void test_connection_after_retirement(void)
{
    char name[128];
    struct flock lock = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
        .l_pid = 0
    };
    struct stat object_stat;
    soa_shared_instance_t *shared;
    soa_instance_t *instance = (soa_instance_t *)(uintptr_t)1;
    int result;
    int fd;

    make_name(name, sizeof(name), "retired");
    CHECK(soa_instance_create(name, 2) == SOA_OK);
    fd = shm_open(name, O_RDWR, 0);
    CHECK(fd != -1);
    do {
        result = fcntl(fd, F_SETLKW, &lock);
    } while (result == -1 && errno == EINTR);
    CHECK(result == 0);
    CHECK(fstat(fd, &object_stat) == 0 && object_stat.st_size > 0);
    shared = mmap(NULL, (size_t)object_stat.st_size, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
    CHECK(shared != MAP_FAILED);
    shared->publication = SOA_PUBLICATION_RETIRED;
    CHECK(pthread_cond_destroy(&shared->state_changed) == 0);
    CHECK(pthread_cond_destroy(&shared->not_empty) == 0);
    CHECK(pthread_cond_destroy(&shared->not_full) == 0);
    CHECK(pthread_mutex_destroy(&shared->mutex) == 0);
    lock.l_type = F_UNLCK;
    CHECK(fcntl(fd, F_SETLK, &lock) == 0);
    CHECK(munmap(shared, (size_t)object_stat.st_size) == 0);
    CHECK(close(fd) == 0);

    CHECK(soa_instance_open(name, &instance) == SOA_CLOSED);
    CHECK(instance == NULL);
    CHECK(shm_unlink(name) == 0);
}

int main(void)
{
    test_registration_lifecycle();
    test_invalid_arguments();
    test_concurrent_duplicate();
    test_32_per_role_simultaneously();
    test_connection_during_closing();
    test_finalizer_notification();
    test_connection_after_retirement();
    puts("OK: conexiones, registro y retirada (P2-07).");
    return EXIT_SUCCESS;
}
