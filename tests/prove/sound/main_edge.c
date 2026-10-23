// proves: sum run_proved
// prove-files: tests/prove/main_edge.c
#include "../main_edge.c"
#include "sound.h"

// The edge: what the prover assumes of it, that it touches only what its pointer points to.
int32_t console_print(Console *console, int32_t value) {
    console->lines++;
    return value;
}

SOUND_MAIN({
    SOUND_RUN("sum", sound_count, {
        const int32_t count = (int32_t)sound_between(-2, 7);
        SOUND_NOTE("sum(%d)", count);
        (void)sum(count);
    });
    SOUND_RUN("run_proved", sound_count, {
        Console console = {sound_i32() & 0xffff};
        SOUND_NOTE("run_proved()");
        (void)run_proved(&console);
    });
})
