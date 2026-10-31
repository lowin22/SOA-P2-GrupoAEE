#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>

typedef struct {
    uint64_t state;
} random_state_t;

uint64_t random_next(random_state_t *random);
/* La media debe estar validada entre cero y mil milisegundos */
uint64_t random_delay_ns(random_state_t *random, double mean_ms);

#endif
