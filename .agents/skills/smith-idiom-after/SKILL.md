---
name: smith-idiom-after
description: "The rolls module as idiomatic Smith: the module, its main, and how each C habit became sized types, c:: names, arena::push and contracts. Load when porting C to .smith or reviewing a port."
---
# The same module as Smith

```smith
// rolls.smith
module rolls;

import smith::arena;
import smith::console;
import smith::format;
import smith::random;

constexpr i64 sides = 6;

export typedef struct {
    u32 *faces;
    usize count;
} Rolls;

// count rolls of a die, each from its own key split from `key`.
export Rolls make(arena::Arena *arena, random::Key key, usize count) {
    Rolls rolls = {.faces = arena::push_slice(arena, u32, count).data, .count = count};
    random::Key keys[:] = arena::push_slice(arena, random::Key, count);
    random::split(key, keys);
    for (usize i = 0; i < countof(keys); i++) {
        rolls.faces[i] = (u32)random::uniform_int64_sample(keys[i], (random::UniformInt64){.low = 1, .high = sides});
    }
    return rolls;
}

export f64 mean(const Rolls *rolls) {
    u64 sum = 0;
    for (usize i = 0; i < rolls->count; i++) sum += rolls->faces[i];
    return rolls->count == 0 ? 0 : (f64)sum / (f64)rolls->count;
}

export void print(const Rolls *rolls) {
    for (usize i = 0; i < rolls->count; i++) (void)console::print(rolls->faces[i], " ");
    (void)console::print("(mean ", format::fixed(mean(rolls), 2), ")\n");
}
```

```smith
// main.smith
module main;
import rolls;
import smith::arena;
import smith::entropy;
i32 main() {
    arena::Arena a = arena::growing(1 << 20, nullptr);
    rolls::Rolls dice = rolls::make(&a, entropy::seed(), 10);
    rolls::print(&dice);
    return 0;
}
```

Also in this family: [smith-idiom](../smith-idiom/SKILL.md), [smith-idiom-before](../smith-idiom-before/SKILL.md), [smith-idiom-port](../smith-idiom-port/SKILL.md).
