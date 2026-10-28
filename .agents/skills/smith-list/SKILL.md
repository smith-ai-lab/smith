---
name: smith-list
description: "Using smith::list (import smith::list; list::push, pop, items, finish, free, count): List(T), a list that grows from the allocator in force; its zero value is empty. Load when code imports smith::list, writes `list::List(`, or on growing an array without malloc and realloc by hand."
---

# A list that grows: smith's list

This skill ships in the smith plugin, from `.agents/skills/smith-list/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`list::List(T)` is a type function (`smith-lang-enums`): the struct of a data pointer, a count and a capacity. Its zero value is the empty list, and its memory comes from the allocator in force on the thread (`smith-mem`), so the same code runs under a fixed arena (it stops at the arena's budget, a checked failure), a growing arena (the last block is extended in place) and the heap.

```smith
module numbers;

import smith::list;

typedef list::List(i32) Numbers;

export usize count_after(usize n) {
    Numbers xs = {};
    for (usize i = 0; i < n; i++) {
        list::push(&xs, (i32)i);
    }
    i32 all[:] = list::items(&xs);
    usize count = countof(all);
    list::free(&xs);
    return count;
}
```

## Rules

- **`list::push(&xs, item)`** adds at the end (the capacity doubles, 4 first); **`list::pop(&xs)`** is an `option(T)`, `.none` when empty; **`list::count(&xs)`** is the count.
- **`list::items(&xs)`** is a slice of the items: valid until the next `push`, which may move them. Nothing checks that yet (the borrow rule waits for #439).
- **`list::finish(&xs)`** hands the items over as a slice and leaves the list empty; the memory is not freed (an arena frees it at its reset or scope end). **`list::free(&xs)`** gives it back and empties the list.
- **The functions take the list as a pattern** (`smith-lang-generics-pattern`): `List(constexpr Type T) *xs` binds `T` from the argument, and writing your own function over lists is the same.
- **Name the option first at file scope**: `typedef option(i32) Last;` before a function that calls `list::pop` (an enum struct first named inside a static function is not found by the instance, a known gap).
- The fields (`data`, `count`, `capacity`) are plain data; use the functions.
