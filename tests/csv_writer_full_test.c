#include <stdio.h>

#include "soa/csv_writer.h"

int main(void)
{
    soa_csv_writer_t writer = {0};

    /*
     * /dev/full simula un dispositivo sin espacio.
     * Se prueba la detección de errores al vaciar
     * o cerrar el flujo de salida.
     */
    int apertura = soa_csv_writer_open(&writer, "/dev/full");

    if (apertura == -1) {
        /*
         * La apertura puede detectar el fallo
         * al intentar escribir el encabezado.
         */
        if (writer.file != NULL) {
            if (soa_csv_writer_close(&writer) != -1) {
                fprintf(stderr,
                        "ERROR: el cierre ocultó el fallo\n");
                return 1;
            }
        }

        puts("OK: fallo de almacenamiento detectado al abrir.");
        return 0;
    }

    soa_event_t event = {0};

    event.role = SOA_ROLE_PRODUCER;
    event.participant_id = 1;
    event.kind = SOA_EVENT_JOIN;
    event.state = SOA_STATE_OPEN;
    event.timestamp_ns = 100;
    event.occupancy = 0;

    int escritura = soa_csv_writer_write(&writer, &event);
    int cierre = soa_csv_writer_close(&writer);

    if (escritura == 0 && cierre == 0) {
        fprintf(stderr,
                "ERROR: no se detectó el fallo de almacenamiento\n");
        return 1;
    }

    puts("OK: fallo real de E/S detectado y propagado.");
    return 0;
}
