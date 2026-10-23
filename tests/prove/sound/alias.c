// proves: restricted restricted_other_type reads_both
#include "../alias.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("restricted (two objects)", sound_count, {
        int32_t a = sound_i32() % 3, b = sound_i32();
        SOUND_NOTE("restricted(&{%d}, &{%d})", a, b);
        restricted(&a, &b);
    });
    SOUND_RUN("restricted_other_type (a member is another object)", sound_count, {
        Pair a = {sound_i32() % 3, sound_i32()};
        int32_t b = sound_i32();
        SOUND_NOTE("restricted_other_type({%d, %d}, &{%d})", a.x, a.y, b);
        restricted_other_type(&a, &b);
    });
    SOUND_RUN("reads_both (two objects)", sound_count, {
        int32_t a = (int32_t)sound_between(0, 2), b = (int32_t)sound_between(-101, 101);
        SOUND_NOTE("reads_both(&{%d}, &{%d})", a, b);
        (void)reads_both(&a, &b);
    });
    SOUND_RUN("reads_both (the same object twice: both are only read)", sound_count, {
        int32_t a = sound_next() & 7 ? 1 : (int32_t)sound_between(-101, 101);
        SOUND_NOTE("reads_both(&{%d}, &{%d}) the same", a, a);
        (void)reads_both(&a, &a);
    });
})
