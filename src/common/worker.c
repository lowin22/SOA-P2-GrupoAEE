#include "soa/worker.h"
#include "soa/buffer.h"
#include "soa/csv_writer.h"
#include "soa/participants.h"
#include "soa/random.h"
#include "soa/timing.h"

#include <stdio.h>
#include <stdlib.h>

static soa_status_t worker_pause(soa_instance_t *instance, random_state_t *random,
                                 double mean_ms, soa_role_t role)
{
    uint64_t delay = random_delay_ns(random, mean_ms);
    uint64_t now;
    soa_status_t status = monotonic_time_ns(&now);
    if (status != SOA_OK) {
        return status;
    }
    if (delay > UINT64_MAX - now) {
        return SOA_OVERFLOW;
    }
    if (role == SOA_ROLE_PRODUCER) {
        return soa_producer_pause(instance, now + delay);
    }
    return delay == 0 ? SOA_OK : consumer_process_until(now + delay);
}

int worker_run(const options_t *options, soa_role_t role)
{
    soa_instance_t *instance = NULL;
    soa_csv_writer_t writer = {0};
    soa_event_t event;
    random_state_t random = {.state = options->seed};
    soa_status_t status = soa_instance_open(options->name, &instance);
    int failed = 0;
    if (status != SOA_OK) {
        fprintf(stderr, "No se pudo abrir la instancia %s\n", options->name);
        failed = 1;
        goto cleanup;
    }
    if (soa_csv_writer_open(&writer, options->log_path) != 0) {
        fprintf(stderr, "No se pudo abrir el log %s\n", options->log_path);
        status = SOA_SYSTEM_ERROR;
        goto cleanup;
    }
    status = soa_participant_register(instance, role, options->id, &event);
    if (status != SOA_OK) {
        fprintf(stderr, "No se pudo registrar el ID %u\n", options->id);
        failed = 1;
        goto cleanup;
    }
    if (soa_csv_writer_write(&writer, &event) != 0) {
        status = SOA_SYSTEM_ERROR;
        goto retire;
    }

    uint64_t sequence = 1;
    for (;;) {
        soa_message_t message = {0};
        if (role == SOA_ROLE_PRODUCER) {
            status = worker_pause(instance, &random, options->mean_ms, role);
            if (status != SOA_OK) {
                break;
            }
            message.producer_id = options->id;
            message.sequence = sequence;
            soa_message_fill_payload(&message);
            status = soa_buffer_put(instance, &message, &event);
        } else {
            status = soa_buffer_take(instance, &message, &event);
        }
        if (status != SOA_OK) {
            break;
        }

        /* El consumidor termina el procesamiento aunque falle su log */
        int log_result = soa_csv_writer_write(&writer, &event);
        if (role == SOA_ROLE_CONSUMER) {
            soa_status_t processing = worker_pause(instance, &random, options->mean_ms, role);
            if (!soa_message_payload_is_valid(&message)) {
                status = SOA_INCOMPATIBLE_INSTANCE;
            } else {
                status = processing;
            }
        }
        if (log_result != 0) {
            status = SOA_SYSTEM_ERROR;
        }
        if (status != SOA_OK) {
            break;
        }
        if (role == SOA_ROLE_PRODUCER) {
            if (options->has_message_limit && sequence == options->messages) {
                break;
            }
            if (sequence == UINT64_MAX) {
                status = SOA_OVERFLOW;
                break;
            }
            ++sequence;
        }
    }

retire:
    if (status != SOA_OK && status != SOA_CLOSED) {
        failed = 1;
    }
    /* Confirma los datos antes de publicar la retirada */
    if (fflush(writer.file) != 0) {
        failed = 1;
    }
    soa_status_t retired = soa_participant_unregister(instance, &event);
    if (retired != SOA_OK) {
        status = retired;
        failed = 1;
    } else if (soa_csv_writer_write(&writer, &event) != 0) {
        failed = 1;
    }

cleanup:
    if (writer.file != NULL && soa_csv_writer_close(&writer) != 0) {
        failed = 1;
    }
    if (instance != NULL && soa_instance_close(&instance) != SOA_OK) {
        failed = 1;
    }
    if (failed || (status != SOA_OK && status != SOA_CLOSED)) {
        if (status == SOA_OK || status == SOA_CLOSED) {
            status = SOA_SYSTEM_ERROR;
        }
        fprintf(stderr, "No se pudo completar el trabajador, codigo %d\n", status);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
