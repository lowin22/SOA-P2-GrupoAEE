#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "soa/buffer.h"
#include "soa/events.h"
#include "soa/instance.h"
#include "soa/participants.h"
#include "soa/shutdown.h"
#include "soa/timing.h"
#include "../src/common/instance_internal.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                                \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while (0)

static void test_empty_shutdown_and_recreate(void)
{
    const char *name = "/soa_p2_shutdown_empty";
    const char *path = "/tmp/soa_p2_shutdown_summary.csv";
    soa_shutdown_snapshot_t *snapshot = NULL;
    soa_instance_t *instance = NULL;
    FILE *file;
    char contents[1024];
    size_t length;

    (void)shm_unlink(name);
    (void)unlink(path);
    CHECK(soa_instance_create(name, 8) == SOA_OK);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_shutdown_run(&instance, &snapshot) == SOA_OK);
    CHECK(instance == NULL);
    CHECK(snapshot != NULL);
    CHECK(soa_shutdown_snapshot_write_csv(snapshot, path) == SOA_OK);
    soa_shutdown_snapshot_free(&snapshot);
    CHECK(snapshot == NULL);

    file = fopen(path, "r");
    CHECK(file != NULL);
    length = fread(contents, 1, sizeof(contents) - 1U, file);
    contents[length] = '\0';
    CHECK(fclose(file) == 0);
    CHECK(strstr(contents, "schema_version,instance_name") != NULL);
    CHECK(strstr(contents, "1,/soa_p2_shutdown_empty,8,0,0,0,") != NULL);
    CHECK(soa_instance_open(name, &instance) == SOA_NOT_FOUND);
    CHECK(soa_instance_create(name, 4) == SOA_OK);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_shutdown_run(&instance, &snapshot) == SOA_OK);
    soa_shutdown_snapshot_free(&snapshot);
    (void)unlink(path);
}

