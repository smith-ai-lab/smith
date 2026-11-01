---
name: smith-categorical
description: "A weighted random index with std::categorical (import std::categorical; categorical::make, categorical::sample): Vose's alias table from weights, drawn from a random::Key. Load when code imports std::categorical or includes <smith/categorical.h>, or on drawing an index with given weights."
---

# categorical: an index with given weights

```smith
import std::categorical;
import std::random;

usize pick(random::Key key) {
    f64 weights[3] = {1, 2, 3};
    categorical::Table table = categorical::make(weights, 3); // built once; memory from the allocator in force
    return categorical::sample(key, table);                   // an index below 3, drawn 1:2:3
}
```

- `make(weights, count)` needs `count >= 1`, weights finite and not negative with a positive sum (checked). Construction is deterministic: the same weights give the same table everywhere. The table and its two worklists come from `mem::alloc` (the allocator in force, see [smith-mem](../smith-mem/SKILL.md)) and belong to the caller's region: nothing is freed one at a time, so set an arena allocator around it (`mem::set_allocator(&arena_allocator.base)`) and let it outlive the table.
- `sample(key, table)` is two draws whatever the count: an index from the first part of a split of the key and a coin from the second. A zero weight is never drawn; the zero word gives index 0.
- It is a module of its own, not part of `std::random`, because it allocates: `random` (which smith links itself) imports nothing that allocates.
- From C: `<smith/categorical.h>`, `SmithCategoricalTable`, `smith_categorical_table_make(weights, count)`, `smith_categorical_table_sample(key, table)`; a project links `smith_library(b, "categorical")`, and `mem`, `random`, `arena` come with it. Set the allocator with `smith_mem_set_allocator(&allocator.base)` first.

Also in this family: [smith-random](../smith-random/SKILL.md), [smith-random-distributions](../smith-random-distributions/SKILL.md).
