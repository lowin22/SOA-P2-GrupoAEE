#include "soa/participants.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "instance_internal.h"

static bool valid_instance(const soa_instance_t *instance)
{
    return instance != NULL && instance->fd >= 0 &&
           instance->read_lock_held && instance->shared != NULL;
}

static bool valid_role(soa_role_t role)
{
    return role == SOA_ROLE_PRODUCER || role == SOA_ROLE_CONSUMER;
}

static soa_status_t monotonic_now(uint64_t *timestamp_ns)
{
    struct timespec value;
    uint64_t seconds;

    if (timestamp_ns == NULL || clock_gettime(CLOCK_MONOTONIC, &value) == -1 ||
        value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1000000000L) {
        return SOA_SYSTEM_ERROR;
    }
    seconds = (uint64_t)value.tv_sec;
    if (seconds > UINT64_MAX / UINT64_C(1000000000)) {
        return SOA_OVERFLOW;
    }
    *timestamp_ns = seconds * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
    return SOA_OK;
}

static uint64_t *used_ids(soa_shared_instance_t *shared, soa_role_t role)
{
    return role == SOA_ROLE_PRODUCER ? shared->used_producer_ids
                                     : shared->used_consumer_ids;
}

static uint32_t *active_count(soa_shared_instance_t *shared, soa_role_t role)
{
    return role == SOA_ROLE_PRODUCER ? &shared->active_producers
                                     : &shared->active_consumers;
}

static soa_participant_metrics_t *participant_metrics(
    soa_shared_instance_t *shared, soa_role_t role, uint32_t id)
{
    return role == SOA_ROLE_PRODUCER ? &shared->producer_metrics[id]
                                     : &shared->consumer_metrics[id];
}

static bool id_was_used(const uint64_t *bitmap, uint32_t id)
{
    return (bitmap[id / 64U] & (UINT64_C(1) << (id % 64U))) != 0;
}

static void set_id_used(uint64_t *bitmap, uint32_t id)
{
    bitmap[id / 64U] |= UINT64_C(1) << (id % 64U);
}

static void clear_id_used(uint64_t *bitmap, uint32_t id)
{
    bitmap[id / 64U] &= ~(UINT64_C(1) << (id % 64U));
}

static void fill_event(soa_event_t *event, soa_role_t role, uint32_t id,
                       soa_event_kind_t kind, uint64_t timestamp_ns,
                       const soa_shared_instance_t *shared)
{
    memset(event, 0, sizeof(*event));
    event->role = role;
    event->participant_id = id;
    event->kind = kind;
    event->state = (soa_state_t)shared->state;
    event->timestamp_ns = timestamp_ns;
    event->occupancy = shared->occupancy;
}

soa_status_t soa_participant_register(soa_instance_t *instance, soa_role_t role,
                                      uint32_t id, soa_event_t *event)
{
    soa_shared_instance_t *shared;
    soa_participant_metrics_t *metrics;
    uint64_t *bitmap;
    uint32_t *active;
    uint64_t timestamp_ns;
    uint64_t previous_first_start;
    soa_event_t captured;
    soa_status_t status;
    int result;

    if (!valid_instance(instance) || event == NULL || !valid_role(role)) {
        return SOA_INVALID_ARGUMENT;
    }
    if (id < SOA_MIN_PARTICIPANT_ID || id > SOA_MAX_PARTICIPANT_ID) {
        return SOA_INVALID_ID;
    }
    if (instance->registered) {
        return SOA_INVALID_ARGUMENT;
    }

    shared = instance->shared;
    result = pthread_mutex_lock(&shared->mutex);
    if (result != 0) {
        return SOA_SYSTEM_ERROR;
    }
    if (shared->state != SOA_STATE_OPEN) {
        (void)pthread_mutex_unlock(&shared->mutex);
        return SOA_CLOSED;
    }

    bitmap = used_ids(shared, role);
    active = active_count(shared, role);
    metrics = participant_metrics(shared, role, id);
    if (id_was_used(bitmap, id)) {
        (void)pthread_mutex_unlock(&shared->mutex);
        return SOA_DUPLICATE_ID;
    }
    if (*active >= SOA_MAX_PARTICIPANT_ID) {
        (void)pthread_mutex_unlock(&shared->mutex);
        return SOA_LIMIT_REACHED;
    }
    status = monotonic_now(&timestamp_ns);
    if (status != SOA_OK) {
        (void)pthread_mutex_unlock(&shared->mutex);
        return status;
    }

    previous_first_start = shared->first_worker_start_ns;
    set_id_used(bitmap, id);
    (*active)++;
    metrics->active = 1;
    if (shared->first_worker_start_ns == 0) {
        shared->first_worker_start_ns = timestamp_ns;
    }
    fill_event(&captured, role, id, SOA_EVENT_JOIN, timestamp_ns, shared);

    if (pthread_cond_broadcast(&shared->state_changed) != 0) {
        shared->first_worker_start_ns = previous_first_start;
        metrics->active = 0;
        (*active)--;
        clear_id_used(bitmap, id);
        (void)pthread_mutex_unlock(&shared->mutex);
        return SOA_SYSTEM_ERROR;
    }

    instance->registered = true;
    instance->role = role;
    instance->participant_id = id;
    *event = captured;
    return pthread_mutex_unlock(&shared->mutex) == 0 ? SOA_OK
                                                     : SOA_SYSTEM_ERROR;
}

soa_status_t soa_participant_unregister(soa_instance_t *instance,
                                        soa_event_t *event)
{
    soa_shared_instance_t *shared;
    soa_participant_metrics_t *metrics;
    uint32_t *active;
    uint64_t timestamp_ns;
    soa_event_t captured;
    soa_status_t status;
    int result;

    if (!valid_instance(instance) || event == NULL) {
        return SOA_INVALID_ARGUMENT;
    }
    if (!instance->registered || !valid_role(instance->role) ||
        instance->participant_id < SOA_MIN_PARTICIPANT_ID ||
        instance->participant_id > SOA_MAX_PARTICIPANT_ID) {
        return SOA_INVALID_ARGUMENT;
    }

    shared = instance->shared;
    result = pthread_mutex_lock(&shared->mutex);
    if (result != 0) {
        return SOA_SYSTEM_ERROR;
    }
    active = active_count(shared, instance->role);
    metrics = participant_metrics(shared, instance->role,
                                  instance->participant_id);
    if (*active == 0 || metrics->active == 0) {
        (void)pthread_mutex_unlock(&shared->mutex);
        return SOA_SYSTEM_ERROR;
    }
    status = monotonic_now(&timestamp_ns);
    if (status != SOA_OK) {
        (void)pthread_mutex_unlock(&shared->mutex);
        return status;
    }

    (*active)--;
    metrics->active = 0;
    fill_event(&captured, instance->role, instance->participant_id,
               SOA_EVENT_LEAVE, timestamp_ns, shared);
    if (pthread_cond_broadcast(&shared->state_changed) != 0) {
        metrics->active = 1;
        (*active)++;
        (void)pthread_mutex_unlock(&shared->mutex);
        return SOA_SYSTEM_ERROR;
    }

    instance->registered = false;
    instance->role = 0;
    instance->participant_id = 0;
    *event = captured;
    return pthread_mutex_unlock(&shared->mutex) == 0 ? SOA_OK
                                                     : SOA_SYSTEM_ERROR;
}
