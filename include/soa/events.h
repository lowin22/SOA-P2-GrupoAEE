#ifndef SOA_EVENTS_H
#define SOA_EVENTS_H

#include <stdint.h>

#include "soa/message.h"
#include "soa/types.h"

/* Instantánea privada capturada dentro de una operación protegida. Es seguro
 * escribirla a CSV después de que la operación haya liberado el mutex. */
typedef struct {
    soa_role_t role;
    uint32_t participant_id;
    soa_event_kind_t kind;
    soa_state_t state;
    uint64_t timestamp_ns;
    uint64_t dequeue_seq;
    uint32_t occupancy;
    soa_message_t message;
} soa_event_t;

#endif
