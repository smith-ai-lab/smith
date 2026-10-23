// proves: sum_bounded digits_counted harness_kept
#include "../loop_count.c"
#include "../static_harness.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("sum_bounded", sound_count, {
        const size_t count = (size_t)sound_between(0, 8);
        SOUND_NOTE("sum_bounded(%zu)", count);
        (void)sum_bounded(count);
    });
    SOUND_RUN("digits_counted", sound_count, {
        const uint64_t value = sound_u64();
        SOUND_NOTE("digits_counted(%llu)", (unsigned long long)value);
        (void)digits_counted(value);
    });
    SOUND_RUN("harness_kept", sound_count, {
        const int32_t x = sound_i32();
        SOUND_NOTE("harness_kept(%d)", x);
        (void)harness_kept(x);
    });
})
