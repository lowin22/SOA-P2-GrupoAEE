#include "soa/worker_metrics.h"

#include <inttypes.h>
#include <stdint.h>

static const char *role_name(soa_role_t role)
{
    switch (role) {
    case SOA_ROLE_PRODUCER:
        return "producer";
    case SOA_ROLE_CONSUMER:
        return "consumer";
    default:
        return NULL;
    }
}

static int valid_metrics(const soa_worker_metrics_t *metrics)
{
    if (metrics == NULL ||
        metrics->participant_id < SOA_MIN_PARTICIPANT_ID ||
        metrics->participant_id > SOA_MAX_PARTICIPANT_ID) {
        return 0;
    }

    if (metrics->role == SOA_ROLE_PRODUCER) {
        return metrics->wait_data_ns == 0 &&
               metrics->extracted == 0;
    }

    if (metrics->role == SOA_ROLE_CONSUMER) {
        return metrics->wait_space_ns == 0 &&
               metrics->accepted == 0;
    }

    return 0;
}

int soa_worker_metrics_open(
    soa_worker_metrics_writer_t *writer,
    const char *path)
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
        "schema_version,role,participant_id,"
        "wait_space_ns,wait_data_ns,mutex_acquire_ns,"
        "accepted,extracted\n",
        file
    ) == EOF) {
        writer->failed = 1;
        return -1;
    }

    return 0;
}

int soa_worker_metrics_write(
    soa_worker_metrics_writer_t *writer,
    const soa_worker_metrics_t *metrics)
{
    if (writer == NULL || writer->file == NULL ||
        writer->failed) {
        return -1;
    }

    if (!valid_metrics(metrics)) {
        writer->failed = 1;
        return -1;
    }

    const char *role = role_name(metrics->role);

    if (role == NULL) {
        writer->failed = 1;
        return -1;
    }

    if (fprintf(
        writer->file,
        "1,%s,%" PRIu32 ",%" PRIu64 ",%" PRIu64
        ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
        role,
        metrics->participant_id,
        metrics->wait_space_ns,
        metrics->wait_data_ns,
        metrics->mutex_acquire_ns,
        metrics->accepted,
        metrics->extracted
    ) < 0) {
        writer->failed = 1;
        return -1;
    }

    return 0;
}

int soa_worker_metrics_close(
    soa_worker_metrics_writer_t *writer)
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
