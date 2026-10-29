#ifndef SOA_BUFFER_H
#define SOA_BUFFER_H

#include "soa/events.h"
#include "soa/instance.h"
#include "soa/message.h"
#include "soa/types.h"

/* Espera espacio y asigna secuencia y tiempo bajo el mutex compartido */
soa_status_t soa_buffer_put(soa_instance_t *instance,
                            const soa_message_t *draft, soa_event_t *event);

/* Espera datos y permite drenar los mensajes aceptados durante el cierre */
soa_status_t soa_buffer_take(soa_instance_t *instance, soa_message_t *message,
                             soa_event_t *event);

#endif
