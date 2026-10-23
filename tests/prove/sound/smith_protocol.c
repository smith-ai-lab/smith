// proves: smith_protocol_pick smith_protocol_fail
// prove-files: runtime/protocol/protocol.c bootstrap/lib/smith/random.c bootstrap/lib/smith/arena.c
// link: lib/json.c bootstrap/lib/smith/random.c bootstrap/lib/smith/arena.c bootstrap/lib/smith/text.c
#include "../../../runtime/protocol/protocol.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("protocol_pick", sound_count, {
        const SmithKey key = smith_key_make(sound_u64());
        const size_t count = (size_t)(sound_next() & 1 ? sound_u64() >> (sound_next() % 3) : sound_between(0, 9));
        SOUND_NOTE("protocol_pick(key %llu, count %zu)", (unsigned long long)key.bits, count);
        const size_t index = smith_protocol_pick(key, count);
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
