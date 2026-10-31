#ifndef SOA_SUMMARY_WRITER_H
#define SOA_SUMMARY_WRITER_H

#include <stdint.h>
#include <stdio.h>

/*
 * Resumen privado de una instancia cerrada normalmente.
 *
 * Los valores deben capturarse o calcularse antes de
 * generar el CSV y permanecer disponibles después
 * de liberar la memoria compartida.
 *
 * Los indicadores has_* distinguen un valor válido
 * de una métrica que no se puede calcular.
 */
typedef struct {
    const char *instance_name;
    uint32_t capacity;

    uint64_t accepted;
    uint64_t extracted;
    uint64_t pending;

    uint64_t first_worker_start_ns;
    uint64_t last_dequeue_ns;
    uint64_t closing_start_ns;
    uint64_t release_complete_ns;

    double throughput_msg_s;
    double latency_mean_ns;
    uint64_t latency_p95_ns;
    uint64_t closing_duration_ns;

    int has_first_worker_start;
    int has_last_dequeue;
    int has_throughput;
    int has_latency_mean;
    int has_latency_p95;
    int has_closing_duration;
} soa_summary_t;

/*
 * Escritor CSV del resumen final.
 * No adquiere mutex ni accede a memoria compartida.
 */
typedef struct {
    FILE *file;
    int failed;
} soa_summary_writer_t;

/* Abre el archivo y escribe las 14 columnas. */
int soa_summary_writer_open(
    soa_summary_writer_t *writer,
    const char *path
);

/* Escribe una fila con los datos del resumen. */
int soa_summary_writer_write(
    soa_summary_writer_t *writer,
    const soa_summary_t *summary
);

/* Cierra el archivo y comprueba errores pendientes. */
int soa_summary_writer_close(
    soa_summary_writer_t *writer
);

#endif
