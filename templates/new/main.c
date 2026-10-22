// @NAME@. `smith run` builds and runs it.
#include "primes.h"

#include <assert.h>
#include <inttypes.h>
#include <smith/io.h>
#include <stdint.h>

// A function takes only the capability it uses, so its signature says what it can do to the world.
// smith lint (smith-effects) reports a call like printf anywhere but in a capability's own module.
static void report(SmithConsole *console, int32_t sum) {
    (void)smith_console_printf(console, "the first 5 primes add up to %" PRId32 "\n", sum);
    (void)smith_console_print(console, "hello from @NAME@\n");
}

// main makes the program's io once, and only main may: it passes down the parts each function needs.
int main(int argc, char **argv) {
    SmithIo io = smith_io_make(argc, argv);
    int32_t sum = primes_sum(5);
    assert(sum == 28);
    report(&io.console, sum);
    smith_io_close(&io);
    return 0;
}
