#ifndef SOA_PARTICIPANTS_H
#define SOA_PARTICIPANTS_H

#include <stdint.h>

#include "soa/events.h"
#include "soa/instance.h"
#include "soa/types.h"

/* Estas operaciones adquieren internamente el mutex compartido. */
soa_status_t soa_participant_register(soa_instance_t *instance, soa_role_t role,
                                      uint32_t id, soa_event_t *event);
soa_status_t soa_participant_unregister(soa_instance_t *instance,
                                        soa_event_t *event);

#endif
