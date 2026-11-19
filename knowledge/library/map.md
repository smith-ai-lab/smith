---
type: specification
title: Map
description: Map, which finds a value by its key, kept in the order its entries were added, and the Hash trait a key needs.
status: partial
built:
  - "`Hash`, which a whole number, `bool`, `()`, `str`, and every struct, enum, tuple, or list is given unless a part lacks it, which `without Hash for T` takes away, and which a type's own `impl` replaces"
  - "not `Map`"
---

# Map

A `Map[K, V]` holds values, each under a key, and finds a value by its key in a time that does not grow with the size of the map.

```
fn ages() -> Map[str, Byte] ! alloc {
  var m: Map[str, Byte] = []
  m = insert(m, "ada", 36)
  m = insert(m, "alan", 41)
  m
}

fn age_of(m: Map[str, Byte], name: str) -> Byte {
  match m[name] {
    .some(age) { age }
    .none { 0 }
  }
}
```

- `[]` is the empty map where a map is expected.
- `insert` gives a map with the value under the key, in place of any value the key had. A map is a value, as a [`List`](list.md) is: when the map it is given is a `var` that is assigned the result, as in `m = insert(m, k, v)`, the map grows in place.
- `m[key]` is `.some` of the value under the key, or `.none`. A map implements [`Index`](../language/indexing.md) with a `valid` that is `true`, since it answers for every key.
- `m.size` is the number of entries.

A map keeps its entries in the order their keys were first added. `m.keys` and `m.values` are lists in that order, and the value at a place belongs to the key at the same place. So a walk over a map is a walk over two lists, and it visits the same entries in the same order every time the program runs.

```
fn oldest(m: Map[str, Byte]) -> Byte {
  var top: Byte = 0
  loop i in 0..m.values.size {
    if m.values[i] > top { top = m.values[i] }
  }
  top
}
```

A key needs [`Eq`](../language/comparisons.md), to be told from another key, and `Hash`, to be found. `Hash` is given automatically, as `Eq` is: a struct, an enum, a tuple, or a list has it unless one of its parts lacks it, `without Hash for T` takes it away, and a type's own `impl` replaces the one it was given. Two keys that are equal have the same hash, whoever wrote the `hash`: equal keys are the same value, and a function gives the same answer for the same value. So a type's own `Hash` cannot break a map.

```
trait Hash[T] {
  fn hash(value: T) -> u64
}
```

The hash of a value is the same in every run, so nothing about a map differs from one run to the next.

A map is laid out for the processor's cache, in three runs of memory with no links between entries.

- The keys lie one after another, and so do the values, apart from the keys. A search reads keys and never a value, and reads a value only once its key is found.
- A table has one small entry for each place a key may be looked for: a few bits of the key's hash, and where in the lists the key is. A search starts at the place the hash names and goes along the table in a line, comparing those few bits several places at a time, and compares a key only where they match.
