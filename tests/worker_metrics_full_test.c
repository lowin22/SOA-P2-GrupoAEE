#include <stdio.h>

#include "soa/worker_metrics.h"

int main(void)
{
    soa_worker_metrics_writer_t writer = {0};

    /*
     * /dev/full simula un dispositivo sin espacio.
     * El fallo puede detectarse durante la escritura,
     * el vaciado del buffer o el cierre.
     */
    int apertura = soa_worker_metrics_open(&writer, "/dev/full");

    if (apertura == -1) {
        if (writer.file != NULL) {
            if (soa_worker_metrics_close(&writer) != -1) {
                fprintf(stderr,
                        "ERROR: el cierre ocultó el fallo previo\n");
                return 1;
            }
        }

        puts("OK: error de almacenamiento detectado al abrir.");
        return 0;
    }

    const soa_worker_metrics_t metrics = {
        .role = SOA_ROLE_PRODUCER,
        .participant_id = 1,
        .wait_space_ns = 500,
        .wait_data_ns = 0,
        .mutex_acquire_ns = 120,
        .accepted = 10,
        .extracted = 0
    };

    int escritura = soa_worker_metrics_write(&writer, &metrics);
    int cierre = soa_worker_metrics_close(&writer);

    if (escritura == 0 && cierre == 0) {
        fprintf(stderr,
                "ERROR: no se detectó el fallo de almacenamiento\n");
        return 1;
    }

    puts("OK: fallo real de E/S detectado y propagado.");
    return 0;
}
