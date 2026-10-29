#ifndef SOA_CSV_WRITER_H
#define SOA_CSV_WRITER_H

#include <stdio.h>

#include "soa/events.h"

/*
 * Escritor CSV v1 para eventos de productores y consumidores.
 *
 * La aplicación debe invocar soa_csv_writer_write() fuera
 * de la región crítica del buffer.
 *
 * Cada instancia del escritor corresponde a un archivo CSV
 * de un único participante.
 */
typedef struct {
    FILE *file;
    int failed;
} soa_csv_writer_t;

/*
 * Abre un archivo nuevo y escribe el encabezado CSV.
 *
 * Retorna 0 si tiene éxito y -1 en caso de error.
 * El directorio de destino debe existir.
 */
int soa_csv_writer_open(soa_csv_writer_t *writer, const char *path);

/*
 * Escribe un evento previamente capturado.
 *
 * Retorna 0 si tiene éxito y -1 en caso de error.
 */
int soa_csv_writer_write(soa_csv_writer_t *writer,
                         const soa_event_t *event);

/*
 * Cierra el archivo y comprueba errores de escritura.
 *
 * Retorna 0 si tiene éxito y -1 en caso de error.
 */
int soa_csv_writer_close(soa_csv_writer_t *writer);

#endif
