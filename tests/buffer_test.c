#include "soa/buffer.h"
#include "../src/common/instance_internal.h"

#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(test) do { \
    if (!(test)) { \
        fprintf(stderr, "%s linea %d fallo %s\n", __FILE__, __LINE__, #test); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct {
    int fd;
    size_t size;
    soa_shared_instance_t *shared;
} fixture_t;

static fixture_t create_fixture(uint32_t capacity)
{
    static unsigned serial;
    char name[80];
    snprintf(name, sizeof name, "/buffer_test_%ld_%u", (long)getpid(), serial++);
    fixture_t fixture = {.size = offsetof(soa_shared_instance_t, messages)
                                   + capacity * sizeof(soa_message_t)};
    fixture.fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fixture.fd >= 0);
    CHECK(shm_unlink(name) == 0);
    CHECK(ftruncate(fixture.fd, (off_t)fixture.size) == 0);
    fixture.shared = mmap(NULL, fixture.size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fixture.fd, 0);
    CHECK(fixture.shared != MAP_FAILED);
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    CHECK(pthread_mutexattr_init(&mutex_attr) == 0);
    CHECK(pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED) == 0);
    CHECK(pthread_condattr_init(&cond_attr) == 0);
    CHECK(pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED) == 0);
    CHECK(pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) == 0);
    CHECK(pthread_mutex_init(&fixture.shared->mutex, &mutex_attr) == 0);
    CHECK(pthread_cond_init(&fixture.shared->not_full, &cond_attr) == 0);
    CHECK(pthread_cond_init(&fixture.shared->not_empty, &cond_attr) == 0);
    CHECK(pthread_cond_init(&fixture.shared->state_changed, &cond_attr) == 0);
    CHECK(pthread_mutexattr_destroy(&mutex_attr) == 0);
    CHECK(pthread_condattr_destroy(&cond_attr) == 0);
    fixture.shared->capacity = capacity;
    fixture.shared->state = SOA_STATE_OPEN;
    fixture.shared->next_enqueue_seq = 1;
    fixture.shared->next_dequeue_seq = 1;
    return fixture;
}

static void destroy_fixture(fixture_t *fixture)
{
    CHECK(pthread_cond_destroy(&fixture->shared->state_changed) == 0);
    CHECK(pthread_cond_destroy(&fixture->shared->not_empty) == 0);
    CHECK(pthread_cond_destroy(&fixture->shared->not_full) == 0);
    CHECK(pthread_mutex_destroy(&fixture->shared->mutex) == 0);
    CHECK(munmap(fixture->shared, fixture->size) == 0);
    CHECK(close(fixture->fd) == 0);
}

static soa_instance_t handle(soa_shared_instance_t *shared, soa_role_t role, uint32_t id)
{
    return (soa_instance_t){
        .shared = shared,
        .role = role,
        .participant_id = id,
        .registered = true,
        .fd = 0,
        .read_lock_held = true
    };
}

static soa_message_t draft(uint32_t id, uint64_t sequence)
{
    soa_message_t message = {.producer_id = id, .sequence = sequence};
    for (size_t i = 0; i < SOA_PAYLOAD_SIZE; ++i) {
        message.payload[i] = (unsigned char)(id + sequence + i);
    }
    return message;
}

static void conservation(soa_shared_instance_t *shared)
{
    CHECK(shared->occupancy <= shared->capacity);
    CHECK(shared->accepted_count == shared->extracted_count + shared->occupancy);
    CHECK(shared->head < shared->capacity && shared->tail < shared->capacity);
}

static void check_message(const soa_message_t *message)
{
    soa_message_t expected = draft(message->producer_id, message->sequence);
    CHECK(message->reserved == 0);
    CHECK(memcmp(message->payload, expected.payload, SOA_PAYLOAD_SIZE) == 0);
}