static void test_no_consumer_preserves_open(void)
{
    const char *name = "/soa_p2_shutdown_no_consumer";
    char path[128];
    soa_shutdown_snapshot_t *snapshot = NULL;
    soa_instance_t *producer = NULL;
    soa_instance_t *consumer = NULL;
    soa_instance_t *finisher = NULL;
    soa_event_t event;
    soa_message_t message = {.producer_id = 1, .sequence = 1};
    struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000L};
    pid_t conductor;
    pid_t second;
    int child_status;

    (void)shm_unlink(name);
    (void)snprintf(path, sizeof(path), "/tmp/soa_p2_shutdown_active_%ld.csv",
                   (long)getpid());
    (void)unlink(path);
    CHECK(soa_instance_create(name, 2) == SOA_OK);
    CHECK(soa_instance_open(name, &producer) == SOA_OK);
    CHECK(soa_participant_register(producer, SOA_ROLE_PRODUCER, 1, &event) ==
          SOA_OK);
    CHECK(soa_buffer_put(producer, &message, &event) == SOA_OK);
    CHECK(soa_participant_unregister(producer, &event) == SOA_OK);
    CHECK(soa_instance_close(&producer) == SOA_OK);

    CHECK(soa_instance_open(name, &finisher) == SOA_OK);
    CHECK(soa_shutdown_run(&finisher, &snapshot) == SOA_NO_CONSUMER);
    CHECK(finisher != NULL);
    CHECK(snapshot == NULL);
    CHECK(soa_instance_close(&finisher) == SOA_OK);

    /* El rechazo anterior conserva OPEN y permite registrar un consumidor. */
    CHECK(soa_instance_open(name, &consumer) == SOA_OK);
    CHECK(soa_participant_register(consumer, SOA_ROLE_CONSUMER, 1, &event) ==
          SOA_OK);

    conductor = fork();
    CHECK(conductor >= 0);
    if (conductor == 0) {
        soa_instance_t *child_instance = NULL;
        soa_shutdown_snapshot_t *child_snapshot = NULL;
        soa_status_t status = soa_instance_open(name, &child_instance);

        if (status == SOA_OK) {
            status = soa_shutdown_run(&child_instance, &child_snapshot);
        }
        if (status == SOA_OK) {
            status = soa_shutdown_snapshot_write_csv(child_snapshot, path);
        }
        soa_shutdown_snapshot_free(&child_snapshot);
        _exit(status == SOA_OK ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    for (;;) {
        uint32_t state;

        CHECK(pthread_mutex_lock(&consumer->shared->mutex) == 0);
        state = consumer->shared->state;
        CHECK(pthread_mutex_unlock(&consumer->shared->mutex) == 0);
        if (state == SOA_STATE_CLOSING) {
            break;
        }
        CHECK(state == SOA_STATE_OPEN);
        CHECK(nanosleep(&pause, NULL) == 0);
    }

    second = fork();
    CHECK(second >= 0);
    if (second == 0) {
        soa_instance_t *second_instance = NULL;
        soa_shutdown_snapshot_t *second_snapshot = NULL;
        soa_status_t status = soa_instance_open(name, &second_instance);

        if (status == SOA_OK) {
            status = soa_shutdown_run(&second_instance, &second_snapshot);
        }
        if (second_instance != NULL) {
            (void)soa_instance_close(&second_instance);
        }
        soa_shutdown_snapshot_free(&second_snapshot);
        _exit(status == SOA_CLOSED || status == SOA_FINISHER_EXISTS
                  ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    CHECK(waitpid(second, &child_status, 0) == second);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);

    CHECK(soa_buffer_take(consumer, &message, &event) == SOA_OK);
    CHECK(event.state == SOA_STATE_CLOSING);
    CHECK(soa_buffer_take(consumer, &message, &event) == SOA_CLOSED);
    CHECK(soa_participant_unregister(consumer, &event) == SOA_OK);
    CHECK(soa_instance_close(&consumer) == SOA_OK);
    CHECK(waitpid(conductor, &child_status, 0) == conductor);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
    CHECK(soa_instance_open(name, &finisher) == SOA_NOT_FOUND);

    {
        FILE *file = fopen(path, "r");
        char contents[1024];
        size_t length;

        CHECK(file != NULL);
        length = fread(contents, 1, sizeof(contents) - 1U, file);
        contents[length] = '\0';
        CHECK(fclose(file) == 0);
        CHECK(strstr(contents,
                     "1,/soa_p2_shutdown_no_consumer,2,1,1,0,") != NULL);
    }
    (void)unlink(path);
}

static void child_ready(int fd)
{
    const char ready = 'R';

    CHECK(write(fd, &ready, 1) == 1);
    CHECK(close(fd) == 0);
}

static void wait_ready(int fd)
{
    char ready;

    CHECK(read(fd, &ready, 1) == 1);
    CHECK(ready == 'R');
    CHECK(close(fd) == 0);
}

static void test_shutdown_wakes_waiting_workers(void)
{
    const char *name = "/soa_p2_shutdown_waiters";
    soa_shutdown_snapshot_t *snapshot = NULL;
    soa_instance_t *finisher = NULL;
    int producer_pipe[2];
    int consumer_pipe[2];
    pid_t producer;
    pid_t consumer;
    int child_status;

    (void)shm_unlink(name);
    CHECK(soa_instance_create(name, 1) == SOA_OK);
    CHECK(pipe(producer_pipe) == 0);
    CHECK(pipe(consumer_pipe) == 0);

    producer = fork();
    CHECK(producer >= 0);
    if (producer == 0) {
        soa_instance_t *instance = NULL;
        soa_event_t event;
        uint64_t now;
        soa_status_t status;

        (void)close(producer_pipe[0]);
        (void)close(consumer_pipe[0]);
        (void)close(consumer_pipe[1]);
        CHECK(soa_instance_open(name, &instance) == SOA_OK);
        CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 1, &event) ==
              SOA_OK);
        child_ready(producer_pipe[1]);
        CHECK(monotonic_time_ns(&now) == SOA_OK);
        status = soa_producer_pause(instance, now + UINT64_C(30000000000));
        CHECK(status == SOA_CLOSED);
        CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
        CHECK(soa_instance_close(&instance) == SOA_OK);
        _exit(EXIT_SUCCESS);
    }

    consumer = fork();
    CHECK(consumer >= 0);
    if (consumer == 0) {
        soa_instance_t *instance = NULL;
        soa_event_t event;
        soa_message_t message;

        (void)close(consumer_pipe[0]);
        (void)close(producer_pipe[0]);
        (void)close(producer_pipe[1]);
        CHECK(soa_instance_open(name, &instance) == SOA_OK);
        CHECK(soa_participant_register(instance, SOA_ROLE_CONSUMER, 1, &event) ==
              SOA_OK);
        child_ready(consumer_pipe[1]);
        CHECK(soa_buffer_take(instance, &message, &event) == SOA_CLOSED);
        CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
        CHECK(soa_instance_close(&instance) == SOA_OK);
        _exit(EXIT_SUCCESS);
    }

    CHECK(close(producer_pipe[1]) == 0);
    CHECK(close(consumer_pipe[1]) == 0);
    wait_ready(producer_pipe[0]);
    wait_ready(consumer_pipe[0]);
    CHECK(soa_instance_open(name, &finisher) == SOA_OK);
    CHECK(soa_shutdown_run(&finisher, &snapshot) == SOA_OK);
    soa_shutdown_snapshot_free(&snapshot);
    CHECK(waitpid(producer, &child_status, 0) == producer);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
    CHECK(waitpid(consumer, &child_status, 0) == consumer);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
}

