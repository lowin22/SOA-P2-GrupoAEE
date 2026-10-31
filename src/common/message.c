#include "soa/message.h"
#include "soa/random.h"
#include "soa/types.h"

#include <string.h>

void soa_message_fill_payload(soa_message_t *message)
{
    random_state_t random = {
        .state = ((uint64_t)message->producer_id << 32) ^ message->sequence
    };
    message->reserved = 0;
    for (size_t block = 0; block < 4; ++block) {
        uint64_t value = random_next(&random);
        for (size_t byte = 0; byte < 8; ++byte) {
            message->payload[block * 8 + byte] = (unsigned char)(value >> (byte * 8));
        }
    }
}

int soa_message_payload_is_valid(const soa_message_t *message)
{
    if (message == NULL || message->producer_id < SOA_MIN_PARTICIPANT_ID
        || message->producer_id > SOA_MAX_PARTICIPANT_ID || message->sequence == 0
        || message->reserved != 0) {
        return 0;
    }
    soa_message_t expected = *message;
    soa_message_fill_payload(&expected);
    return memcmp(message->payload, expected.payload, SOA_PAYLOAD_SIZE) == 0;
}
