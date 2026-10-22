#ifndef SOA_MESSAGE_H
#define SOA_MESSAGE_H

#include <stdint.h>

#define SOA_PAYLOAD_SIZE 32U

/* Representación por valor apta para memoria compartida; no contiene punteros. */
typedef struct {
    uint32_t producer_id;
    uint32_t reserved; /* Debe escribirse como cero. */
    uint64_t sequence;
    uint64_t enqueue_seq;
    uint64_t enqueue_time_ns;
    unsigned char payload[SOA_PAYLOAD_SIZE];
} soa_message_t;

/* Genera/verifica el payload determinista descrito en docs/ARQUITECTURA.md. */
void soa_message_fill_payload(soa_message_t *message);
int soa_message_payload_is_valid(const soa_message_t *message);

#endif