static void test_fifo(uint32_t capacity)
{
    fixture_t f = create_fixture(capacity);
    soa_instance_t producer = handle(f.shared, SOA_ROLE_PRODUCER, 1);
    soa_instance_t consumer = handle(f.shared, SOA_ROLE_CONSUMER, 2);
    uint64_t sequence = 1;
    for (unsigned round = 0; round < 3; ++round) {
        for (uint32_t i = 0; i < capacity; ++i) {
            soa_message_t message = draft(1, sequence);
            message.reserved = 99;
            message.enqueue_seq = 999;
            soa_event_t event;
            CHECK(soa_buffer_put(&producer, &message, &event) == SOA_OK);
            CHECK(event.kind == SOA_EVENT_ENQUEUE && event.participant_id == 1);
            CHECK(event.message.enqueue_seq == sequence++);
            CHECK(event.timestamp_ns == event.message.enqueue_time_ns);
            CHECK(event.occupancy == i + 1 && event.state == SOA_STATE_OPEN);
            CHECK(message.reserved == 99 && message.enqueue_seq == 999);
            conservation(f.shared);
        }
        for (uint32_t i = 0; i < capacity; ++i) {
            soa_message_t message;
            soa_event_t event;
            CHECK(soa_buffer_take(&consumer, &message, &event) == SOA_OK);
            CHECK(message.sequence == round * capacity + i + 1);
            CHECK(event.dequeue_seq == message.enqueue_seq);
            CHECK(event.role == SOA_ROLE_CONSUMER && event.participant_id == 2);
            CHECK(event.kind == SOA_EVENT_DEQUEUE && event.occupancy == capacity - i - 1);
            CHECK(event.timestamp_ns >= message.enqueue_time_ns);
            CHECK(event.timestamp_ns == f.shared->last_dequeue_ns);
            CHECK(memcmp(&message, &event.message, sizeof message) == 0);
            check_message(&message);
            conservation(f.shared);
        }
    }
    for (uint32_t i = 0; i < capacity; ++i) {
        soa_message_t message = draft(1, sequence++);
        soa_event_t event;
        CHECK(soa_buffer_put(&producer, &message, &event) == SOA_OK);
    }
    for (unsigned i = 0; i < 2 * capacity; ++i) {
        soa_message_t message;
        soa_event_t event;
        CHECK(soa_buffer_take(&consumer, &message, &event) == SOA_OK);
        CHECK(message.sequence == message.enqueue_seq);
        CHECK(event.dequeue_seq == message.enqueue_seq);
        check_message(&message);
        message = draft(1, sequence++);
        CHECK(soa_buffer_put(&producer, &message, &event) == SOA_OK);
        conservation(f.shared);
    }
    for (uint32_t i = 0; i < capacity; ++i) {
        soa_message_t message;
        soa_event_t event;
        CHECK(soa_buffer_take(&consumer, &message, &event) == SOA_OK);
        CHECK(message.sequence == message.enqueue_seq);
        CHECK(event.dequeue_seq == message.enqueue_seq);
    }
    conservation(f.shared);
    destroy_fixture(&f);
}

static void close_buffer(soa_shared_instance_t *shared)
{
    CHECK(pthread_mutex_lock(&shared->mutex) == 0);
    shared->state = SOA_STATE_CLOSING;
    CHECK(pthread_cond_broadcast(&shared->not_full) == 0);
    CHECK(pthread_cond_broadcast(&shared->not_empty) == 0);
    CHECK(pthread_cond_broadcast(&shared->state_changed) == 0);
    CHECK(pthread_mutex_unlock(&shared->mutex) == 0);
}

static void remap_child(fixture_t *fixture)
{
    soa_shared_instance_t *mapped = mmap(NULL, fixture->size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fixture->fd, 0);
    CHECK(mapped != MAP_FAILED && mapped != fixture->shared);
    CHECK(munmap(fixture->shared, fixture->size) == 0);
    fixture->shared = mapped;
    alarm(10);
}

