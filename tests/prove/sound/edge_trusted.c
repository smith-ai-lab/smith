// proves: shown_twice
#include <stdio.h>
#define printf(...) 0
#include "../edge_trusted.c"
#undef printf
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("shown_twice", sound_count, {
        const int32_t x = (int32_t)sound_between(-5, 1005);
        SOUND_NOTE("shown_twice(%d)", x);
        (void)shown_twice(x);
    });
})
