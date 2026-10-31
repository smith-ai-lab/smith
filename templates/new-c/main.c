// @NAME@. `smith run` builds and runs it.
#include "primes.h"

#include <inttypes.h>
#include <smith/console.h>
#include <smith/contract.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Writes the text to stdout through smith's console, which a test can stand in for with smith_console_set_handler.
static void say(const char *text) {
    struct smith_array_const_u8_n bytes = {(const uint8_t *)text, {strlen(text)}};
    (void)smith_console_write(bytes);
}

static void report(int32_t sum) {
    char line[64];
    (void)snprintf(line, sizeof line, "the first 5 primes add up to %" PRId32 "\n", sum);
    say(line);
    say("hello from @NAME@\n");
}

int main(void) {
    int32_t sum = primes_sum(5);
    smith_assert(sum <= 28);
    report(sum);
    return 0;
}
