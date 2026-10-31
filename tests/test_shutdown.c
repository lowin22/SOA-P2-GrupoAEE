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

static void wait_for_shared_state(soa_instance_t *instance, uint32_t state);

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

    wait_for_shared_state(consumer, SOA_STATE_CLOSING);

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

static void wait_for_shared_state(soa_instance_t *instance, uint32_t state)
{
    CHECK(pthread_mutex_lock(&instance->shared->mutex) == 0);
    while (instance->shared->state != state) {
        CHECK(instance->shared->state == SOA_STATE_OPEN);
        CHECK(pthread_cond_wait(&instance->shared->state_changed,
                                &instance->shared->mutex) == 0);
    }
    CHECK(pthread_mutex_unlock(&instance->shared->mutex) == 0);
}

static void test_shutdown_full_buffer_rejects_blocked_put(void)
{
    const char *name = "/soa_p2_shutdown_full";
    const char *path = "/tmp/soa_p2_shutdown_full.csv";
    soa_instance_t *consumer = NULL;
    soa_event_t event;
    soa_message_t message;
    int ready_pipe[2];
    int finisher_pipe[2];
    pid_t producer;
    pid_t finisher;
    int child_status;

    (void)shm_unlink(name);
    (void)unlink(path);
    CHECK(soa_instance_create(name, 1) == SOA_OK);
    CHECK(pipe(ready_pipe) == 0);
    CHECK(pipe(finisher_pipe) == 0);

    finisher = fork();
    CHECK(finisher >= 0);
    if (finisher == 0) {
        soa_instance_t *instance = NULL;
        soa_shutdown_snapshot_t *snapshot = NULL;
        soa_status_t status;

        (void)close(ready_pipe[0]);
        (void)close(ready_pipe[1]);
        (void)close(finisher_pipe[1]);
        wait_ready(finisher_pipe[0]);
        status = soa_instance_open(name, &instance);
        if (status == SOA_OK) {
            status = soa_shutdown_run(&instance, &snapshot);
        }
        if (status == SOA_OK) {
            status = soa_shutdown_snapshot_write_csv(snapshot, path);
        }
        soa_shutdown_snapshot_free(&snapshot);
        _exit(status == SOA_OK ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    producer = fork();
    CHECK(producer >= 0);
    if (producer == 0) {
        soa_instance_t *instance = NULL;
        soa_message_t first = {.producer_id = 1, .sequence = 1};
        soa_message_t second = {.producer_id = 1, .sequence = 2};
        soa_event_t child_event;

        (void)close(ready_pipe[0]);
        (void)close(finisher_pipe[0]);
        (void)close(finisher_pipe[1]);
        CHECK(soa_instance_open(name, &instance) == SOA_OK);
        CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 1,
                                       &child_event) == SOA_OK);
        CHECK(soa_buffer_put(instance, &first, &child_event) == SOA_OK);
        child_ready(ready_pipe[1]);
        CHECK(soa_buffer_put(instance, &second, &child_event) == SOA_CLOSED);
        CHECK(soa_participant_unregister(instance, &child_event) == SOA_OK);
        CHECK(soa_instance_close(&instance) == SOA_OK);
        _exit(EXIT_SUCCESS);
    }

    CHECK(close(ready_pipe[1]) == 0);
    CHECK(close(finisher_pipe[0]) == 0);
    wait_ready(ready_pipe[0]);
    CHECK(soa_instance_open(name, &consumer) == SOA_OK);
    CHECK(soa_participant_register(consumer, SOA_ROLE_CONSUMER, 1, &event) ==
          SOA_OK);
    CHECK(pthread_mutex_lock(&consumer->shared->mutex) == 0);
    CHECK(consumer->shared->state == SOA_STATE_OPEN);
    CHECK(consumer->shared->occupancy == 1);
    CHECK(consumer->shared->accepted_count == 1);
    CHECK(consumer->shared->active_producers == 1);
    CHECK(pthread_mutex_unlock(&consumer->shared->mutex) == 0);
    CHECK(waitpid(producer, &child_status, WNOHANG) == 0);
    child_ready(finisher_pipe[1]);
    wait_for_shared_state(consumer, SOA_STATE_CLOSING);
    CHECK(pthread_mutex_lock(&consumer->shared->mutex) == 0);
    CHECK(consumer->shared->accepted_count == 1);
    CHECK(consumer->shared->occupancy == 1);
    CHECK(pthread_mutex_unlock(&consumer->shared->mutex) == 0);

    CHECK(soa_buffer_take(consumer, &message, &event) == SOA_OK);
    CHECK(message.producer_id == 1 && message.sequence == 1);
    CHECK(event.state == SOA_STATE_CLOSING);
    CHECK(soa_buffer_take(consumer, &message, &event) == SOA_CLOSED);
    CHECK(soa_participant_unregister(consumer, &event) == SOA_OK);
    CHECK(soa_instance_close(&consumer) == SOA_OK);

    CHECK(waitpid(producer, &child_status, 0) == producer);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
    CHECK(waitpid(finisher, &child_status, 0) == finisher);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);

    {
        FILE *file = fopen(path, "r");
        char contents[1024];
        size_t length;

        CHECK(file != NULL);
        length = fread(contents, 1, sizeof(contents) - 1U, file);
        contents[length] = '\0';
        CHECK(fclose(file) == 0);
        CHECK(strstr(contents, "1,/soa_p2_shutdown_full,1,1,1,0,") != NULL);
    }
    CHECK(soa_instance_open(name, &consumer) == SOA_NOT_FOUND);
    (void)unlink(path);
}

