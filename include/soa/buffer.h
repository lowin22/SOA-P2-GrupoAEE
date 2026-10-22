#ifndef SOA_BUFFER_H
#define SOA_BUFFER_H

#include "soa/events.h"
#include "soa/instance.h"
#include "soa/message.h"
#include "soa/types.h"

/* Bloquea por espacio mientras OPEN. enqueue_seq y enqueue_time_ns del borrador
 * son ignorados y se asignan al aceptar bajo el mutex. */
soa_status_t soa_buffer_put(soa_instance_t *instance,
                            const soa_message_t *draft, soa_event_t *event);

/* Bloquea por datos mientras OPEN. Retorna SOA_CLOSED al estar cerrando y vacío. */
soa_status_t soa_buffer_take(soa_instance_t *instance, soa_message_t *message,
                             soa_event_t *event);

#endif
