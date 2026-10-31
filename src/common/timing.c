#include "soa/timing.h"
#include "instance_internal.h"

#include <errno.h>
#include <time.h>

soa_status_t monotonic_time_ns(uint64_t *out)
{
    struct timespec now;
    if (out == NULL) {
        return SOA_INVALID_ARGUMENT;
    }
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

soa_status_t soa_producer_pause(soa_instance_t *instance, uint64_t monotonic_deadline_ns)
{
    if (instance == NULL || instance->shared == NULL || !instance->registered
        || !instance->read_lock_held || instance->role != SOA_ROLE_PRODUCER) {
        return SOA_INVALID_ARGUMENT;
    }
    soa_shared_instance_t *shared = instance->shared;
    struct timespec deadline = {
        .tv_sec = (time_t)(monotonic_deadline_ns / UINT64_C(1000000000)),
        .tv_nsec = (long)(monotonic_deadline_ns % UINT64_C(1000000000))
    };
    int rc = pthread_mutex_lock(&shared->mutex);
    if (rc != 0) {
        errno = rc;
        return SOA_SYSTEM_ERROR;
    }
    soa_status_t status = SOA_OK;
    while (shared->state == SOA_STATE_OPEN) {
        uint64_t now;
        status = monotonic_time_ns(&now);
        if (status != SOA_OK || now >= monotonic_deadline_ns) {
            break;
        }
        rc = pthread_cond_timedwait(&shared->state_changed, &shared->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            break;
        }
        if (rc != 0) {
            errno = rc;
            status = SOA_SYSTEM_ERROR;
            break;
        }
    }
    if (status == SOA_OK && shared->state != SOA_STATE_OPEN) {
        status = SOA_CLOSED;
    }
    rc = pthread_mutex_unlock(&shared->mutex);
    if (rc != 0) {
        errno = rc;
        return SOA_SYSTEM_ERROR;
    }
    return status;
}

soa_status_t consumer_process_until(uint64_t deadline_ns)
{
    struct timespec deadline = {
        .tv_sec = (time_t)(deadline_ns / UINT64_C(1000000000)),
        .tv_nsec = (long)(deadline_ns % UINT64_C(1000000000))
    };
    int rc;
    do {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    } while (rc == EINTR);
    if (rc != 0) {
        errno = rc;
        return SOA_SYSTEM_ERROR;
    }
    return SOA_OK;
}