static void test_shutdown_wakes_waiting_workers(void)
{
    const char *name = "/soa_p2_shutdown_waiters";
    const char *path = "/tmp/soa_p2_shutdown_waiters.csv";
    soa_shutdown_snapshot_t *snapshot = NULL;
    soa_instance_t *finisher = NULL;
    int producer_pipe[2];
    int consumer_pipe[2];
    pid_t producer;
    pid_t consumer;
    int child_status;

    (void)shm_unlink(name);
    (void)unlink(path);
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
    CHECK(soa_shutdown_snapshot_write_csv(snapshot, path) == SOA_OK);
    soa_shutdown_snapshot_free(&snapshot);
    CHECK(waitpid(producer, &child_status, 0) == producer);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
    CHECK(waitpid(consumer, &child_status, 0) == consumer);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);

    {
        FILE *file = fopen(path, "r");
        char contents[1024];
        size_t length;

        CHECK(file != NULL);
        length = fread(contents, 1, sizeof(contents) - 1U, file);
        contents[length] = '\0';
        CHECK(fclose(file) == 0);
        CHECK(strstr(contents, "1,/soa_p2_shutdown_waiters,1,0,0,0,") != NULL);
    }
    (void)unlink(path);
}

static void test_shutdown_waits_for_consumer_processing(void)
{
    const char *name = "/soa_p2_shutdown_processing";
    const char *path = "/tmp/soa_p2_shutdown_processing.csv";
    soa_shutdown_snapshot_t *snapshot = NULL;
    soa_instance_t *producer = NULL;
    soa_instance_t *finisher = NULL;
    soa_event_t event;
    soa_message_t draft = {.producer_id = 1, .sequence = 1};
    int ready_pipe[2];
    pid_t consumer;
    int child_status;

    (void)shm_unlink(name);
    (void)unlink(path);
    CHECK(soa_instance_create(name, 1) == SOA_OK);
    CHECK(soa_instance_open(name, &producer) == SOA_OK);
    CHECK(soa_participant_register(producer, SOA_ROLE_PRODUCER, 1, &event) ==
          SOA_OK);
    CHECK(soa_buffer_put(producer, &draft, &event) == SOA_OK);
    CHECK(soa_participant_unregister(producer, &event) == SOA_OK);
    CHECK(soa_instance_close(&producer) == SOA_OK);
    CHECK(pipe(ready_pipe) == 0);
    consumer = fork();
    CHECK(consumer >= 0);
    if (consumer == 0) {
        soa_instance_t *instance = NULL;
        soa_event_t event;
        soa_message_t message;
        uint64_t now;

        (void)close(ready_pipe[0]);
        CHECK(soa_instance_open(name, &instance) == SOA_OK);
        CHECK(soa_participant_register(instance, SOA_ROLE_CONSUMER, 1, &event) ==
              SOA_OK);
        CHECK(soa_buffer_take(instance, &message, &event) == SOA_OK);
        CHECK(message.producer_id == 1 && message.sequence == 1);
        CHECK(event.occupancy == 0);
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
    CHECK(soa_shutdown_snapshot_write_csv(snapshot, path) == SOA_OK);
    soa_shutdown_snapshot_free(&snapshot);
    CHECK(waitpid(consumer, &child_status, 0) == consumer);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);

    {
        FILE *file = fopen(path, "r");
        char contents[1024];
        size_t length;

        CHECK(file != NULL);
        length = fread(contents, 1, sizeof(contents) - 1U, file);
        contents[length] = '\0';
        CHECK(fclose(file) == 0);
        CHECK(strstr(contents,
                     "1,/soa_p2_shutdown_processing,1,1,1,0,") != NULL);
    }
    (void)unlink(path);
}

