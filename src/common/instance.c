#include "soa/instance.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "instance_internal.h"

static bool valid_name(const char *name)
{
    size_t length;

    if (name == NULL || name[0] != '/' || name[1] == '\0') {
        return false;
    }
    length = strlen(name);
    if (length > NAME_MAX) {
        return false;
    }
    return strchr(name + 1, '/') == NULL;
}

static bool mapping_size(uint32_t capacity, size_t *size)
{
    const size_t header_size = offsetof(soa_shared_instance_t, messages);

    if (capacity < SOA_MIN_CAPACITY || capacity > SOA_MAX_CAPACITY ||
        (size_t)capacity > (SIZE_MAX - header_size) / sizeof(soa_message_t)) {
        return false;
    }
    *size = header_size + (size_t)capacity * sizeof(soa_message_t);
    return true;
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

static void close_descriptor(int fd, bool locked)
{
    if (locked) {
        (void)descriptor_lock(fd, F_UNLCK, false);
    }
    (void)close(fd);
}

static bool valid_header(const soa_shared_instance_t *shared, off_t object_size,
                         size_t *expected_size)
{
    size_t calculated_size;

    if (shared->publication != SOA_PUBLICATION_READY ||
        shared->magic != SOA_INSTANCE_MAGIC ||
        shared->abi_version != SOA_INSTANCE_ABI_VERSION ||
        !mapping_size(shared->capacity, &calculated_size) ||
        shared->mapped_size != calculated_size || object_size < 0 ||
        (uintmax_t)object_size != (uintmax_t)calculated_size) {
        return false;
    }
    *expected_size = calculated_size;
    return true;
}

soa_status_t soa_instance_create(const char *name, uint32_t capacity)
{
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    soa_shared_instance_t *shared = MAP_FAILED;
    size_t size;
    int fd = -1;
    bool locked = false;
    bool mutex_attr_ready = false;
    bool cond_attr_ready = false;
    bool mutex_ready = false;
    unsigned int conditions_ready = 0;

    if (!valid_name(name) || !mapping_size(capacity, &size) ||
        size > (size_t)INT64_MAX) {
        return SOA_INVALID_ARGUMENT;
    }

    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        return errno == EEXIST ? SOA_EXISTS : SOA_SYSTEM_ERROR;
    }
    if (descriptor_lock(fd, F_WRLCK, true) == -1) {
        goto cleanup;
    }
    locked = true;
    if (ftruncate(fd, (off_t)size) == -1) {
        goto cleanup;
    }
    shared = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        goto cleanup;
    }

    memset(shared, 0, size);
    shared->mapped_size = size;
    shared->capacity = capacity;
    shared->state = SOA_STATE_OPEN;
    shared->next_enqueue_seq = 1;
    shared->next_dequeue_seq = 1;

    if (pthread_mutexattr_init(&mutex_attr) != 0) {
        goto cleanup;
    }
    mutex_attr_ready = true;
    if (pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutex_init(&shared->mutex, &mutex_attr) != 0) {
        goto cleanup;
    }
    mutex_ready = true;

    if (pthread_condattr_init(&cond_attr) != 0) {
        goto cleanup;
    }
    cond_attr_ready = true;
    if (pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) != 0) {
        goto cleanup;
    }
    if (pthread_cond_init(&shared->not_full, &cond_attr) != 0) {
        goto cleanup;
    }
    conditions_ready = 1;
    if (pthread_cond_init(&shared->not_empty, &cond_attr) != 0) {
        goto cleanup;
    }
    conditions_ready = 2;
    if (pthread_cond_init(&shared->state_changed, &cond_attr) != 0) {
        goto cleanup;
    }
    conditions_ready = 3;

    (void)pthread_condattr_destroy(&cond_attr);
    cond_attr_ready = false;
    (void)pthread_mutexattr_destroy(&mutex_attr);
    mutex_attr_ready = false;

    shared->magic = SOA_INSTANCE_MAGIC;
    shared->abi_version = SOA_INSTANCE_ABI_VERSION;
    shared->publication = SOA_PUBLICATION_READY;

    if (munmap(shared, size) == -1) {
        goto cleanup;
    }
    shared = MAP_FAILED;
    if (descriptor_lock(fd, F_UNLCK, false) == -1) {
        goto cleanup;
    }
    locked = false;
    if (close(fd) == -1) {
        fd = -1;
        (void)shm_unlink(name);
        return SOA_SYSTEM_ERROR;
    }
    return SOA_OK;

