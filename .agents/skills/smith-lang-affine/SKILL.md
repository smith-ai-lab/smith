---
name: smith-lang-affine
description: "[[smith::affine]] on a type: a value of it is used at most once, and a second use is a compile error naming the first (a key that must not be drawn from twice, a handle that must not be closed twice). Load when a type is marked [[smith::affine]], or on 'smith-affine:', 'is used again; it was already used at line', 'is used in a loop, so it would be used once per iteration' or 'an element of an array of affine values is used through a constant index'."
---

# Affine types

Draw twice from one random key and you get the same numbers twice. Mark the type and the compiler refuses a second use.

```smith
module keys;

export [[smith::affine]] typedef struct { u64 bits; } Key;

export u64 draw(Key key) { return key.bits; }

export void split(Key key, Key out[:]) { (void)key; (void)out; }    // uses key once, fills out

export u64 total(Key key, Key parts[:]) {
    split(key, parts);                                              // key is used here: not again
    u64 sum = 0;
    for (usize i = 0; i < countof(parts); i++) sum += draw(parts[i]);   // the plain loop: one element per turn
    return sum;
}
```

The mark is a valid C23 attribute (in C: `[[smith::affine]] typedef struct { ... } Key;`). smith checks it on every compile and in the editor, with no flag.

## The rules

- **Any read is a use**: passing, assigning, returning, `&key`, a member read (`h.key`). There is no borrowing and no new notation.
- **At most once**: a value never used is fine. A second use on any path is an error at the second use, naming the first. A use in one branch counts after the branches join.
- **Assigning a fresh value makes it usable again**: `key = next[0];`. Passing a whole array or slice to a call (`split(key, parts)`) makes its elements fresh.
- **Each element of an array or slice is its own value**: `keys[0]` and `keys[1]` once each. The index is a constant or the counter of a plain loop.
- **A loop** that uses a value from outside it is an error (once per iteration), except the plain loop above: counter from 0, `i < countof(parts)`, `i++`, `i` not changed in the body, `parts[i]` used once.

```smith fragment
u64 a = draw(parts[0]);
u64 b = draw(parts[0]);   // error: smith-affine: 'parts[0]' is used again; it was already used at line N
```

## Errors

Each message is at the second use, with a note at the first. The loop and index errors show the plain loop and the reassignment (`key = next[0];`) as the way out.

Not checked: unions, `_Atomic`, a cast that hides the type. A struct holding an affine member is not affine itself (reading the member is a use). Template functions and functions with another error are skipped.