static void test_shutdown_waits_for_unregistered_connection(void)
{
    const char *name = "/soa_p2_shutdown_connection";
    soa_instance_t *observer = NULL;
    soa_event_t event;
    int ready_pipe[2];
    int continue_pipe[2];
    int finisher_pipe[2];
    pid_t connector;
    pid_t finisher;
    int child_status;

    (void)shm_unlink(name);
    CHECK(soa_instance_create(name, 1) == SOA_OK);
    CHECK(pipe(ready_pipe) == 0);
    CHECK(pipe(continue_pipe) == 0);
    CHECK(pipe(finisher_pipe) == 0);
    connector = fork();
    CHECK(connector >= 0);
    if (connector == 0) {
        soa_instance_t *connection = NULL;
        soa_event_t child_event;

        (void)close(ready_pipe[0]);
        (void)close(continue_pipe[1]);
        (void)close(finisher_pipe[0]);
        (void)close(finisher_pipe[1]);
        CHECK(soa_instance_open(name, &connection) == SOA_OK);
        child_ready(ready_pipe[1]);
        wait_ready(continue_pipe[0]);
        CHECK(soa_participant_register(connection, SOA_ROLE_PRODUCER, 1,
                                       &child_event) == SOA_CLOSED);
        CHECK(soa_instance_close(&connection) == SOA_OK);
        _exit(EXIT_SUCCESS);
    }

    finisher = fork();
    CHECK(finisher >= 0);
    if (finisher == 0) {
        soa_instance_t *instance = NULL;
        soa_shutdown_snapshot_t *snapshot = NULL;
        soa_status_t status;

        (void)close(ready_pipe[0]);
        (void)close(ready_pipe[1]);
        (void)close(continue_pipe[0]);
        (void)close(continue_pipe[1]);
        (void)close(finisher_pipe[1]);
        wait_ready(finisher_pipe[0]);
        status = soa_instance_open(name, &instance);
        if (status == SOA_OK) {
            status = soa_shutdown_run(&instance, &snapshot);
        }
        soa_shutdown_snapshot_free(&snapshot);
        _exit(status == SOA_OK ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    CHECK(close(ready_pipe[1]) == 0);
    CHECK(close(continue_pipe[0]) == 0);
    CHECK(close(finisher_pipe[0]) == 0);
    wait_ready(ready_pipe[0]);
    CHECK(soa_instance_open(name, &observer) == SOA_OK);
    CHECK(soa_participant_register(observer, SOA_ROLE_CONSUMER, 1, &event) ==
          SOA_OK);
    child_ready(finisher_pipe[1]);
    wait_for_shared_state(observer, SOA_STATE_CLOSING);
    child_ready(continue_pipe[1]);
    CHECK(soa_participant_unregister(observer, &event) == SOA_OK);
    CHECK(soa_instance_close(&observer) == SOA_OK);
    CHECK(waitpid(connector, &child_status, 0) == connector);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
    CHECK(waitpid(finisher, &child_status, 0) == finisher);
    CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == EXIT_SUCCESS);
}

int main(void)
{
    alarm(30);
    test_empty_shutdown_and_recreate();
    test_no_consumer_preserves_open();
    test_shutdown_full_buffer_rejects_blocked_put();
    test_shutdown_wakes_waiting_workers();
    test_shutdown_waits_for_consumer_processing();
    test_shutdown_waits_for_unregistered_connection();
    puts("OK: intercalados de cierre, conservacion y reutilizacion comprobados.");
    return EXIT_SUCCESS;
}
