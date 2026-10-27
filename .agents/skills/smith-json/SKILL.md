---
name: smith-json
description: "Reading JSON with smith's json (import smith::json; <smith/json.h>): an arena tree and from C. Load for JSON, code that calls smith_json_ or includes <smith/json.h>, tokenizer.json, MCP, DAP or JSON-RPC messages."
---

# JSON: smith's json

**json is C today, used from `.smith` as `import <smith/json.h>;` with every name under `c::`** (`c::SmithJsonValue`, `c::smith_json_parse`); it becomes a module later (the names and what they do stay). Text is a module already, so it is `const char[:]` (the header's `struct smith_array_const_char_n`); the tree's memory comes from the allocator in force (`smith::mem`), so no function takes an arena. The prose below names functions by their C names (`smith_json_parse`); in `.smith` write `c::` before each. The C form is under "From C".

## Read a tree

```smith
import smith::text;
import <smith/json.h>;

// The name of a document's "name" member, or an empty text; the tree is the caller's region (its allocator in force).
typeof(const char[:]) name_of(const char input[:]) {
    c::SmithJsonLimits limits = {.depth = 32, .string_length = 1 << 16, .count = 1 << 20}; // yours, never defaults
    const c::SmithJsonValue *root;
    c::SmithJsonError error;
    if (!c::smith_json_parse(input, limits, &root, &error)) return {};
    const c::SmithJsonValue *name = c::smith_json_member(root, "name"); // the last of a repeated key, or nullptr
    if (name == nullptr) return {};
    return name->text;
}

usize total_items(const c::SmithJsonValue *list) {
    usize total = 0;
    for (usize i = 0; i < list->count; i++) total += list->items[i]->count; // arrays: elements; objects: member values, .key each
    return total;
}
```

- A value has `kind`, `offset`, `key`, `text` (a decoded string, or a number as written), `count`, `items` and `span`.
- The input must outlive the tree. A string with no escape is a slice of it; one with escapes is decoded into the arena.
- The tree is one block from the allocator in force (`mem::alloc`), and there is no free: put an arena in force around the call (`arena::ArenaAllocator allocator = arena::allocator(&a);` then `mem::set_allocator(&allocator.base)`, restored with `defer`) and its reset is the free. An allocator that refuses panics with its report (there is no `OutOfMemory` reason). A bad document, or one over a limit, is the `false` and `error`, and takes nothing.
- A tree is allocated through the allocator only: nothing in json calls `malloc` or an arena by itself.

## From C

C includes `<smith/json.h>` and uses the names as they are (`struct smith_array_const_char_n` with `.data` and `.count[0]`, `smith_text_make`):

```c
#include <smith/arena.h>
#include <smith/json.h>
#include <smith/mem.h>

SmithArenaAllocator allocator = smith_arena_allocator(&arena); // the tree comes from the allocator in force
SmithAllocator *before = smith_mem_set_allocator(&allocator.base);
SmithJsonLimits limits = {.depth = 32, .string_length = 1 << 16, .count = 1 << 20};
const SmithJsonValue *root;
SmithJsonError error;
bool parsed = smith_json_parse(input, limits, &root, &error);
smith_mem_set_allocator(before);
if (!parsed) fprintf(stderr, "byte %zu: %s\n", error.offset, smith_json_reason_text(error.reason));
const SmithJsonValue *name = smith_json_member(root, "name");
```

Also in this family: [smith-json-pull-write](../smith-json-pull-write/SKILL.md).
