#ifndef INSTANCE_INTERNAL_H
#define INSTANCE_INTERNAL_H

#include <stdbool.h>
#include <pthread.h>

#include "soa/instance.h"
#include "soa/message.h"

/* Representacion del buffer que debera integrar el modulo de memoria */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    pthread_cond_t state_changed;
    soa_state_t state;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t occupancy;
    uint64_t next_enqueue_seq;
    uint64_t next_dequeue_seq;
    uint64_t accepted_count;
    uint64_t extracted_count;
    uint64_t last_dequeue_ns;
    soa_message_t messages[];
} shared_buffer_t;

/* El registro y la reserva de vida pertenecen al modulo de participantes */
struct soa_instance {
    shared_buffer_t *shared;
    soa_role_t role;
    uint32_t id;
    bool registered;
};

#endif
