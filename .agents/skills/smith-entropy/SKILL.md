---
name: smith-entropy
description: "Using smith::entropy (import smith::entropy; entropy::seed, set_handler, fake): a fresh random::Key from the system's entropy, called directly with no io::Io, and a fake with fixed bytes for a test. Load when code imports smith::entropy or includes <smith/entropy.h>, or on a random key that is not replayable, seeds from the system, or testing code that draws one."
---

# Entropy from the system: smith::entropy

This skill ships in the smith plugin, from `.agents/skills/smith-entropy/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there. It uses `smith-random`.

`smith::random` is pure: a `random::Key` is a value made from a seed (`random::key_make(seed)`). The system's randomness is this module's one job, so a key that is not replayable comes from here.

```smith
module fresh;

import smith::entropy;
import smith::random;

export random::Key fresh() {
    return entropy::seed(); // 64 random bits, a different key on every call
}
```

- **`seed()`** returns a `random::Key` from `getentropy`. Print `key.bits` to replay a run: `random::key_make(bits)` is the same key. A system that cannot answer is a checked failure.
- **The effect is `entropy`** (`smith-lang-effects`), inferred from the call, so a `[[smith::pure]]` function cannot call it. No `io::Io` is passed. `random` itself has no such effect.
- **Test with a fake**: `entropy::fake(number)` returns an `entropy::Fake` that writes the bytes of `number` (least significant first) and counts its fills in `asked`. `entropy::set_handler(&fake.base)` puts it in force for the thread and returns the previous handler (null: the system's); restore it with `defer`. `getentropy` of any other code on the thread reaches it too; `arc4random` and `getrandom` do not. See `lib/smith/entropy_test.smith`.
- **From C**: `<smith/entropy.h>` has `smith_entropy_seed()`, `smith_entropy_fake(number)` and `smith_entropy_set_handler`; the handler is `SmithEntropyHandler {fill}` from `<smith/handlers.h>`. This replaces `smith_random_seed`.