/* El pipe confirma el intento y permite detectar una salida prematura */
static void test_wait(bool producer_waits, bool closing)
{
    fixture_t f = create_fixture(1);
    soa_instance_t producer = handle(f.shared, SOA_ROLE_PRODUCER, 1);
    soa_instance_t consumer = handle(f.shared, SOA_ROLE_CONSUMER, 1);
    soa_message_t message = draft(1, 1);
    soa_event_t event;
    if (producer_waits) {
        CHECK(soa_buffer_put(&producer, &message, &event) == SOA_OK);
    }
    int pipe_fd[2];
    CHECK(pipe(pipe_fd) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        close(pipe_fd[0]);
        remap_child(&f);
        soa_instance_t worker = handle(f.shared,
            producer_waits ? SOA_ROLE_PRODUCER : SOA_ROLE_CONSUMER, 1);
        char ready = 'R';
        CHECK(write(pipe_fd[1], &ready, 1) == 1);
        soa_status_t result = producer_waits
            ? soa_buffer_put(&worker, &message, &event)
            : soa_buffer_take(&worker, &message, &event);
        CHECK(result == (closing ? SOA_CLOSED : SOA_OK));
        if (!producer_waits && !closing) {
            check_message(&message);
        }
        CHECK(write(pipe_fd[1], &result, sizeof result) == (ssize_t)sizeof result);
        _exit(0);
    }
    close(pipe_fd[1]);
    char ready;
    CHECK(read(pipe_fd[0], &ready, 1) == 1 && ready == 'R');
    struct pollfd result_fd = {.fd = pipe_fd[0], .events = POLLIN};
    CHECK(poll(&result_fd, 1, 50) == 0);
    CHECK(pthread_mutex_lock(&f.shared->mutex) == 0);
    CHECK(pthread_cond_broadcast(producer_waits ? &f.shared->not_full
                                               : &f.shared->not_empty) == 0);
    CHECK(pthread_mutex_unlock(&f.shared->mutex) == 0);
    CHECK(poll(&result_fd, 1, 50) == 0);
    if (closing) {
        close_buffer(f.shared);
    } else if (producer_waits) {
        CHECK(soa_buffer_take(&consumer, &message, &event) == SOA_OK);
    } else {
        CHECK(soa_buffer_put(&producer, &message, &event) == SOA_OK);
    }
    CHECK(poll(&result_fd, 1, 5000) == 1);
    soa_status_t result;
    CHECK(read(pipe_fd[0], &result, sizeof result) == (ssize_t)sizeof result);
    CHECK(result == (closing ? SOA_CLOSED : SOA_OK));
    CHECK(close(pipe_fd[0]) == 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    conservation(f.shared);
    if (closing && producer_waits) {
        CHECK(f.shared->accepted_count == 1 && f.shared->occupancy == 1);
        CHECK(soa_buffer_take(&consumer, &message, &event) == SOA_OK);
        CHECK(event.state == SOA_STATE_CLOSING);
        CHECK(soa_buffer_take(&consumer, &message, &event) == SOA_CLOSED);
    }
    destroy_fixture(&f);
}

static void test_errors(void)
{
    fixture_t f = create_fixture(1);
    soa_instance_t p = handle(f.shared, SOA_ROLE_PRODUCER, 1);
    soa_instance_t c = handle(f.shared, SOA_ROLE_CONSUMER, 1);
    soa_message_t message = draft(1, 1);
    soa_event_t event = {.timestamp_ns = 77};
    CHECK(soa_buffer_put(NULL, &message, &event) == SOA_INVALID_ARGUMENT);
    CHECK(soa_buffer_take(&p, &message, &event) == SOA_INVALID_ARGUMENT);
    CHECK(soa_buffer_put(&c, &message, &event) == SOA_INVALID_ARGUMENT);
    CHECK(soa_buffer_put(&p, NULL, &event) == SOA_INVALID_ARGUMENT);
    CHECK(soa_buffer_take(&c, &message, NULL) == SOA_INVALID_ARGUMENT);
    p.fd = -1;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_INVALID_ARGUMENT);
    p.fd = 0;
    p.read_lock_held = false;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_INVALID_ARGUMENT);
    p.read_lock_held = true;
    p.registered = false;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_INVALID_ARGUMENT);
    p.registered = true;
    f.shared->next_enqueue_seq = UINT64_MAX;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_OVERFLOW);
    CHECK(f.shared->occupancy == 0 && event.timestamp_ns == 77);
    f.shared->next_enqueue_seq = 1;
    f.shared->accepted_count = UINT64_MAX;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_OVERFLOW);
    f.shared->accepted_count = 0;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_OK);
    f.shared->next_dequeue_seq = UINT64_MAX;
    CHECK(soa_buffer_take(&c, &message, &event) == SOA_OVERFLOW);
    CHECK(f.shared->occupancy == 1);
    f.shared->next_dequeue_seq = 1;
    f.shared->extracted_count = UINT64_MAX;
    CHECK(soa_buffer_take(&c, &message, &event) == SOA_OVERFLOW);
    f.shared->extracted_count = 0;
    close_buffer(f.shared);
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_CLOSED);
    CHECK(soa_buffer_take(&c, &message, &event) == SOA_OK);
    CHECK(soa_buffer_take(&c, &message, &event) == SOA_CLOSED);
    f.shared->state = SOA_STATE_CLOSED;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_CLOSED);
    CHECK(soa_buffer_take(&c, &message, &event) == SOA_CLOSED);
    f.shared->state = SOA_STATE_OPEN;
    CHECK(soa_buffer_put(&p, &message, &event) == SOA_OK);
    f.shared->state = SOA_STATE_CLOSED;
    CHECK(soa_buffer_take(&c, &message, &event) == SOA_CLOSED);
    CHECK(f.shared->occupancy == 1);
    f.shared->capacity = 0;
    CHECK(soa_buffer_take(&c, &message, &event) == SOA_INCOMPATIBLE_INSTANCE);
    f.shared->capacity = 1;
    destroy_fixture(&f);
}

int main(void)
{
    alarm(30);
    test_fifo(1);
    test_fifo(2);
    test_fifo(8);
    test_fifo(1024);
    test_wait(true, false);
    test_wait(false, false);
    test_wait(true, true);
    test_wait(false, true);
    test_errors();
    puts("OK buffer FIFO bloqueo entre procesos cierre y conservacion");
    return EXIT_SUCCESS;
}
