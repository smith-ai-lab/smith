---
name: smith-json
description: "Using <smith/json.h>, smith's JSON reader and writer: a pull parser, an arena tree, a bounded writer. Load for JSON, code that includes <smith/json.h> or calls smith_json_, when reading tokenizer.json, when parsing or writing protocol messages (MCP, DAP, JSON-RPC), or when a build.c calls smith_library(b, \"json\")."
---

# JSON: smith's json

This skill ships in the smith plugin, from `.agents/skills/smith-json/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The header is `.smith/include/smith/json.h`; read it first. Numbers are `smith::text`'s (smith-text skill), memory is `smith::arena`'s (smith-arena). No libc, no malloc, every loop bounded.

**json is C today, used from `.smith` as `import <smith/json.h>;` with every name under `c::`** (`c::SmithJsonValue`, `c::smith_json_parse`); it becomes a module later (the names and what they do stay). Text and the arena are modules already, so its inputs are `text::Text` and `arena::Arena` (the same types as the header's `SmithText` and `SmithArena`). The prose below names functions by their C names (`smith_json_parse`); in `.smith` write `c::` before each. The C form is under "From C".

## Read a tree

```smith
import smith::arena;
import smith::text;
import <smith/json.h>;

// The name of a document's "name" member, or an empty text; the error goes to the console of the caller.
text::Text name_of(arena::Arena *a, text::Text input) {
    c::SmithJsonLimits limits = {.depth = 32, .string_length = 1 << 16, .count = 1 << 20}; // yours, never defaults
    const c::SmithJsonValue *root;
    c::SmithJsonError error;
    if (!c::smith_json_parse(a, input, limits, &root, &error)) return (text::Text){};
    const c::SmithJsonValue *name = c::smith_json_member(root, "name"); // the last of a repeated key, or nullptr
    if (name == nullptr) return (text::Text){};
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
- A tree that does not fit is `OutOfMemory`, and the arena is left as it was.

## Pull, without a tree

```smith
import smith::text;
import <smith/json.h>;

bool has_model(text::Text input, c::SmithJsonLimits limits) {
    c::SmithJsonReader reader = c::smith_json_reader_make(input, limits);
    for (c::SmithJsonToken t = c::smith_json_read(&reader); t != c::SmithJsonToken_End; t = c::smith_json_read(&reader)) {
        if (t == c::SmithJsonToken_Error) return false; // reader.error
        if (t == c::SmithJsonToken_Key && !c::smith_json_is(&reader, "model")) (void)c::smith_json_skip(&reader); // skip a value
    }
    return true;
}
```

`smith_json_decode(&reader, &arena)` decodes the last string or key; `smith_json_is` compares one with no arena. `smith_json_skip` jumps over a value.

A number is kept as its text, checked for JSON's grammar only. Convert it yourself with `text::parse_i64`, `parse_u64` (exact; out of range is false, and `1e2` is not an integer) or `parse_f64` (correctly rounded; `1e999` is false).

## Write

```smith
import smith::text;
import <smith/json.h>;

bool write_pi(text::Text *out) {
    char buffer[4096];
    c::SmithJsonWriter w = c::smith_json_writer_make(buffer, sizeof buffer, 2); // 0 for compact, else spaces per level
    c::smith_json_write_object_begin(&w);
    c::smith_json_write_key(&w, text::make("pi"));
    c::smith_json_write_f64(&w, 3.14159); // the fewest digits that read back as the same bits
    c::smith_json_write_object_end(&w);
    return c::smith_json_writer_finish(&w, out); // false: w.error, one of Full, Misplaced, BadUtf8, NotFinite
}
```

The first mistake is kept and the rest do nothing, so check once, at the end. Non-UTF-8 and nan/infinity are refused.

`smith_json_write_value(&w, tree)` writes a whole tree from `smith_json_parse` back as one value (strings escaped again, numbers as they were written, members in order, duplicate keys kept). It is how a protocol keeps a field it does not look inside: store the `const SmithJsonValue *`, write it back later.

It refuses what RFC 8259 does, plus a lone surrogate escape and a byte order mark (JSONTestSuite: all `y_` accepted, all `n_` refused, the 35 `i_` listed in `tests/json.c`). In build.c: `smith_library(b, "json")` with `arena` and `text`; `tests/json.sh` has a worked one.

## From C

C includes `<smith/json.h>` and uses the names as they are (`SmithText`, `SmithArena`, `smith_text_make`):

```c
#include <smith/json.h>

SmithJsonLimits limits = {.depth = 32, .string_length = 1 << 16, .count = 1 << 20};
const SmithJsonValue *root;
SmithJsonError error;
if (!smith_json_parse(&arena, input, limits, &root, &error))
    fprintf(stderr, "byte %zu: %s\n", error.offset, smith_json_reason_text(error.reason));
const SmithJsonValue *name = smith_json_member(root, "name");
```