static void test_shutdown_waits_for_consumer_processing(void)
{
    const char *name = "/soa_p2_shutdown_processing";
    soa_shutdown_snapshot_t *snapshot = NULL;
    soa_instance_t *finisher = NULL;
    int ready_pipe[2];
    pid_t consumer;
    int child_status;

    (void)shm_unlink(name);
    CHECK(soa_instance_create(name, 1) == SOA_OK);
    CHECK(pipe(ready_pipe) == 0);
    consumer = fork();
    CHECK(consumer >= 0);
    if (consumer == 0) {
        soa_instance_t *instance = NULL;
        soa_event_t event;
        uint64_t now;

        (void)close(ready_pipe[0]);
        CHECK(soa_instance_open(name, &instance) == SOA_OK);
        CHECK(soa_participant_register(instance, SOA_ROLE_CONSUMER, 1, &event) ==
              SOA_OK);
        CHECK(monotonic_time_ns(&now) == SOA_OK);
        child_ready(ready_pipe[1]);
        CHECK(consumer_process_until(now + UINT64_C(100000000)) == SOA_OK);
        CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
        CHECK(soa_instance_close(&instance) == SOA_OK);
        _exit(EXIT_SUCCESS);
    }

    CHECK(close(ready_pipe[1]) == 0);
    wait_ready(ready_pipe[0]);
    CHECK(soa_instance_open(name, &finisher) == SOA_OK);
    CHECK(soa_shutdown_run(&finisher, &snapshot) == SOA_OK);
    soa_shutdown_snapshot_free(&snapshot);
    CHECK(waitpid(consumer, &child_status, 0) == consumer);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
}

static void test_shutdown_waits_for_unregistered_connection(void)
{
    const char *name = "/soa_p2_shutdown_connection";
    soa_shutdown_snapshot_t *snapshot = NULL;
    soa_instance_t *finisher = NULL;
    int ready_pipe[2];
    pid_t connector;
    int child_status;

    (void)shm_unlink(name);
    CHECK(soa_instance_create(name, 1) == SOA_OK);
    CHECK(pipe(ready_pipe) == 0);
    connector = fork();
    CHECK(connector >= 0);
    if (connector == 0) {
        soa_instance_t *connection = NULL;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000000L};

        (void)close(ready_pipe[0]);
        CHECK(soa_instance_open(name, &connection) == SOA_OK);
        child_ready(ready_pipe[1]);
        CHECK(nanosleep(&pause, NULL) == 0);
        CHECK(soa_instance_close(&connection) == SOA_OK);
        _exit(EXIT_SUCCESS);
    }

    CHECK(close(ready_pipe[1]) == 0);
    wait_ready(ready_pipe[0]);
    CHECK(soa_instance_open(name, &finisher) == SOA_OK);
    CHECK(soa_shutdown_run(&finisher, &snapshot) == SOA_OK);
    soa_shutdown_snapshot_free(&snapshot);
    CHECK(waitpid(connector, &child_status, 0) == connector);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
}

int main(void)
{
    alarm(30);
    test_empty_shutdown_and_recreate();
    test_no_consumer_preserves_open();
    test_shutdown_wakes_waiting_workers();
    test_shutdown_waits_for_consumer_processing();
    test_shutdown_waits_for_unregistered_connection();
    puts("OK: cierre seguro, resumen privado y reutilizacion comprobados.");
    return EXIT_SUCCESS;
}
