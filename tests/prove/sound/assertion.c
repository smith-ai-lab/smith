// proves: next masked
#include "../assertion.c"
#define main asserts_only_main
#include "../asserts_only.c"
#undef main
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("next", sound_count, {
        const int32_t x = (int32_t)sound_between(-5, 105);
        SOUND_NOTE("next(%d)", x);
        (void)next(x);
    });
    SOUND_RUN("masked", sound_count, {
        const int32_t x = sound_i32();
        SOUND_NOTE("masked(%d)", x);
        (void)masked(x);
    });
})
