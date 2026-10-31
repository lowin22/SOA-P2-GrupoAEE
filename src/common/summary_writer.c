#include "soa/summary_writer.h"

#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "soa/types.h"

static int valid_name(const char *name)
{
    if (name == NULL) {
        return 0;
    }

    size_t length = strnlen(name, 256);

    if (length < 2 || length > 255 || name[0] != '/') {
        return 0;
    }

    for (size_t i = 1; i < length; ++i) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return 0;
        }
    }

    return 1;
}

static int valid_summary(const soa_summary_t *s)
{
    if (s == NULL || !valid_name(s->instance_name)) {
        return 0;
    }

    if (s->capacity < SOA_MIN_CAPACITY ||
        s->capacity > SOA_MAX_CAPACITY ||
        s->extracted > s->accepted ||
        s->pending != s->accepted - s->extracted) {
        return 0;
    }

    /* El resumen corresponde a un cierre normal. */
    if (s->pending != 0 || s->accepted != s->extracted) {
        return 0;
    }

    if (s->closing_start_ns == 0 ||
        s->release_complete_ns < s->closing_start_ns) {
        return 0;
    }

    if (s->has_first_worker_start &&
        s->first_worker_start_ns == 0) {
        return 0;
    }

    if (s->has_last_dequeue &&
        (!s->has_first_worker_start ||
         s->last_dequeue_ns < s->first_worker_start_ns)) {
        return 0;
    }

    if (s->extracted == 0) {
        if (s->has_last_dequeue || s->has_throughput ||
            s->has_latency_mean || s->has_latency_p95) {
            return 0;
        }
    } else {
        /* Los indicadores permiten declarar un par de latencias no disponible
         * sin inventar valores; si existe medición, media y p95 son atómicos. */
        if (!s->has_first_worker_start ||
            !s->has_last_dequeue ||
            s->has_latency_mean != s->has_latency_p95) {
            return 0;
        }
    }

    if (s->extracted > 0) {
        int intervalo_positivo =
            s->last_dequeue_ns > s->first_worker_start_ns;

        if (s->has_throughput != intervalo_positivo) {
            return 0;
        }

        if (intervalo_positivo) {
            double intervalo_ns =
                (double)(s->last_dequeue_ns -
                         s->first_worker_start_ns);

            double esperado =
                (double)s->extracted * 1000000000.0 / intervalo_ns;

            if (!isfinite(s->throughput_msg_s) ||
                s->throughput_msg_s < 0.0 ||
                fabs(s->throughput_msg_s - esperado) >
                    fmax(1e-9, fabs(esperado) * 1e-9)) {
                return 0;
            }
        }
    }

    if (s->has_latency_mean &&
        (!isfinite(s->latency_mean_ns) ||
         s->latency_mean_ns < 0.0)) {
        return 0;
    }

    if (s->has_closing_duration &&
        s->closing_duration_ns !=
            s->release_complete_ns - s->closing_start_ns) {
        return 0;
    }

    return 1;
}

int soa_summary_writer_open(
    soa_summary_writer_t *writer,
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
        "schema_version,instance_name,capacity,accepted,"
        "extracted,pending,first_worker_start_ns,last_dequeue_ns,"
        "closing_start_ns,release_complete_ns,throughput_msg_s,"
        "latency_mean_ns,latency_p95_ns,closing_duration_ns\n",
        file
    ) == EOF) {
        writer->failed = 1;
        return -1;
    }

    return 0;
}

int soa_summary_writer_write(
    soa_summary_writer_t *writer,
    const soa_summary_t *s)
{
    if (writer == NULL || writer->file == NULL ||
        writer->failed) {
        return -1;
    }

    if (!valid_summary(s)) {
        writer->failed = 1;
        return -1;
    }

    FILE *file = writer->file;

    if (fprintf(
        file,
        "1,%s,%" PRIu32 ",%" PRIu64 ",%" PRIu64
        ",%" PRIu64 ",",
        s->instance_name,
        s->capacity,
        s->accepted,
        s->extracted,
        s->pending
    ) < 0) {
        goto error;
    }

    if (s->has_first_worker_start &&
        fprintf(file, "%" PRIu64,
                s->first_worker_start_ns) < 0) {
        goto error;
    }

    if (fputc(',', file) == EOF) {
        goto error;
    }

    if (s->has_last_dequeue &&
        fprintf(file, "%" PRIu64,
                s->last_dequeue_ns) < 0) {
        goto error;
    }

    if (fprintf(
        file, ",%" PRIu64 ",%" PRIu64 ",",
        s->closing_start_ns,
        s->release_complete_ns
    ) < 0) {
        goto error;
    }

    if (s->has_throughput &&
        fprintf(file, "%.9g", s->throughput_msg_s) < 0) {
        goto error;
    }

    if (fputc(',', file) == EOF) {
        goto error;
    }

    if (s->has_latency_mean &&
        fprintf(file, "%.12g", s->latency_mean_ns) < 0) {
        goto error;
    }

    if (fputc(',', file) == EOF) {
        goto error;
    }

    if (s->has_latency_p95 &&
        fprintf(file, "%" PRIu64, s->latency_p95_ns) < 0) {
        goto error;
    }

    if (fputc(',', file) == EOF) {
        goto error;
    }

    if (s->has_closing_duration &&
        fprintf(file, "%" PRIu64,
                s->closing_duration_ns) < 0) {
        goto error;
    }

    if (fputc('\n', file) == EOF) {
        goto error;
    }

    return 0;

error:
    writer->failed = 1;
    return -1;
}

int soa_summary_writer_close(
    soa_summary_writer_t *writer)
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
