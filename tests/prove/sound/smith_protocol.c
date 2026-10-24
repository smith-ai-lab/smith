// proves: smith_protocol_pick smith_protocol_fail
// prove-files: runtime/protocol/protocol.c lib/smith/random.smith lib/smith/arena.smith
// link: lib/json.c lib/smith/random.smith lib/smith/arena.smith lib/smith/text.smith
#include "../../../runtime/protocol/protocol.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("protocol_pick", sound_count, {
        const uint64_t seed = sound_u64(); // a key is used once, so it is made where it is used
        const size_t count = (size_t)(sound_next() & 1 ? sound_u64() >> (sound_next() % 3) : sound_between(0, 9));
        SOUND_NOTE("protocol_pick(key %llu, count %zu)", (unsigned long long)seed, count);
        const size_t index = smith_protocol_pick(smith_key_make(seed), count);
        if (index >= count) exit(1);
    });
    SOUND_RUN("protocol_fail", sound_count, {
        SmithProtocolError error;
        sound_fill(&error, sizeof error);
        SmithJsonValue value;
        sound_fill(&value, sizeof value);
        const SmithProtocolReason reason = (SmithProtocolReason)sound_between(0, 6);
        const SmithJsonValue *at = sound_next() & 3 ? &value : nullptr;
        SOUND_NOTE("protocol_fail(reason %d, at %p)", (int)reason, (const void *)at);
        (void)smith_protocol_fail(&error, reason, at);
    });
})
