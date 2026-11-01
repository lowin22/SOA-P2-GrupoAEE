#include "soa/instance.h"
#include "../src/common/instance_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc != 3 && argc != 4) {
        return 1;
    }
    bool wait_producers = strcmp(argv[1], "wait-producers") == 0;
    bool wait_consumers = strcmp(argv[1], "wait-consumers") == 0;
    unsigned long expected = 0;
    if (wait_producers || wait_consumers) {
        char *end;
        if (argc != 4) {
            return 1;
        }
        expected = strtoul(argv[3], &end, 10);
        if (*end != '\0' || expected == 0 || expected > SOA_MAX_PARTICIPANT_ID) {
            return 1;
        }
    } else if (argc != 3) {
        return 1;
    }
    soa_instance_t *instance = NULL;
    if (soa_instance_open(argv[2], &instance) != SOA_OK) {
        return 1;
    }
    soa_shared_instance_t *shared = instance->shared;
    if (pthread_mutex_lock(&shared->mutex) != 0) {
        return 1;
    }
    int result = 0;
    if (wait_producers || wait_consumers) {
        uint32_t *active = wait_producers ? &shared->active_producers
                                         : &shared->active_consumers;
        while (*active < expected && shared->state == SOA_STATE_OPEN) {
            if (pthread_cond_wait(&shared->state_changed, &shared->mutex) != 0) {
                result = 1;
                break;
            }
        }
        if (*active < expected) {
            result = 1;
        }
    } else if (strcmp(argv[1], "close") == 0) {
        if (shared->active_consumers == 0
            && (shared->active_producers != 0 || shared->occupancy != 0)) {
            result = 1;
        } else {
            shared->state = SOA_STATE_CLOSING;
            result |= pthread_cond_broadcast(&shared->not_empty);
            result |= pthread_cond_broadcast(&shared->not_full);
            result |= pthread_cond_broadcast(&shared->state_changed);
        }
    } else if (strcmp(argv[1], "wake") == 0) {
        result = pthread_cond_broadcast(&shared->state_changed);
    } else if (strcmp(argv[1], "inspect") != 0) {
        result = 1;
    }
    uint64_t produced = 0, consumed = 0, mutex_wait = 0, condition_wait = 0;
    for (uint32_t i = 1; i <= SOA_MAX_PARTICIPANT_ID; ++i) {
        produced += shared->producer_metrics[i].operations;
        consumed += shared->consumer_metrics[i].operations;
        mutex_wait += shared->producer_metrics[i].mutex_wait_ns
                    + shared->consumer_metrics[i].mutex_wait_ns;
        condition_wait += shared->producer_metrics[i].condition_wait_ns
                        + shared->consumer_metrics[i].condition_wait_ns;
    }
    uint32_t state = shared->state, occupancy = shared->occupancy;
    uint32_t producers = shared->active_producers, consumers = shared->active_consumers;
    uint64_t accepted = shared->accepted_count, extracted = shared->extracted_count;
    if (pthread_mutex_unlock(&shared->mutex) != 0) {
        result = 1;
    }
    if (soa_instance_close(&instance) != SOA_OK) {
        result = 1;
    }
    printf("state=%u occupancy=%u producers=%u consumers=%u accepted=%" PRIu64
           " extracted=%" PRIu64 " produced=%" PRIu64 " consumed=%" PRIu64
           " mutex_wait=%" PRIu64 " condition_wait=%" PRIu64 "\n",
           state, occupancy, producers, consumers, accepted, extracted,
           produced, consumed, mutex_wait, condition_wait);
    return result == 0 ? 0 : 1;
}
