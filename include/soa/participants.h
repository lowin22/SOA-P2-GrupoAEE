#ifndef SOA_PARTICIPANTS_H
#define SOA_PARTICIPANTS_H

#include <stdint.h>

#include "soa/events.h"
#include "soa/instance.h"
#include "soa/types.h"

/* Estas operaciones adquieren internamente el mutex compartido. event es
 * obligatorio y solo se modifica cuando la operación toma efecto. */
soa_status_t soa_participant_register(soa_instance_t *instance, soa_role_t role,
                                      uint32_t id, soa_event_t *event);

/* Antes de retirarse, el trabajador debe completar operaciones y entregar sus
 * métricas compartidas. Al retornar SOA_OK ya no puede acceder a la instancia;
 * el evento privado LEAVE puede escribirse y cerrarse en el log posteriormente. */
soa_status_t soa_participant_unregister(soa_instance_t *instance,
                                        soa_event_t *event);

#endif