cleanup:
    if (cond_attr_ready) {
        (void)pthread_condattr_destroy(&cond_attr);
    }
    if (mutex_attr_ready) {
        (void)pthread_mutexattr_destroy(&mutex_attr);
    }
    if (shared != MAP_FAILED) {
        if (conditions_ready >= 3U) {
            (void)pthread_cond_destroy(&shared->state_changed);
        }
        if (conditions_ready >= 2U) {
            (void)pthread_cond_destroy(&shared->not_empty);
        }
        if (conditions_ready >= 1U) {
            (void)pthread_cond_destroy(&shared->not_full);
        }
        if (mutex_ready) {
            (void)pthread_mutex_destroy(&shared->mutex);
        }
        (void)munmap(shared, size);
    }
    (void)shm_unlink(name);
    close_descriptor(fd, locked);
    return SOA_SYSTEM_ERROR;
}

soa_status_t soa_instance_open(const char *name, soa_instance_t **out)
{
    const size_t header_size = offsetof(soa_shared_instance_t, messages);
    soa_shared_instance_t *header = MAP_FAILED;
    soa_shared_instance_t *shared = MAP_FAILED;
    soa_instance_t *instance = NULL;
    struct stat object_stat;
    size_t size;
    int fd;
    soa_status_t status = SOA_SYSTEM_ERROR;

    if (out == NULL) {
        return SOA_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (!valid_name(name)) {
        return SOA_INVALID_ARGUMENT;
    }

    fd = shm_open(name, O_RDWR, 0);
    if (fd == -1) {
        return errno == ENOENT ? SOA_NOT_FOUND : SOA_SYSTEM_ERROR;
    }
    if (descriptor_lock(fd, F_RDLCK, true) == -1) {
        close_descriptor(fd, false);
        return SOA_SYSTEM_ERROR;
    }
    if (fstat(fd, &object_stat) == -1) {
        goto cleanup;
    }
    if (object_stat.st_size == 0) {
        status = SOA_NOT_READY;
        goto cleanup;
    }
    if (object_stat.st_size < 0 ||
        (uintmax_t)object_stat.st_size < (uintmax_t)header_size) {
        status = SOA_INCOMPATIBLE_INSTANCE;
        goto cleanup;
    }

    header = mmap(NULL, header_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (header == MAP_FAILED) {
        goto cleanup;
    }
    if (header->publication == SOA_PUBLICATION_RETIRED) {
        status = SOA_CLOSED;
        goto cleanup;
    }
    if (!valid_header(header, object_stat.st_size, &size)) {
        status = header->publication == SOA_PUBLICATION_INITIALIZING
                     ? SOA_NOT_READY
                     : SOA_INCOMPATIBLE_INSTANCE;
        goto cleanup;
    }
    if (munmap(header, header_size) == -1) {
        header = MAP_FAILED;
        goto cleanup;
    }
    header = MAP_FAILED;

    shared = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        goto cleanup;
    }
    if (!valid_header(shared, object_stat.st_size, &size)) {
        status = shared->publication == SOA_PUBLICATION_RETIRED
                     ? SOA_CLOSED
                     : SOA_INCOMPATIBLE_INSTANCE;
        goto cleanup;
    }
    instance = calloc(1, sizeof(*instance));
    if (instance == NULL) {
        goto cleanup;
    }
    instance->fd = fd;
    instance->read_lock_held = true;
    instance->mapped_size = size;
    instance->shared = shared;
    *out = instance;
    return SOA_OK;

cleanup:
    if (header != MAP_FAILED) {
        (void)munmap(header, header_size);
    }
    if (shared != MAP_FAILED) {
        (void)munmap(shared, size);
    }
    free(instance);
    close_descriptor(fd, true);
    return status;
}

soa_status_t soa_instance_close(soa_instance_t **instance)
{
    soa_instance_t *current;
    bool failed = false;

    if (instance == NULL || *instance == NULL) {
        return SOA_INVALID_ARGUMENT;
    }
    current = *instance;
    if (current->registered) {
        return SOA_INVALID_ARGUMENT;
    }
    if (munmap(current->shared, current->mapped_size) == -1) {
        failed = true;
    }
    if (current->read_lock_held &&
        descriptor_lock(current->fd, F_UNLCK, false) == -1) {
        failed = true;
    }
    if (close(current->fd) == -1) {
        failed = true;
    }
    free(current);
    *instance = NULL;
    return failed ? SOA_SYSTEM_ERROR : SOA_OK;
}
