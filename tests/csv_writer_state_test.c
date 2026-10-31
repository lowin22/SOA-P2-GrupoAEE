#include <stdio.h>

#include "soa/csv_writer.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: csv_writer_state_test ARCHIVO.csv\n");
        return 1;
    }

    soa_csv_writer_t writer = {0};
    soa_event_t event = {0};

    if (soa_csv_writer_open(&writer, argv[1]) != 0) {
        fprintf(stderr, "ERROR: apertura fallida\n");
        return 1;
    }

    /* Primer evento: inválido porque no tiene secuencia. */
    event.role = SOA_ROLE_PRODUCER;
    event.participant_id = 1;
    event.kind = SOA_EVENT_ENQUEUE;
    event.state = SOA_STATE_OPEN;
    event.timestamp_ns = 100;
    event.occupancy = 1;
    event.message.producer_id = 1;

    if (soa_csv_writer_write(&writer, &event) != -1) {
        fprintf(stderr, "ERROR: se aceptó el evento inválido\n");
        soa_csv_writer_close(&writer);
        return 1;
    }

    if (writer.failed != 1) {
        fprintf(stderr, "ERROR: no se registró el fallo\n");
        soa_csv_writer_close(&writer);
        return 1;
    }

    /* Segundo evento: válido, pero no debe escribirse tras el fallo. */
    event.kind = SOA_EVENT_JOIN;
    event.timestamp_ns = 110;
    event.occupancy = 0;

    if (soa_csv_writer_write(&writer, &event) != -1) {
        fprintf(stderr, "ERROR: se permitió escribir tras el fallo\n");
        soa_csv_writer_close(&writer);
        return 1;
    }

    /* El cierre debe informar que el escritor sufrió un error. */
    if (soa_csv_writer_close(&writer) != -1) {
        fprintf(stderr, "ERROR: el cierre ocultó el fallo previo\n");
        return 1;
    }

    puts("OK: el escritor bloquea nuevas escrituras tras un error.");
    return 0;
}
