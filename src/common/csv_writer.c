#include "soa/csv_writer.h"

#include <inttypes.h>
#include <stdint.h>

static const char *role_name(soa_role_t role)
{
    switch (role) {
    case SOA_ROLE_PRODUCER: return "producer";
    case SOA_ROLE_CONSUMER: return "consumer";
    default: return NULL;
    }
}

static const char *state_name(soa_state_t state)
{
    switch (state) {
    case SOA_STATE_OPEN: return "OPEN";
    case SOA_STATE_CLOSING: return "CLOSING";
    case SOA_STATE_CLOSED: return "CLOSED";
    default: return NULL;
    }
}

static const char *event_name(soa_event_kind_t kind)
{
    switch (kind) {
    case SOA_EVENT_JOIN: return "JOIN";
    case SOA_EVENT_ENQUEUE: return "ENQUEUE";
    case SOA_EVENT_DEQUEUE: return "DEQUEUE";
    case SOA_EVENT_LEAVE: return "LEAVE";
    default: return NULL;
    }
}

int soa_csv_writer_open(soa_csv_writer_t *writer, const char *path)
{
    if (writer == NULL || path == NULL) {
        return -1;
    }

    writer->file = NULL;
    writer->failed = 0;

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        writer->failed = 1;
        return -1;
    }

    writer->file = file;

    if (fputs(
        "schema_version,role,participant_id,event,timestamp_ns,"
        "state,producer_id,sequence,enqueue_seq,dequeue_seq,"
        "enqueue_time_ns,occupancy,payload_hex\n",
        file
    ) == EOF) {
        writer->failed = 1;
        return -1;
    }

    return 0;
}

int soa_csv_writer_write(soa_csv_writer_t *writer,
                         const soa_event_t *event)
{
    if (writer == NULL || event == NULL ||
        writer->file == NULL || writer->failed) {
        return -1;
    }

    const char *role = role_name(event->role);
    const char *state = state_name(event->state);
    const char *kind = event_name(event->kind);

    if (role == NULL || state == NULL || kind == NULL ||
        event->participant_id < SOA_MIN_PARTICIPANT_ID ||
        event->participant_id > SOA_MAX_PARTICIPANT_ID ||
        event->occupancy > SOA_MAX_CAPACITY) {
        return -1;
    }

    /*
     * Validar el mensaje completo antes de escribir cualquier campo.
     * Así evitamos filas parciales por errores de validación.
     */
    if (event->kind == SOA_EVENT_ENQUEUE ||
        event->kind == SOA_EVENT_DEQUEUE) {

        const soa_message_t *message = &event->message;

        if (message->producer_id < SOA_MIN_PARTICIPANT_ID ||
            message->producer_id > SOA_MAX_PARTICIPANT_ID ||
            message->sequence == 0 ||
            message->enqueue_seq == 0 ||
            message->enqueue_time_ns > event->timestamp_ns ||
            (event->kind == SOA_EVENT_ENQUEUE &&
             event->role != SOA_ROLE_PRODUCER) ||
            (event->kind == SOA_EVENT_DEQUEUE &&
             (event->role != SOA_ROLE_CONSUMER ||
              event->dequeue_seq == 0))) {
            writer->failed = 1;
            return -1;
        }
    }

    FILE *file = writer->file;

    if (fprintf(
        file, "1,%s,%" PRIu32 ",%s,%" PRIu64 ",%s,",
        role, event->participant_id, kind,
        event->timestamp_ns, state
    ) < 0) {
        writer->failed = 1;
        return -1;
    }

    if (event->kind == SOA_EVENT_ENQUEUE ||
        event->kind == SOA_EVENT_DEQUEUE) {

        const soa_message_t *message = &event->message;

        if (fprintf(
            file, "%" PRIu32 ",%" PRIu64 ",%" PRIu64 ",",
            message->producer_id,
            message->sequence,
            message->enqueue_seq
        ) < 0) {
            writer->failed = 1;
            return -1;
        }

        if (event->kind == SOA_EVENT_DEQUEUE) {
            if (fprintf(file, "%" PRIu64, event->dequeue_seq) < 0) {
                writer->failed = 1;
                return -1;
            }
        }

        if (fprintf(
            file, ",%" PRIu64 ",%" PRIu32 ",",
            message->enqueue_time_ns,
            event->occupancy
        ) < 0) {
            writer->failed = 1;
            return -1;
        }

        static const char hex[] = "0123456789abcdef";

        for (size_t i = 0; i < SOA_PAYLOAD_SIZE; ++i) {
            unsigned char byte = message->payload[i];

            if (fputc(hex[byte >> 4], file) == EOF ||
                fputc(hex[byte & 15], file) == EOF) {
                writer->failed = 1;
                return -1;
            }
        }

        if (fputc('\n', file) == EOF) {
            writer->failed = 1;
            return -1;
        }

    } else {
        if (fprintf(file, ",,,,,%" PRIu32 ",\n",
                    event->occupancy) < 0) {
            writer->failed = 1;
            return -1;
        }
    }

    return 0;
}

int soa_csv_writer_close(soa_csv_writer_t *writer)
{
    if (writer == NULL || writer->file == NULL) {
        return -1;
    }

    int failed = writer->failed;
    FILE *file = writer->file;

    writer->file = NULL;

    if (fflush(file) != 0) {
        failed = 1;
    }

    if (fclose(file) != 0) {
        failed = 1;
    }

    writer->failed = failed;

    return failed ? -1 : 0;
}
