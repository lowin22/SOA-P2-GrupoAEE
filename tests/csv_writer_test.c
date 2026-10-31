#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "soa/csv_writer.h"

static const unsigned char payload[SOA_PAYLOAD_SIZE] = {
    0x6f, 0x95, 0x59, 0xfd, 0xa6, 0x91, 0x43, 0x20,
    0x11, 0x38, 0xfc, 0xe9, 0xa8, 0xcb, 0xea, 0x31,
    0xa8, 0x37, 0xbd, 0x4c, 0xf6, 0x73, 0x15, 0xdd,
    0xb7, 0x2a, 0xa7, 0x50, 0x6a, 0xb7, 0x08, 0xf1
};

static int generar_csv(const char *ruta, soa_role_t rol)
{
    soa_csv_writer_t writer = {0};
    soa_event_t event = {0};

    event.role = rol;
    event.participant_id = 1;
    event.state = SOA_STATE_OPEN;

    if (soa_csv_writer_open(&writer, ruta) != 0) {
        return -1;
    }

    /* Registro del participante. */
    event.kind = SOA_EVENT_JOIN;
    event.timestamp_ns = rol == SOA_ROLE_PRODUCER ? 90 : 95;
    event.occupancy = rol == SOA_ROLE_PRODUCER ? 0 : 1;

    if (soa_csv_writer_write(&writer, &event) != 0) {
        soa_csv_writer_close(&writer);
        return -1;
    }

    /* Mensaje compartido entre inserción y extracción. */
    event.message.producer_id = 1;
    event.message.sequence = 1;
    event.message.enqueue_seq = 1;
    event.message.enqueue_time_ns = 100;
    memcpy(event.message.payload, payload, sizeof(payload));

    if (rol == SOA_ROLE_PRODUCER) {
        event.kind = SOA_EVENT_ENQUEUE;
        event.timestamp_ns = 100;
        event.occupancy = 1;
    } else {
        event.kind = SOA_EVENT_DEQUEUE;
        event.timestamp_ns = 150;
        event.dequeue_seq = 1;
        event.occupancy = 0;
    }

    if (soa_csv_writer_write(&writer, &event) != 0) {
        soa_csv_writer_close(&writer);
        return -1;
    }

    /* Retirada del participante. */
    event.kind = SOA_EVENT_LEAVE;
    event.timestamp_ns = rol == SOA_ROLE_PRODUCER ? 110 : 160;

    if (soa_csv_writer_write(&writer, &event) != 0) {
        soa_csv_writer_close(&writer);
        return -1;
    }

    return soa_csv_writer_close(&writer);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr,
                "Uso: csv_writer_test PRODUCTOR.csv CONSUMIDOR.csv\n");
        return 1;
    }

    if (generar_csv(argv[1], SOA_ROLE_PRODUCER) != 0 ||
        generar_csv(argv[2], SOA_ROLE_CONSUMER) != 0) {
        fprintf(stderr, "ERROR: escritura CSV fallida\n");
        return 1;
    }

    puts("OK: CSV de productor y consumidor generados.");
    return 0;
}
