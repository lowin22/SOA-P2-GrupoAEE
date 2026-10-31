#ifndef SOA_TIMING_H
#define SOA_TIMING_H

#include <stdint.h>

#include "soa/instance.h"
#include "soa/types.h"

/* Espera hasta el plazo monotónico y despierta cuando comienza el cierre */
soa_status_t soa_producer_pause(soa_instance_t *instance,
                                uint64_t monotonic_deadline_ns);

soa_status_t monotonic_time_ns(uint64_t *out);
/* Completa el procesamiento aunque se solicite el cierre */
soa_status_t consumer_process_until(uint64_t deadline_ns);

#endif
