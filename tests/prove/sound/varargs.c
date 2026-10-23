// proves: twice
#include "../varargs.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("twice", sound_count, {
        const int32_t x = (int32_t)sound_between(-5, 1005);
        SOUND_NOTE("twice(%d)", x);
        (void)twice(x);
    });
})
