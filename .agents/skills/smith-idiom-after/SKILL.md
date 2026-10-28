---
name: smith-idiom-after
description: "The rolls module as idiomatic Smith: the module, its main, and how each C habit became sized types, c:: names, arena::push and contracts. Load when porting C to .smith or reviewing a port."
---

# The same module as Smith

```smith
// rolls.smith
module rolls;

import std::arena;
import std::io;
import std::random;

constexpr I64 sides = 6;

export typedef struct {
    U32 *faces;
    Usize count;
} Rolls;

// count rolls of a die, each from its own key split from `key`.
export Rolls make(arena::Arena *arena, random::Key key, Usize count) {
    Rolls rolls = {.faces = arena::push_slice(arena, U32, count).data, .count = count};
    random::Key keys[:] = arena::push_slice(arena, random::Key, count);
    random::split(key, keys);
    for (Usize i = 0; i < countof(keys); i++) {
        rolls.faces[i] = (U32)random::uniform_int64_sample(keys[i], (random::UniformInt64){.low = 1, .high = sides});
    }
    return rolls;
}

export F64 mean(const Rolls *rolls) {
    U64 sum = 0;
    for (Usize i = 0; i < rolls->count; i++) sum += rolls->faces[i];
    return rolls->count == 0 ? 0 : (F64)sum / (F64)rolls->count;
}

export void print(io::Console *console, const Rolls *rolls) {
    for (Usize i = 0; i < rolls->count; i++) (void)io::console_printf(console, "%u ", rolls->faces[i]);
    (void)io::console_printf(console, "(mean %.2f)\n", mean(rolls));
}
```

```smith
// main.smith
module main;

import rolls;
import std::io;

I32 main(io::Io *io) {
    rolls::Rolls dice = rolls::make(&io->arena, io->key, 10);
    rolls::print(&io->console, &dice);
    return 0;
}
```

Also in this family: [smith-idiom](../smith-idiom/SKILL.md), [smith-idiom-before](../smith-idiom-before/SKILL.md), [smith-idiom-port](../smith-idiom-port/SKILL.md).
