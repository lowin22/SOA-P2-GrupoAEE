#ifndef SOA_INSTANCE_H
#define SOA_INSTANCE_H

#include <stdint.h>

#include "soa/types.h"

typedef struct soa_instance soa_instance_t;

/* Crea y publica de forma exclusiva. No devuelve una conexión abierta. */
soa_status_t soa_instance_create(const char *name, uint32_t capacity);

/* out queda en NULL ante error. El handle conserva mapeo, descriptor y reserva
 * de vida hasta soa_instance_close o soa_shutdown_run. */
soa_status_t soa_instance_open(const char *name, soa_instance_t **out);

/* Requiere que este proceso no permanezca registrado. */
soa_status_t soa_instance_close(soa_instance_t **instance);

#endif
