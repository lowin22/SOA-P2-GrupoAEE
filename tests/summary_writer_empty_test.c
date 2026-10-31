#include <stdio.h>

#include "soa/summary_writer.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: summary_writer_empty_test ARCHIVO.csv\n");
        return 1;
    }

    soa_summary_writer_t writer = {0};

    const soa_summary_t summary = {
        .instance_name = "/grupo_vacio",
        .capacity = 8,

        .accepted = 0,
        .extracted = 0,
        .pending = 0,

        .closing_start_ns = 1000000000ULL,
        .release_complete_ns = 1500000000ULL,
        .closing_duration_ns = 500000000ULL,

        .has_first_worker_start = 0,
        .has_last_dequeue = 0,
        .has_throughput = 0,
        .has_latency_mean = 0,
        .has_latency_p95 = 0,
        .has_closing_duration = 1
    };

    if (soa_summary_writer_open(&writer, argv[1]) != 0) {
        return 1;
    }

    if (soa_summary_writer_write(&writer, &summary) != 0) {
        (void)soa_summary_writer_close(&writer);
        return 1;
    }

    if (soa_summary_writer_close(&writer) != 0) {
        return 1;
    }

    puts("OK: resumen sin mensajes generado.");
    return 0;
}
