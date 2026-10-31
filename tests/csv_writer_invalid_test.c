#include <stdio.h>

#include "soa/csv_writer.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        return 1;
    }

    soa_csv_writer_t writer = {0};
    soa_event_t event = {0};

    event.role = SOA_ROLE_PRODUCER;
    event.participant_id = 1;
    event.kind = SOA_EVENT_ENQUEUE;
    event.state = SOA_STATE_OPEN;
    event.timestamp_ns = 100;
    event.occupancy = 1;

    /* Mensaje inválido: sequence y enqueue_seq son cero. */
    event.message.producer_id = 1;

    if (soa_csv_writer_open(&writer, argv[1]) != 0) {
        return 1;
    }

    int resultado = soa_csv_writer_write(&writer, &event);

    soa_csv_writer_close(&writer);

    if (resultado != -1) {
        fprintf(stderr, "ERROR: se aceptó un evento inválido\n");
        return 1;
    }

    puts("OK: el evento inválido fue rechazado");
    return 0;
}
