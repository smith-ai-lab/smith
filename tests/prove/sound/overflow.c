// proves: add
#include "../overflow.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("add", sound_count, {
        const int32_t a = (int32_t)sound_between(-3, 1000003), b = (int32_t)sound_between(-3, 1000003);
        SOUND_NOTE("add(%d, %d)", a, b);
        (void)add(a, b);
    });
})
