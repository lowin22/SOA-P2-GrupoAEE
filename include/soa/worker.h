#ifndef WORKER_H
#define WORKER_H

#include "soa/options.h"
#include "soa/types.h"

int worker_run(const options_t *options, soa_role_t role);

#endif
