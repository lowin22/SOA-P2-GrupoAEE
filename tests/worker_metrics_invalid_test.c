#include <stdio.h>

#include "soa/worker_metrics.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: worker_metrics_invalid_test ARCHIVO.csv\n");
        return 1;
    }

    soa_worker_metrics_writer_t writer = {0};

    soa_worker_metrics_t metrics = {
        .role = SOA_ROLE_PRODUCER,
        .participant_id = 0,
        .wait_space_ns = 100,
        .accepted = 5
    };

    if (soa_worker_metrics_open(&writer, argv[1]) != 0) {
        fprintf(stderr, "ERROR: apertura fallida\n");
        return 1;
    }

    /* Caso 1: identificador fuera de rango. */
    if (soa_worker_metrics_write(&writer, &metrics) != -1) {
        fprintf(stderr, "ERROR: se aceptó ID inválido\n");
        (void)soa_worker_metrics_close(&writer);
        return 1;
    }

    /* Caso 2: intentar escribir después del error. */
    metrics.participant_id = 1;

    if (soa_worker_metrics_write(&writer, &metrics) != -1) {
        fprintf(stderr, "ERROR: se permitió escribir tras el fallo\n");
        (void)soa_worker_metrics_close(&writer);
        return 1;
    }

    if (soa_worker_metrics_close(&writer) != -1) {
        fprintf(stderr, "ERROR: el cierre ocultó el fallo\n");
        return 1;
    }

    /* Caso 3: consumidor con inserciones incorrectas. */
    soa_worker_metrics_writer_t segundo = {0};

    metrics.role = SOA_ROLE_CONSUMER;
    metrics.participant_id = 2;
    metrics.accepted = 5;
    metrics.extracted = 3;

    if (soa_worker_metrics_open(&segundo, argv[1]) != 0) {
        return 1;
    }

    if (soa_worker_metrics_write(&segundo, &metrics) != -1) {
        fprintf(stderr, "ERROR: consumidor con inserciones aceptado\n");
        (void)soa_worker_metrics_close(&segundo);
        return 1;
    }

    if (soa_worker_metrics_close(&segundo) != -1) {
        fprintf(stderr, "ERROR: cierre no detectó el fallo\n");
        return 1;
    }

    puts("OK: estadísticas inválidas rechazadas correctamente.");
    return 0;
}
