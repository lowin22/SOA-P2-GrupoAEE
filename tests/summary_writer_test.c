#include <stdio.h>

#include "soa/summary_writer.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: summary_writer_test ARCHIVO.csv\n");
        return 1;
    }

    soa_summary_writer_t writer = {0};

    const soa_summary_t summary = {
        .instance_name = "/grupo_aee",
        .capacity = 8,
        .accepted = 10,
        .extracted = 10,
        .pending = 0,

        .first_worker_start_ns = 1000000000ULL,
        .last_dequeue_ns = 3000000000ULL,
        .closing_start_ns = 3500000000ULL,
        .release_complete_ns = 4000000000ULL,

        .throughput_msg_s = 5.0,
        .latency_mean_ns = 1500000.0,
        .latency_p95_ns = 2500000,
        .closing_duration_ns = 500000000ULL,

        .has_first_worker_start = 1,
        .has_last_dequeue = 1,
        .has_throughput = 1,
        .has_latency_mean = 1,
        .has_latency_p95 = 1,
        .has_closing_duration = 1
    };

    if (soa_summary_writer_open(&writer, argv[1]) != 0) {
        fprintf(stderr, "ERROR: apertura del CSV\n");
        return 1;
    }

    if (soa_summary_writer_write(&writer, &summary) != 0) {
        fprintf(stderr, "ERROR: escritura del resumen\n");
        (void)soa_summary_writer_close(&writer);
        return 1;
    }

    if (soa_summary_writer_close(&writer) != 0) {
        fprintf(stderr, "ERROR: cierre del CSV\n");
        return 1;
    }

    puts("OK: resumen final exportado correctamente.");
    return 0;
}
