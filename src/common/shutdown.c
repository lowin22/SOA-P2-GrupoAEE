#include "soa/shutdown.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "instance_internal.h"
#include "soa/summary_writer.h"

struct soa_shutdown_snapshot {
    char name[NAME_MAX + 1U];
    uint32_t capacity;
    uint64_t accepted;
    uint64_t extracted;
    uint64_t pending;
    uint64_t first_worker_start_ns;
    uint64_t last_dequeue_ns;
    uint64_t closing_start_ns;
    uint64_t release_complete_ns;
};

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

static int descriptor_lock(int fd, short type, bool wait)
{
    struct flock lock = {
        .l_type = type,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0,
        .l_pid = 0
    };
    int result;

    do {
        result = fcntl(fd, wait ? F_SETLKW : F_SETLK, &lock);
    } while (result == -1 && errno == EINTR);
    return result;
}

static bool valid_instance(const soa_instance_t *instance)
{
    return instance != NULL && instance->fd >= 0 &&
           instance->read_lock_held && instance->shared != NULL &&
           !instance->registered && instance->name[0] == '/';
}

static bool drained(const soa_shared_instance_t *shared)
{
    return shared->occupancy == 0 && shared->active_producers == 0 &&
           shared->active_consumers == 0;
}

static void wake_all(soa_shared_instance_t *shared)
{
    (void)pthread_cond_broadcast(&shared->not_full);
    (void)pthread_cond_broadcast(&shared->not_empty);
    (void)pthread_cond_broadcast(&shared->state_changed);
}

soa_status_t soa_shutdown_run(soa_instance_t **instance,
                              soa_shutdown_snapshot_t **snapshot)
{
    soa_shutdown_snapshot_t *copy;
    soa_instance_t *current;
    soa_shared_instance_t *shared;
    uint64_t closing_start;
    bool failed = false;
    int result;

    if (instance == NULL || snapshot == NULL) {
        return SOA_INVALID_ARGUMENT;
    }
    *snapshot = NULL;
    current = *instance;
    if (!valid_instance(current)) {
        return SOA_INVALID_ARGUMENT;
    }

    copy = calloc(1, sizeof(*copy));
    if (copy == NULL) {
        return SOA_SYSTEM_ERROR;
    }
    (void)memcpy(copy->name, current->name, strlen(current->name) + 1U);

    shared = current->shared;
    result = pthread_mutex_lock(&shared->mutex);
    if (result != 0) {
        free(copy);
        return SOA_SYSTEM_ERROR;
    }
    if (shared->state != SOA_STATE_OPEN) {
        (void)pthread_mutex_unlock(&shared->mutex);
        free(copy);
        return SOA_CLOSED;
    }
    if (shared->finisher_claimed != 0) {
        (void)pthread_mutex_unlock(&shared->mutex);
        free(copy);
        return SOA_FINISHER_EXISTS;
    }
    shared->finisher_claimed = 1;

    if (shared->active_consumers == 0 &&
        (shared->active_producers > 0 || shared->occupancy > 0)) {
        shared->finisher_claimed = 0;
        (void)pthread_cond_broadcast(&shared->state_changed);
        (void)pthread_mutex_unlock(&shared->mutex);
        free(copy);
        return SOA_NO_CONSUMER;
    }
    if (monotonic_now(&closing_start) != SOA_OK) {
        shared->finisher_claimed = 0;
        (void)pthread_cond_broadcast(&shared->state_changed);
        (void)pthread_mutex_unlock(&shared->mutex);
        free(copy);
        return SOA_SYSTEM_ERROR;
    }

    shared->closing_start_ns = closing_start;
    shared->state = SOA_STATE_CLOSING;
    wake_all(shared);

    while (!drained(shared)) {
        result = pthread_cond_wait(&shared->state_changed, &shared->mutex);
        if (result != 0) {
            (void)pthread_mutex_unlock(&shared->mutex);
            free(copy);
            return SOA_SYSTEM_ERROR;
        }
    }

    shared->state = SOA_STATE_CLOSED;
    copy->capacity = shared->capacity;
    copy->accepted = shared->accepted_count;
    copy->extracted = shared->extracted_count;
    copy->pending = shared->occupancy;
    copy->first_worker_start_ns = shared->first_worker_start_ns;
    copy->last_dequeue_ns = shared->last_dequeue_ns;
    copy->closing_start_ns = shared->closing_start_ns;
    wake_all(shared);
    if (pthread_mutex_unlock(&shared->mutex) != 0) {
        free(copy);
        return SOA_SYSTEM_ERROR;
    }

    if (descriptor_lock(current->fd, F_UNLCK, false) == -1) {
        free(copy);
        return SOA_SYSTEM_ERROR;
    }
    current->read_lock_held = false;
    if (descriptor_lock(current->fd, F_WRLCK, true) == -1) {
        free(copy);
        return SOA_SYSTEM_ERROR;
    }

    shared->publication = SOA_PUBLICATION_RETIRED;
    if (pthread_cond_destroy(&shared->state_changed) != 0) {
        failed = true;
    }
    if (pthread_cond_destroy(&shared->not_empty) != 0) {
        failed = true;
    }
    if (pthread_cond_destroy(&shared->not_full) != 0) {
        failed = true;
    }
    if (pthread_mutex_destroy(&shared->mutex) != 0) {
        failed = true;
    }
    if (shm_unlink(current->name) == -1 && errno != ENOENT) {
        failed = true;
    }
    if (munmap(current->shared, current->mapped_size) == -1) {
        failed = true;
    }
    current->shared = NULL;
    if (descriptor_lock(current->fd, F_UNLCK, false) == -1) {
        failed = true;
    }
    if (close(current->fd) == -1) {
        failed = true;
    }
    current->fd = -1;
    free(current);
    *instance = NULL;

    if (monotonic_now(&copy->release_complete_ns) != SOA_OK ||
        copy->release_complete_ns < copy->closing_start_ns) {
        failed = true;
    }
    if (failed) {
        free(copy);
        return SOA_SYSTEM_ERROR;
    }
    *snapshot = copy;
    return SOA_OK;
}

