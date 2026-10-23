// proves: geometry::box::clamp
// prove-files: tests/prove/geometry/box.smith
// link: tests/prove/geometry/box.smith
// The module written in .smith (its functions are named as written), called through its generated header's names.
#include "sound.h"

int32_t geometry_box_clamp(int32_t x, int32_t low, int32_t high) __asm__("_geometry::box::clamp");

SOUND_MAIN({
    SOUND_RUN("geometry::box::clamp", sound_count, {
        const int32_t x = sound_i32(), low = sound_i32(), high = sound_i32();
        SOUND_NOTE("clamp(%d, %d, %d)", x, low, high);
        (void)geometry_box_clamp(x, low, high);
    });
})
