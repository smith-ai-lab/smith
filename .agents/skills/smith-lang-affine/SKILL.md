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

The mark is a valid C23 attribute (in C: `[[smith::affine]] typedef struct { ... } Key;`; `[[clang::annotate("smith::affine")]]` is the same mark, which a compiler without smith's plugin accepts without a warning). smith checks it on every compile and in the editor, with no flag.

## The rules

- **Any read is a use**: passing, assigning, returning, `&key` anywhere but a call argument, a member read (`h.key`).
- **A borrow is not a use**: `&key` written directly as the argument of a `T *` or `const T *` parameter. The caller still owns `key`; its one consuming use is checked as always. `Key *p = &key;` stays a use.
- **At most once**: a value never used is fine. A second use on any path is an error at the second use, naming the first. A use in one branch counts after the branches join.
- **Assigning a fresh value makes it usable again**: `key = next[0];`. Passing a whole array or slice to a call (`split(key, parts)`) makes its elements fresh.
- **Each element of an array or slice is its own value**: `keys[0]` and `keys[1]` once each. The index is a constant or the counter of a plain loop.
- **A loop** that uses a value from outside it is an error (once per iteration), except the plain loop above: counter from 0, `i < countof(parts)`, `i++`, `i` not changed in the body, `parts[i]` used once.

A second `draw(parts[0])` is `error: smith-affine: 'parts[0]' is used again; it was already used at line N`.

## Borrows

```smith fragment
export u64 peek(const Key *k) { return k->bits; }   // shared: reads
export void bump(Key *k) { k->bits += 1; }          // exclusive: may write
u64 demo(Key key) {
    bump(&key);
    u64 a = peek(&key) + peek(&key);                // borrows, no use
    return a + draw(key);                           // the one consuming use
}
```

- **A `const T *` parameter** cannot be used up: `Key y = *k;`, `draw(*k)`, `return *k;` are errors (`'k' is borrowed; it cannot be used up here`). It may read non-affine members and pass `k` on to a pointer parameter; a `T *` may also be written through.
- **It cannot outlive the call**: storing `k` (global, field, local), returning it or converting it is an error (`'k' is borrowed; it cannot outlive the call`).
- **A `T *` may be used up and replaced** (`split(*stream, next); *stream = next[0];`) on every path; not replaced is an error. A borrow of a place already used is an error.
- **Shared and exclusive**: one call may borrow a place through several `const T *`; a `T *` borrow with any other argument naming the same place (`f(&x, &x)`, `f(&x, x)`) is an error.

Each error is at the second use, with a note at the first; the loop and index errors show the plain loop and the reassignment as the way out. Not checked: unions, `_Atomic`, a cast that hides the type, a pointer kept in a local. A struct holding an affine member is not affine itself. Template functions and functions with another error are skipped.
