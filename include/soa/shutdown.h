#ifndef SOA_SHUTDOWN_H
#define SOA_SHUTDOWN_H

#include "soa/instance.h"
#include "soa/types.h"

typedef struct soa_shutdown_snapshot soa_shutdown_snapshot_t;

/* Reclama el cierre, drena, espera retiradas y destruye. En éxito consume y anula
 * *instance; el snapshot es memoria privada válida después de shm_unlink. */
soa_status_t soa_shutdown_run(soa_instance_t **instance,
                              soa_shutdown_snapshot_t **snapshot);

void soa_shutdown_snapshot_free(soa_shutdown_snapshot_t **snapshot);

#endif
