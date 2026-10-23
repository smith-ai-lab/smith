// proves: swap_halves
#include "../cause.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("swap_halves", sound_count, {
        Pair pair = {sound_i32(), sound_i32()};
        SOUND_NOTE("swap_halves({%d, %d})", pair.a, pair.b);
        swap_halves(&pair);
    });
})
