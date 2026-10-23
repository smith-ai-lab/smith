---
name: smith-idiom-after
description: "The rolls module as idiomatic Smith: the module, its main, and how each C habit became sized types, c:: names, arena::push and contracts. Load when porting C to .smith or reviewing a port."
---

# The same module as Smith

```smith
// rolls.smith
module rolls;

import smith::arena;
import smith::io;
import smith::random;

constexpr i64 sides = 6;

export typedef struct {
    u32 *faces;
    usize count;
} Rolls;

// count rolls of a die, each from its own key derived from `key`.
export Rolls make(arena::Arena *arena, random::Key key, usize count) {
    Rolls rolls = {.faces = arena::push_array(arena, u32, count), .count = count};
    for (usize i = 0; i < count; i++) {
        random::Key roll = random::key_derive(key, i);
        rolls.faces[i] = (u32)random::uniform_int64_sample(roll, (random::UniformInt64){.low = 1, .high = sides});
    }
    return rolls;
}

export f64 mean(const Rolls *rolls) {
    u64 sum = 0;
    for (usize i = 0; i < rolls->count; i++) sum += rolls->faces[i];
    return rolls->count == 0 ? 0 : (f64)sum / (f64)rolls->count;
}

export void print(io::Console *console, const Rolls *rolls) {
    for (usize i = 0; i < rolls->count; i++) (void)io::console_printf(console, "%u ", rolls->faces[i]);
    (void)io::console_printf(console, "(mean %.2f)\n", mean(rolls));
}
```

```smith
// main.smith
module main;

import rolls;
import smith::io;

i32 main(io::Io *io) {
    rolls::Rolls dice = rolls::make(&io->arena, io->key, 10);
    rolls::print(&io->console, &dice);
    return 0;
}
```

Also in this family: [smith-idiom](../smith-idiom/SKILL.md), [smith-idiom-before](../smith-idiom-before/SKILL.md), [smith-idiom-port](../smith-idiom-port/SKILL.md).