soa_status_t soa_shutdown_snapshot_write_csv(
    const soa_shutdown_snapshot_t *snapshot, const char *path)
{
    soa_summary_writer_t writer = {0};
    soa_summary_t summary = {0};
    bool failed;

    if (snapshot == NULL || path == NULL || path[0] == '\0' ||
        snapshot->release_complete_ns < snapshot->closing_start_ns) {
        return SOA_INVALID_ARGUMENT;
    }

    summary.instance_name = snapshot->name;
    summary.capacity = snapshot->capacity;
    summary.accepted = snapshot->accepted;
    summary.extracted = snapshot->extracted;
    summary.pending = snapshot->pending;
    summary.first_worker_start_ns = snapshot->first_worker_start_ns;
    summary.last_dequeue_ns = snapshot->last_dequeue_ns;
    summary.closing_start_ns = snapshot->closing_start_ns;
    summary.release_complete_ns = snapshot->release_complete_ns;
    summary.closing_duration_ns = snapshot->release_complete_ns -
                                  snapshot->closing_start_ns;
    summary.has_first_worker_start = snapshot->first_worker_start_ns != 0;
    summary.has_last_dequeue = snapshot->last_dequeue_ns != 0;
    summary.has_closing_duration = 1;
    summary.has_throughput = snapshot->extracted > 0 &&
                             snapshot->last_dequeue_ns >
                                 snapshot->first_worker_start_ns;
    if (summary.has_throughput) {
        summary.throughput_msg_s =
            (double)snapshot->extracted * 1000000000.0 /
            (double)(snapshot->last_dequeue_ns -
                     snapshot->first_worker_start_ns);
    }

    if (soa_summary_writer_open(&writer, path) != 0) {
        return SOA_SYSTEM_ERROR;
    }
    failed = soa_summary_writer_write(&writer, &summary) != 0;
    if (soa_summary_writer_close(&writer) != 0) {
        failed = true;
    }
    return failed ? SOA_SYSTEM_ERROR : SOA_OK;
}

void soa_shutdown_snapshot_free(soa_shutdown_snapshot_t **snapshot)
{
    if (snapshot != NULL) {
        free(*snapshot);
        *snapshot = NULL;
    }
}
