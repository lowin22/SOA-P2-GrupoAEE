#ifndef SOA_TIMING_H
#define SOA_TIMING_H

#include <stdint.h>

#include "soa/instance.h"
#include "soa/types.h"

/* Espera hasta el plazo monotónico absoluto o retorna SOA_CLOSED al comenzar el
 * cierre. Está destinada a la pausa previa del productor. */
soa_status_t soa_producer_pause(soa_instance_t *instance,
                                uint64_t monotonic_deadline_ns);

#endif
