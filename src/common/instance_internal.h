#ifndef SOA_INSTANCE_INTERNAL_H
#define SOA_INSTANCE_INTERNAL_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "soa/instance.h"
#include "soa/message.h"
#include "soa/types.h"

#define SOA_INSTANCE_MAGIC UINT64_C(0x534f41503253484d)
#define SOA_INSTANCE_ABI_VERSION UINT32_C(1)
#define SOA_ID_BITMAP_WORDS ((SOA_MAX_PARTICIPANT_ID + 64U) / 64U)

typedef enum {
    SOA_PUBLICATION_INITIALIZING = 0,
    SOA_PUBLICATION_READY = 1,
    SOA_PUBLICATION_RETIRED = 2
} soa_publication_t;

typedef struct {
    uint64_t mutex_wait_ns;
    uint64_t condition_wait_ns;
    uint64_t operations;
    uint8_t active;
    uint8_t reserved[7];
} soa_participant_metrics_t;

typedef struct {
    uint64_t magic;
    uint64_t mapped_size;
    uint32_t abi_version;
    uint32_t capacity;
    uint32_t publication;
    uint32_t state;

    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    pthread_cond_t state_changed;

    uint32_t finisher_claimed;
    uint32_t head;
    uint32_t tail;
    uint32_t occupancy;
    uint32_t active_producers;
    uint32_t active_consumers;
    uint32_t reserved_scalar[2];

    uint64_t next_enqueue_seq;
    uint64_t next_dequeue_seq;
    uint64_t accepted_count;
    uint64_t extracted_count;
    uint64_t first_worker_start_ns;
    uint64_t last_dequeue_ns;
    uint64_t closing_start_ns;

    uint64_t used_producer_ids[SOA_ID_BITMAP_WORDS];
    uint64_t used_consumer_ids[SOA_ID_BITMAP_WORDS];
    soa_participant_metrics_t producer_metrics[SOA_MAX_PARTICIPANT_ID + 1U];
    soa_participant_metrics_t consumer_metrics[SOA_MAX_PARTICIPANT_ID + 1U];

    soa_message_t messages[];
} soa_shared_instance_t;

struct soa_instance {
    int fd;
    bool read_lock_held;
    bool registered;
    soa_role_t role;
    uint32_t participant_id;
    size_t mapped_size;
    soa_shared_instance_t *shared;
};

#endif
