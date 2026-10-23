// proves: fill fill_once
#include "../zero_init.c"
#include "sound.h"

static void bag_make(Bag *bag) {
    sound_fill(bag, sizeof *bag);
    if (sound_next() % 4 != 0) bag->count = 0;
}

SOUND_MAIN({
    SOUND_RUN("fill", sound_count, {
        Bag bag;
        bag_make(&bag);
        const int32_t value = sound_i32();
        SOUND_NOTE("fill(count %d, %d)", bag.count, value);
        fill(&bag, value);
    });
    SOUND_RUN("fill_once", sound_count, {
        Bag bag;
        bag_make(&bag);
        SOUND_NOTE("fill_once(count %d)", bag.count);
        fill_once(&bag);
    });
})
