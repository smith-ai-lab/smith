// proves: five set_five three_then_bump bump two push two_through_a_pointer uses_bump_twice_read bump_twice_read clobber
#include "../memory.c"
#include "sound.h"

static void list_make(List *list) {
    sound_fill(list, sizeof *list);
    list->len = (uint8_t)sound_between(0, 12);
    if (sound_next() % 16 == 0) list->len = sound_u8();
}

SOUND_MAIN({
    SOUND_RUN("five", sound_count, {
        SOUND_NOTE("five()");
        (void)five();
    });
    SOUND_RUN("set_five", sound_count, {
        List list;
        list_make(&list);
        SOUND_NOTE("set_five(len %u)", list.len);
        set_five(&list);
    });
    SOUND_RUN("three_then_bump", sound_count, {
        List list;
        list_make(&list);
        if (sound_next() & 1) list.len = (uint8_t)sound_between(95, 104);
        SOUND_NOTE("three_then_bump(len %u)", list.len);
        (void)three_then_bump(&list);
    });
    SOUND_RUN("bump", sound_count, {
        List list;
        list_make(&list);
        if (sound_next() & 1) list.len = (uint8_t)sound_between(195, 204);
        SOUND_NOTE("bump(len %u)", list.len);
        bump(&list);
    });
    SOUND_RUN("two", sound_count, {
        SOUND_NOTE("two()");
        (void)two();
    });
    SOUND_RUN("push", sound_count, {
        List list;
        list_make(&list);
        const uint8_t x = sound_u8();
        SOUND_NOTE("push(len %u, %u)", list.len, x);
        push(&list, x);
    });
    SOUND_RUN("two_through_a_pointer", sound_count, {
        List list;
        list_make(&list);
        SOUND_NOTE("two_through_a_pointer(len %u)", list.len);
        (void)two_through_a_pointer(&list);
    });
    SOUND_RUN("uses_bump_twice_read", sound_count, {
        SOUND_NOTE("uses_bump_twice_read()");
        (void)uses_bump_twice_read();
    });
    SOUND_RUN("bump_twice_read", sound_count, {
        List list;
        list_make(&list);
        if (sound_next() & 1) list.len = (uint8_t)sound_between(195, 204);
        SOUND_NOTE("bump_twice_read(len %u)", list.len);
        bump_twice_read(&list);
    });
    SOUND_RUN("clobber", sound_count, {
        List list;
        list_make(&list);
        SOUND_NOTE("clobber(len %u)", list.len);
        clobber(&list);
    });
})
