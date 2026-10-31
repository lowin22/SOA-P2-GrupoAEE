#include <stdio.h>

#include "soa/summary_writer.h"

int main(void)
{
    soa_summary_writer_t writer = {0};

    int apertura = soa_summary_writer_open(&writer, "/dev/full");

    if (apertura != 0) {
        if (writer.file != NULL) {
            if (soa_summary_writer_close(&writer) != -1) {
                fprintf(stderr, "ERROR: cierre ocultó el fallo\n");
                return 1;
            }
        }

        puts("OK: error detectado durante apertura.");
        return 0;
    }

    const soa_summary_t summary = {
        .instance_name = "/grupo_aee",
        .capacity = 8,
        .accepted = 0,
        .extracted = 0,
        .pending = 0,
        .closing_start_ns = 1000,
        .release_complete_ns = 1500,
        .closing_duration_ns = 500,
        .has_closing_duration = 1
    };

    int escritura = soa_summary_writer_write(&writer, &summary);
    int cierre = soa_summary_writer_close(&writer);

    if (escritura == 0 && cierre == 0) {
        fprintf(stderr,
                "ERROR: no se detectó el fallo de almacenamiento\n");
        return 1;
    }

    puts("OK: fallo real de E/S detectado y propagado.");
    return 0;
}
