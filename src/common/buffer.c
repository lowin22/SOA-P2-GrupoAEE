#include "soa/buffer.h"
#include "instance_internal.h"

#include <errno.h>
#include <time.h>

static soa_status_t monotonic_now(uint64_t *out)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return SOA_SYSTEM_ERROR;
    }
    if (now.tv_sec < 0 || (uint64_t)now.tv_sec >
        (UINT64_MAX - (uint64_t)now.tv_nsec) / UINT64_C(1000000000)) {
        return SOA_OVERFLOW;
    }
    *out = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return SOA_OK;
}

static soa_status_t unlock_buffer(soa_shared_instance_t *shared, soa_status_t status)
{
    int rc = pthread_mutex_unlock(&shared->mutex);
    if (rc != 0) {
        errno = rc;
        return SOA_SYSTEM_ERROR;
    }
    return status;
}

static soa_status_t lock_buffer(soa_instance_t *instance, soa_role_t role)
{
    if (instance == NULL || instance->shared == NULL || instance->fd < 0
        || !instance->read_lock_held || !instance->registered
        || instance->role != role || instance->participant_id < SOA_MIN_PARTICIPANT_ID
        || instance->participant_id > SOA_MAX_PARTICIPANT_ID) {
        return SOA_INVALID_ARGUMENT;
    }
    soa_shared_instance_t *shared = instance->shared;
    int rc = pthread_mutex_lock(&shared->mutex);
    if (rc != 0) {
        errno = rc;
        return SOA_SYSTEM_ERROR;
    }
    if (shared->capacity < SOA_MIN_CAPACITY || shared->capacity > SOA_MAX_CAPACITY
        || shared->head >= shared->capacity || shared->tail >= shared->capacity
        || shared->occupancy > shared->capacity
        || shared->state < SOA_STATE_OPEN || shared->state > SOA_STATE_CLOSED
        || shared->next_enqueue_seq == 0 || shared->next_dequeue_seq == 0) {
        return unlock_buffer(shared, SOA_INCOMPATIBLE_INSTANCE);
    }
    return SOA_OK;
}

soa_status_t soa_buffer_put(soa_instance_t *instance,
                          const soa_message_t *draft, soa_event_t *event)
{
    if (draft == NULL || event == NULL || instance == NULL
        || draft->producer_id != instance->participant_id || draft->sequence == 0) {
        return SOA_INVALID_ARGUMENT;
    }
    soa_status_t status = lock_buffer(instance, SOA_ROLE_PRODUCER);
    if (status != SOA_OK) {
        return status;
    }
    soa_shared_instance_t *shared = instance->shared;
    while (shared->state == SOA_STATE_OPEN && shared->occupancy == shared->capacity) {
        int rc = pthread_cond_wait(&shared->not_full, &shared->mutex);
        if (rc != 0) {
            errno = rc;
            return unlock_buffer(shared, SOA_SYSTEM_ERROR);
        }
    }
    if (shared->state != SOA_STATE_OPEN) {
        return unlock_buffer(shared, SOA_CLOSED);
    }
    if (shared->next_enqueue_seq == UINT64_MAX || shared->accepted_count == UINT64_MAX) {
        return unlock_buffer(shared, SOA_OVERFLOW);
    }
    uint64_t now;
    status = monotonic_now(&now);
    if (status != SOA_OK) {
        return unlock_buffer(shared, status);
    }

    soa_message_t message = *draft;
    message.reserved = 0;
    message.enqueue_seq = shared->next_enqueue_seq++;
    message.enqueue_time_ns = now;
    shared->messages[shared->tail] = message;
    shared->tail = (shared->tail + 1) % shared->capacity;
    ++shared->occupancy;
    ++shared->accepted_count;
    *event = (soa_event_t){
        .role = instance->role, .participant_id = instance->participant_id,
        .kind = SOA_EVENT_ENQUEUE, .state = shared->state,
        .timestamp_ns = now, .occupancy = shared->occupancy, .message = message
    };

    int rc = pthread_cond_signal(&shared->not_empty);
    if (rc != 0) {
        errno = rc;
    }
    return unlock_buffer(shared, rc == 0 ? SOA_OK : SOA_SYSTEM_ERROR);
}

soa_status_t soa_buffer_take(soa_instance_t *instance, soa_message_t *message,
                           soa_event_t *event)
{
    if (message == NULL || event == NULL) {
        return SOA_INVALID_ARGUMENT;
    }
    soa_status_t status = lock_buffer(instance, SOA_ROLE_CONSUMER);
    if (status != SOA_OK) {
        return status;
    }
    soa_shared_instance_t *shared = instance->shared;
    while (shared->state == SOA_STATE_OPEN && shared->occupancy == 0) {
        int rc = pthread_cond_wait(&shared->not_empty, &shared->mutex);
        if (rc != 0) {
            errno = rc;
            return unlock_buffer(shared, SOA_SYSTEM_ERROR);
        }
    }
    if (shared->state == SOA_STATE_CLOSED || shared->occupancy == 0) {
        return unlock_buffer(shared, SOA_CLOSED);
    }
    if (shared->next_dequeue_seq == UINT64_MAX || shared->extracted_count == UINT64_MAX) {
        return unlock_buffer(shared, SOA_OVERFLOW);
    }
    uint64_t now;
    status = monotonic_now(&now);
    if (status != SOA_OK) {
        return unlock_buffer(shared, status);
    }

    *message = shared->messages[shared->head];
    shared->head = (shared->head + 1) % shared->capacity;
    --shared->occupancy;
    ++shared->extracted_count;
    shared->last_dequeue_ns = now;
    *event = (soa_event_t){
        .role = instance->role, .participant_id = instance->participant_id,
        .kind = SOA_EVENT_DEQUEUE, .state = shared->state,
        .timestamp_ns = now, .dequeue_seq = shared->next_dequeue_seq++,
        .occupancy = shared->occupancy, .message = *message
    };

    int rc = pthread_cond_signal(&shared->not_full);
    int state_rc = 0;
    if (shared->occupancy == 0) {
        state_rc = pthread_cond_broadcast(&shared->state_changed);
    }
    if (rc == 0) {
        rc = state_rc;
    }
    if (rc != 0) {
        errno = rc;
    }
    return unlock_buffer(shared, rc == 0 ? SOA_OK : SOA_SYSTEM_ERROR);
}
