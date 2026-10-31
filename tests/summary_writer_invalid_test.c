#include <math.h>
#include <stdio.h>

#include "soa/summary_writer.h"

static soa_summary_t valid_summary(void)
{
    soa_summary_t s = {0};

    s.instance_name = "/grupo_aee";
    s.capacity = 8;
    s.accepted = 10;
    s.extracted = 10;
    s.pending = 0;

    s.first_worker_start_ns = 1000;
    s.last_dequeue_ns = 3000;
    s.closing_start_ns = 3500;
    s.release_complete_ns = 4000;

    s.throughput_msg_s = 5000000.0;
    s.latency_mean_ns = 100.0;
    s.latency_p95_ns = 200;
    s.closing_duration_ns = 500;

    s.has_first_worker_start = 1;
    s.has_last_dequeue = 1;
    s.has_throughput = 1;
    s.has_latency_mean = 1;
    s.has_latency_p95 = 1;
    s.has_closing_duration = 1;

    return s;
}

static int reject_case(const char *path, const soa_summary_t *s)
{
    soa_summary_writer_t writer = {0};

    if (soa_summary_writer_open(&writer, path) != 0) {
        return -1;
    }

    if (soa_summary_writer_write(&writer, s) != -1) {
        fprintf(stderr, "ERROR: se aceptó un resumen inválido\n");
        (void)soa_summary_writer_close(&writer);
        return -1;
    }

    /* Tras el fallo, el escritor no debe aceptar más filas. */
    soa_summary_t valid = valid_summary();

    if (soa_summary_writer_write(&writer, &valid) != -1) {
        fprintf(stderr, "ERROR: se permitió escribir tras el fallo\n");
        (void)soa_summary_writer_close(&writer);
        return -1;
    }

    if (soa_summary_writer_close(&writer) != -1) {
        fprintf(stderr, "ERROR: el cierre ocultó el fallo\n");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: summary_writer_invalid_test ARCHIVO.csv\n");
        return 1;
    }

    soa_summary_t s = valid_summary();

    /* Cierre normal con mensajes pendientes. */
    s.extracted = 9;
    s.pending = 1;

    if (reject_case(argv[1], &s) != 0) {
        return 1;
    }

    /* Tiempos de cierre invertidos. */
    s = valid_summary();
    s.release_complete_ns = 3000;

    if (reject_case(argv[1], &s) != 0) {
        return 1;
    }

    /* Throughput inválido. */
    s = valid_summary();
    s.throughput_msg_s = NAN;

    if (reject_case(argv[1], &s) != 0) {
        return 1;
    }

    /* Nombre de instancia incompatible con el contrato. */
    s = valid_summary();
    s.instance_name = "/grupo,incorrecto";

    if (reject_case(argv[1], &s) != 0) {
        return 1;
    }

    /* Duración del cierre incorrecta. */
    s = valid_summary();
    s.closing_duration_ns = 999;

    if (reject_case(argv[1], &s) != 0) {
        return 1;
    }

    puts("OK: cinco casos de resúmenes inválidos rechazados.");
    return 0;
}
