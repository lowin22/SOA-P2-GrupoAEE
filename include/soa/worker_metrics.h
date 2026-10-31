#ifndef SOA_WORKER_METRICS_H
#define SOA_WORKER_METRICS_H

#include <stdint.h>
#include <stdio.h>

#include "soa/types.h"

/*
 * Estadísticas históricas de un participante.
 *
 * Esta estructura representa una copia privada de las
 * métricas, no una referencia a memoria compartida.
 *
 * Los contadores corresponden únicamente a operaciones
 * exitosas del participante.
 */
typedef struct {
    soa_role_t role;
    uint32_t participant_id;

    uint64_t wait_space_ns;
    uint64_t wait_data_ns;
    uint64_t mutex_acquire_ns;

    uint64_t accepted;
    uint64_t extracted;
} soa_worker_metrics_t;

/*
 * Escritor del archivo worker_metrics.csv.
 *
 * La aplicación debe utilizarlo fuera de las
 * regiones críticas de memoria compartida.
 */
typedef struct {
    FILE *file;
    int failed;
} soa_worker_metrics_writer_t;

/*
 * Abre el CSV y escribe su encabezado.
 * Retorna 0 en éxito y -1 ante un error.
 */
int soa_worker_metrics_open(
    soa_worker_metrics_writer_t *writer,
    const char *path
);

/*
 * Escribe las estadísticas de un participante.
 * Retorna 0 en éxito y -1 ante un error.
 */
int soa_worker_metrics_write(
    soa_worker_metrics_writer_t *writer,
    const soa_worker_metrics_t *metrics
);

/*
 * Cierra el archivo y comprueba errores pendientes.
 * Retorna 0 en éxito y -1 ante un error.
 */
int soa_worker_metrics_close(
    soa_worker_metrics_writer_t *writer
);

#endif
