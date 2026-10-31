---
name: smith-idiom-before
description: "A C module before porting, the rolls example: its header and source, with the habits that make a port C renamed. Load when reading C you are about to turn into a .smith module."
---

# A C module

A module that allocates from an arena, draws random numbers and prints to the console. First the C, as an agent writes it from the C API:

```c
// rolls.h
#ifndef ROLLS_H
#define ROLLS_H
#include <stddef.h>
#include <stdint.h>
#include <smith/arena.h>
#include <smith/console.h>
#include <smith/random.h>
#define ROLLS_SIDES 6

typedef struct {
    uint32_t *faces;
    size_t count;
} Rolls;

Rolls rolls_make(SmithArena *arena, SmithKey key, size_t count);
double rolls_mean(const Rolls *rolls);
void rolls_print(const Rolls *rolls);
#endif
```

```c
// rolls.c
#include "rolls.h"
#include <stdalign.h>
#include <stdio.h>
// count rolls of a die, each from its own key split from `key`.
Rolls rolls_make(SmithArena *arena, SmithKey key, size_t count) {
    Rolls rolls = {.faces = smith_arena_push_array(arena, sizeof(uint32_t), count, alignof(uint32_t)), .count = count};
    SmithKey *keys = smith_arena_push_array(arena, sizeof(SmithKey), count, alignof(SmithKey));
    smith_key_split_n(key, keys, count);
    for (size_t i = 0; i < count; i++)
        rolls.faces[i] = (uint32_t)smith_uniform_int64_sample(keys[i], (SmithUniformInt64){.low = 1, .high = ROLLS_SIDES});
    return rolls;
}

double rolls_mean(const Rolls *rolls) {
    uint64_t sum = 0;
    for (size_t i = 0; i < rolls->count; i++) sum += rolls->faces[i];
    return rolls->count == 0 ? 0 : (double)sum / (double)rolls->count;
}
void rolls_print(const Rolls *rolls) {
    for (size_t i = 0; i < rolls->count; i++) printf("%u ", rolls->faces[i]);
    printf("(mean %.2f)\n", rolls_mean(rolls));
}
```

Renamed to `rolls.smith` this compiles, and it has 13 standard C types, 8 `#include`s and 2 `#define`s. As Smith it is one file with no header:

Also in this family: [smith-idiom](../smith-idiom/SKILL.md), [smith-idiom-after](../smith-idiom-after/SKILL.md), [smith-idiom-port](../smith-idiom-port/SKILL.md).
