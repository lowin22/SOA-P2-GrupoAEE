#include "soa/random.h"
#include "soa/message.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "Fallo en linea %d\n", __LINE__); exit(1); \
} } while (0)

int main(void)
{
    random_state_t a = {.state = 0}, b = {.state = 0};
    CHECK(random_next(&a) == UINT64_C(0xe220a8397b1dcdaf));
    a.state = 0;
    uint64_t total = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        uint64_t delay = random_delay_ns(&a, 5.0);
        CHECK(delay == random_delay_ns(&b, 5.0));
        total += delay;
    }
    double mean = (double)total / 100000.0;
    CHECK(mean > 4900000.0 && mean < 5100000.0);
    uint64_t state = a.state;
    CHECK(random_delay_ns(&a, 0.0) == 0 && a.state == state);
    soa_message_t message = {.producer_id = 1, .sequence = 1};
    soa_message_fill_payload(&message);
    CHECK(soa_message_payload_is_valid(&message));
    soa_message_t same = {.producer_id = 1, .sequence = 1};
    soa_message_fill_payload(&same);
    CHECK(memcmp(message.payload, same.payload, SOA_PAYLOAD_SIZE) == 0);
    message.payload[0] ^= 1;
    CHECK(!soa_message_payload_is_valid(&message));
    puts("OK generador reproducible pausas exponenciales y payload");
    return 0;
}
