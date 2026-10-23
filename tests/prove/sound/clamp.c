// proves: clamp draw_health
#include "../clamp.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("clamp", sound_count, {
        const int32_t x = sound_i32(), low = sound_i32(), high = sound_i32();
        SOUND_NOTE("clamp(%d, %d, %d)", x, low, high);
        (void)clamp(x, low, high);
    });
    SOUND_RUN("clamp at its edges", sound_count, {
        const int32_t low = sound_i32(), high = sound_i32(), x = (sound_next() & 1) ? low : high;
        SOUND_NOTE("clamp(%d, %d, %d)", x, low, high);
        (void)clamp(x, low, high);
    });
    SOUND_RUN("draw_health", sound_count, {
        const int32_t health = sound_i32(), max_health = (int32_t)sound_between(-3, 1003);
        SOUND_NOTE("draw_health(%d, %d)", health, max_health);
        (void)draw_health(health, max_health);
    });
})
