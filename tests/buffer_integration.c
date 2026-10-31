#include "soa/buffer.h"
#include "soa/participants.h"
#include "../src/common/instance_internal.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(test) do { \
    if (!(test)) { \
        fprintf(stderr, "%s linea %d fallo %s\n", __FILE__, __LINE__, #test); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Carga conocida compatible con el verificador del proyecto */
static void fill_payload(soa_message_t *message)
{
    uint64_t state = ((uint64_t)message->producer_id << 32) ^ message->sequence;
    for (size_t block = 0; block < 4; ++block) {
        state += UINT64_C(0x9E3779B97F4A7C15);
        uint64_t value = state;
        value = (value ^ (value >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        value = (value ^ (value >> 27)) * UINT64_C(0x94D049BB133111EB);
        value ^= value >> 31;
        for (size_t byte = 0; byte < 8; ++byte) {
            message->payload[block * 8 + byte] = (unsigned char)(value >> (byte * 8));
        }
    }
}

/* Escribe los eventos despues de liberar el mutex */
static void write_event(FILE *file, const soa_event_t *event)
{
    const char *role = event->role == SOA_ROLE_PRODUCER ? "producer" : "consumer";
    const char *kind;
    switch (event->kind) {
    case SOA_EVENT_JOIN: kind = "JOIN"; break;
    case SOA_EVENT_LEAVE: kind = "LEAVE"; break;
    case SOA_EVENT_ENQUEUE: kind = "ENQUEUE"; break;
    case SOA_EVENT_DEQUEUE: kind = "DEQUEUE"; break;
    default: CHECK(false);
    }
    const char *state;
    switch (event->state) {
    case SOA_STATE_OPEN: state = "OPEN"; break;
    case SOA_STATE_CLOSING: state = "CLOSING"; break;
    case SOA_STATE_CLOSED: state = "CLOSED"; break;
    default: CHECK(false);
    }
    CHECK(fprintf(file, "1,%s,%u,%s,%" PRIu64 ",%s,", role, event->participant_id,
                  kind, event->timestamp_ns, state) > 0);
    if (event->kind == SOA_EVENT_ENQUEUE || event->kind == SOA_EVENT_DEQUEUE) {
        const soa_message_t *message = &event->message;
        CHECK(fprintf(file, "%u,%" PRIu64 ",%" PRIu64 ",", message->producer_id,
                      message->sequence, message->enqueue_seq) > 0);
        if (event->kind == SOA_EVENT_DEQUEUE) {
            CHECK(fprintf(file, "%" PRIu64, event->dequeue_seq) > 0);
        }
        CHECK(fprintf(file, ",%" PRIu64 ",%u,", message->enqueue_time_ns,
                      event->occupancy) > 0);
        for (size_t i = 0; i < SOA_PAYLOAD_SIZE; ++i) {
            CHECK(fprintf(file, "%02x", (unsigned)message->payload[i]) == 2);
        }
        CHECK(fputc('\n', file) != EOF);
    } else {
        CHECK(fprintf(file, ",,,,,%u,\n", event->occupancy) > 0);
    }
}

static void worker(const char *name, soa_role_t role, uint32_t id,
                   unsigned count, const char *path)
{
    soa_instance_t *instance = NULL;
    soa_event_t event;
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_participant_register(instance, role, id, &event) == SOA_OK);
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    CHECK(fputs("schema_version,role,participant_id,event,timestamp_ns,state,"
                "producer_id,sequence,enqueue_seq,dequeue_seq,enqueue_time_ns,"
                "occupancy,payload_hex\n", file) >= 0);
    write_event(file, &event);
    for (unsigned i = 1; i <= count; ++i) {
        soa_message_t message = {.producer_id = id, .sequence = i};
        if (role == SOA_ROLE_PRODUCER) {
            fill_payload(&message);
            CHECK(soa_buffer_put(instance, &message, &event) == SOA_OK);
        } else {
            CHECK(soa_buffer_take(instance, &message, &event) == SOA_OK);
        }
        write_event(file, &event);
    }
    CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
    write_event(file, &event);
    CHECK(fclose(file) == 0);
    CHECK(soa_instance_close(&instance) == SOA_OK);
}

static void check_and_remove(const char *name, uint64_t expected)
{
    soa_instance_t *instance = NULL;
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    soa_shared_instance_t *shared = instance->shared;
    CHECK(pthread_mutex_lock(&shared->mutex) == 0);
    CHECK(shared->accepted_count == expected && shared->extracted_count == expected);
    CHECK(shared->occupancy == 0);
    CHECK(shared->active_producers == 0 && shared->active_consumers == 0);
    CHECK(shared->next_enqueue_seq == expected + 1);
    CHECK(shared->next_dequeue_seq == expected + 1);
    CHECK(pthread_mutex_unlock(&shared->mutex) == 0);
    CHECK(soa_instance_close(&instance) == SOA_OK);

    /* Libera la instancia cuando todos los procesos terminaron */
    int fd = shm_open(name, O_RDWR, 0);
    CHECK(fd >= 0);
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    CHECK(fcntl(fd, F_SETLKW, &lock) == 0);
    shared = mmap(NULL, sizeof *shared, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(shared != MAP_FAILED);
    shared->publication = SOA_PUBLICATION_RETIRED;
    CHECK(pthread_cond_destroy(&shared->state_changed) == 0);
    CHECK(pthread_cond_destroy(&shared->not_empty) == 0);
    CHECK(pthread_cond_destroy(&shared->not_full) == 0);
    CHECK(pthread_mutex_destroy(&shared->mutex) == 0);
    CHECK(munmap(shared, sizeof *shared) == 0);
    CHECK(shm_unlink(name) == 0);
    CHECK(close(fd) == 0);
}

static void test_registered_drain(const char *name)
{
    CHECK(soa_instance_create(name, 1) == SOA_OK);
    soa_instance_t *instance = NULL;
    soa_event_t event;
    soa_message_t message = {.producer_id = 1, .sequence = 1};
    fill_payload(&message);
    CHECK(soa_instance_open(name, &instance) == SOA_OK);
    CHECK(soa_buffer_put(instance, &message, &event) == SOA_INVALID_ARGUMENT);
    CHECK(soa_participant_register(instance, SOA_ROLE_PRODUCER, 1, &event) == SOA_OK);
    CHECK(soa_buffer_put(instance, &message, &event) == SOA_OK);
    CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
    CHECK(soa_participant_register(instance, SOA_ROLE_CONSUMER, 1, &event) == SOA_OK);
    CHECK(pthread_mutex_lock(&instance->shared->mutex) == 0);
    instance->shared->state = SOA_STATE_CLOSING;
    CHECK(pthread_mutex_unlock(&instance->shared->mutex) == 0);
    CHECK(soa_buffer_take(instance, &message, &event) == SOA_OK);
    CHECK(event.state == SOA_STATE_CLOSING && event.dequeue_seq == 1);
    CHECK(soa_buffer_take(instance, &message, &event) == SOA_CLOSED);
    CHECK(soa_participant_unregister(instance, &event) == SOA_OK);
    CHECK(soa_buffer_take(instance, &message, &event) == SOA_INVALID_ARGUMENT);
    CHECK(soa_instance_close(&instance) == SOA_OK);
    check_and_remove(name, 1);
    puts("OK drenaje con conexion y registro reales");
}

int main(int argc, char *argv[])
{
    alarm(30);
    if (argc == 7 && strcmp(argv[1], "worker") == 0) {
        worker(argv[2], strcmp(argv[3], "producer") == 0
               ? SOA_ROLE_PRODUCER : SOA_ROLE_CONSUMER,
               (uint32_t)strtoul(argv[4], NULL, 10),
               (unsigned)strtoul(argv[5], NULL, 10), argv[6]);
    } else if (argc == 3 && strcmp(argv[1], "drain") == 0) {
        test_registered_drain(argv[2]);
    } else if (argc == 4 && strcmp(argv[1], "check") == 0) {
        check_and_remove(argv[2], strtoull(argv[3], NULL, 10));
    } else {
        fprintf(stderr, "Argumentos invalidos del programa de pruebas\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
