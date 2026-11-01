---
name: smith-json
description: "Reading JSON with smith's json (import std::json; <smith/json.h>): a tree from malloc and from C. Load for JSON, code that calls smith_json_ or includes <smith/json.h>, tokenizer.json, MCP, DAP or JSON-RPC messages."
---

# JSON: smith's json

**json is C today, used from `.smith` as `import <smith/json.h>;` with every name under `c::`** (`c::SmithJsonValue`, `c::smith_json_parse`); it becomes a module later (the names and what they do stay). Text is a module already, so its text is `const char[:]` (the header's `struct smith_array_const_char_n`). The tree is plain `malloc`: whatever allocator is in force gives it (put an arena in force around a parse to free the tree by resetting it; `smith-mem`). The prose below names functions by their C names (`smith_json_parse`); in `.smith` write `c::` before each. The C form is under "From C".

## Read a tree

```smith
import std::arena;
import std::mem;
import std::text;
import <smith/json.h>;

// The name of a document's "name" member, or an empty text; the error goes to the console of the caller. The tree is
// taken from the arena put in force for the parse, so the caller's reset of `a` frees it.
typeof(const char[:]) name_of(arena::Arena *a, const char input[:]) {
    arena::ArenaAllocator allocator = arena::allocator(a);
    mem::Allocator *before = mem::set_allocator(&allocator.base);
    c::SmithJsonLimits limits = {.depth = 32, .string_length = 1 << 16, .count = 1 << 20}; // yours, never defaults
    const c::SmithJsonValue *root;
    c::SmithJsonError error;
    Bool parsed = c::smith_json_parse(input, limits, &root, &error);
    mem::set_allocator(before);
    if (!parsed) return {};
    const c::SmithJsonValue *name = c::smith_json_member(root, "name"); // the last of a repeated key, or nullptr
    if (name == nullptr) return {};
    return name->text;
}

Usize total_items(const c::SmithJsonValue *list) {
    Usize total = 0;
    for (Usize i = 0; i < list->count; i++) total += list->items[i]->count; // arrays: elements; objects: member values, .key each
    return total;
}
```

- A value has `kind`, `offset`, `key`, `text` (a decoded string, or a number as written), `count`, `items` and `span`.
- The input must outlive the tree. A string with no escape is a slice of it; one with escapes is decoded into the tree's block.
- A tree the allocator in force cannot give stops the program (there is no `OutOfMemory` reason). A bad document, or one over a limit, is the `false` and `error`.

## From C

C includes `<smith/json.h>` and uses the names as they are (`struct smith_array_const_char_n` with `.data` and `.count[0]`, `smith_text_make`):

```c
#include <smith/json.h>

SmithJsonLimits limits = {.depth = 32, .string_length = 1 << 16, .count = 1 << 20};
const SmithJsonValue *root;
SmithJsonError error;
if (!smith_json_parse(input, limits, &root, &error)) // malloc's tree: free it, or set an arena first
    fprintf(stderr, "byte %zu: %s\n", error.offset, smith_json_reason_text(error.reason));
const SmithJsonValue *name = smith_json_member(root, "name");
```

Also in this family: [smith-json-pull-write](../smith-json-pull-write/SKILL.md).
