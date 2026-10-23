// proves: after_announce
// sensor_percent's body is assembly (it returns whatever is in a register), so the functions that call it have no run
// time to check: only after_announce, which calls the trusted function that has no body, runs.
#define main trusted_main
#include "../trusted.c"
#undef main
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("after_announce", sound_count, {
        const int32_t x = (int32_t)sound_between(-5, 105);
        SOUND_NOTE("after_announce(%d)", x);
        (void)after_announce(x);
    });
})
