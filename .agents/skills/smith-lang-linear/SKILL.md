---
name: smith-lang-linear
description: "[[std::linear]] on a type: a value of it is used exactly once on every path (a key that must not be drawn from twice or dropped, a handle that must be closed), a second use and a missing use are compile errors, `_ = x;` discards, `defer` uses it on every way out. Load when a type is marked [[std::linear]], or on 'smith-linear:', 'is used again; it was already used at line', 'is never used on this path', 'is assigned a new value while the old one was never used', 'is borrowed' or 'an element of an array of linear values is used through a constant index'."
---

# Linear types

Draw twice from one random key and you get the same numbers twice; forget a key and a stream is thrown away. Mark the type and the compiler checks both: every value is used **exactly once**, on every path.

```smith
module keys;

import std::random;

export u64 pair(random::Key key) {
    struct { random::Key, random::Key } parts = random::split(key);   // uses key; parts[0] and parts[1] are two new keys
    random::UniformDouble unit = {.low = 0, .high = 1};
    f64 a = random::uniform_double_sample(parts[0], unit);
    _ = parts[1];                                                      // the second is dropped, and the code says so
    return (u64)a;
}
```

The mark is a valid C23 attribute (in C: `[[std::linear]] typedef struct { ... } Key;`; `[[clang::annotate("std::linear")]]` is the same mark, which a compiler without smith's plugin accepts without a warning). smith checks it on every compile and in the editor, with no flag.

## The rules

- **A use** is a read: passing by value, assigning, returning, `&key`, a member read (`h.key`), `_ = key;`, and a `defer` that uses it. `_ = key;` is the explicit discard.
- **Exactly once**: a second use on any path is an error at the second use, naming the first; a way out where the value is not yet used on that path is an error naming it and the fix. The ways out are a `return`, a `try` that returns early, a `break`, a `continue`, a `goto` and the end of the block the value is declared in; a parameter is checked at the function's exits.
- **`defer`** uses the value at the `defer` and covers every way out after it: `defer file::close(f);` right after opening is the shape. A way out before the `defer` is still an error.
- **A branch** that uses it on one path only leaves it unused on the other, and using it after the join is a second use.
- **Assigning a fresh value makes it usable again** (`key = next[0];`); assigning over a value never used is an error (use it, or `_ = key;` first). A variable declared with no value holds nothing to use.
- **A loop** that uses a value from outside it is an error (once per iteration), except the plain loop below.

## Tuples, arrays and slices

- **A tuple's count is in its type**, so each linear field is its own value (`parts[0]`, `parts[1]`), used exactly once; passing the whole tuple to a call uses every field.
- **A constant-size array** of linear values: each constant index is its own value; all of them are used.
- **A slice** (`random::Key keys[:] = random::split_many(key, n)`) has no count the check reads: it is used by moving it into a call (which owes a use of each element of a non-const slice parameter) or by the plain loop `for (usize i = 0; i < countof(keys); i++)` with `keys[i]` used once on every path of the body (a `break` that skips elements is an error). Any other index, or an index by constant on a slice, is an error.
- **A `const` slice parameter** (`const Key xs[:]`) borrows: the callee reads members (`xs[0].bits`) but cannot use an element up (an error naming the fix), and the caller still owes the uses.

```smith fragment
random::Key keys[:] = random::split_many(key, 3);
u64 total = 0;
for (usize i = 0; i < countof(keys); i++)
    total += draw(keys[i]);          // each key once; a slice is covered only by a loop like this or by a move
```

## Errors

Each message names the value and the way out (`the `return` at line N returns`, `the block ends at line N`). Not checked: unions, `_Atomic`, a cast that hides the type. A struct holding a linear member is not linear itself (reading the member is a use); a tuple is. Template functions and functions with another error are skipped.
