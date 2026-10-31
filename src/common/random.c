#include "soa/random.h"

#include <math.h>

uint64_t random_next(random_state_t *random)
{
    random->state += UINT64_C(0x9E3779B97F4A7C15);
    uint64_t value = random->state;
    value = (value ^ (value >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94D049BB133111EB);
    return value ^ (value >> 31);
}

uint64_t random_delay_ns(random_state_t *random, double mean_ms)
{
    if (mean_ms == 0.0) {
        return 0;
    }
    /* La muestra siempre es mayor que cero para evitar log de cero */
    double uniform = (double)((random_next(random) >> 11) + 1) * 0x1p-53;
    return (uint64_t)(-log(uniform) * mean_ms * 1000000.0);
}
