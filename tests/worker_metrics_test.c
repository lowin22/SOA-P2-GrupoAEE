#include <stdio.h>

#include "soa/worker_metrics.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: worker_metrics_test ARCHIVO.csv\n");
        return 1;
    }

    soa_worker_metrics_writer_t writer = {0};

    const soa_worker_metrics_t productor = {
        .role = SOA_ROLE_PRODUCER,
        .participant_id = 1,
        .wait_space_ns = 500,
        .wait_data_ns = 0,
        .mutex_acquire_ns = 120,
        .accepted = 10,
        .extracted = 0
    };

    const soa_worker_metrics_t consumidor = {
        .role = SOA_ROLE_CONSUMER,
        .participant_id = 2,
        .wait_space_ns = 0,
        .wait_data_ns = 800,
        .mutex_acquire_ns = 150,
        .accepted = 0,
        .extracted = 10
    };

    if (soa_worker_metrics_open(&writer, argv[1]) != 0) {
        fprintf(stderr, "ERROR: apertura del CSV\n");
        return 1;
    }

    if (soa_worker_metrics_write(&writer, &productor) != 0 ||
        soa_worker_metrics_write(&writer, &consumidor) != 0) {
        fprintf(stderr, "ERROR: escritura de estadísticas\n");
        (void)soa_worker_metrics_close(&writer);
        return 1;
    }

    if (soa_worker_metrics_close(&writer) != 0) {
        fprintf(stderr, "ERROR: cierre del CSV\n");
        return 1;
    }

    puts("OK: estadísticas históricas exportadas.");
    return 0;
}
