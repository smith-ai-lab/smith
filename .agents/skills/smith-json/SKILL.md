---
name: smith-json
description: "Using <smith/json.h>, smith's JSON reader and writer: a pull parser, an arena tree, a bounded writer. Load for JSON, code that includes <smith/json.h> or calls smith_json_, when reading tokenizer.json, when parsing or writing protocol messages (MCP, DAP, JSON-RPC), or when a build.c calls smith_library(b, \"json\")."
---

# JSON: smith's json

This skill ships in the smith plugin, from `.agents/skills/smith-json/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The header is `.smith/include/smith/json.h`; read it first. Numbers are `<smith/text.h>`'s (smith-text skill), memory is `<smith/arena.h>`'s (smith-arena). No libc, no malloc, every loop bounded.

## Read a tree

```c
SmithJsonLimits limits = {.depth = 32, .string_length = 1 << 16, .count = 1 << 20};   // yours, never defaults
const SmithJsonValue *root;
SmithJsonError error;
if (!smith_json_parse(&arena, input, limits, &root, &error))
    fprintf(stderr, "byte %zu: %s\n", error.offset, smith_json_reason_text(error.reason));
const SmithJsonValue *name = smith_json_member(root, "name");     // the last of a repeated key, or nullptr
for (size_t i = 0; i < list->count; i++) use(list->items[i]);     // arrays: elements; objects: member values, .key each
```

- A value has `kind`, `offset`, `key`, `text` (a decoded string, or a number as written), `count`, `items` and `span`.
- The input must outlive the tree. A string with no escape is a slice of it; one with escapes is decoded into the arena.
- A tree that does not fit is `OutOfMemory`, and the arena is left as it was.

## Pull, without a tree

```c
SmithJsonReader reader = smith_json_reader_make(input, limits);
for (SmithJsonToken t = smith_json_read(&reader); t != SmithJsonToken_End; t = smith_json_read(&reader)) {
    if (t == SmithJsonToken_Error) { /* reader.error */ break; }
    if (t == SmithJsonToken_Key && !smith_json_is(&reader, "model")) (void)smith_json_skip(&reader);   // skip a value
}
```

`smith_json_decode(&reader, &arena)` decodes the last string or key; `smith_json_is` compares one with no arena. `smith_json_skip` jumps over a value.

A number is kept as its text, checked for JSON's grammar only. Convert it yourself with `smith_text_parse_i64`, `_u64` (exact; out of range is false, and `1e2` is not an integer) or `_f64` (correctly rounded; `1e999` is false).

## Write

```c
char buffer[4096];
SmithJsonWriter w = smith_json_writer_make(buffer, sizeof buffer, 2);   // 0 for compact, else spaces per level
smith_json_write_object_begin(&w);
smith_json_write_key(&w, smith_text_make("pi"));
smith_json_write_f64(&w, 3.14159);     // the fewest digits that read back as the same bits
smith_json_write_object_end(&w);
SmithText out;
if (!smith_json_writer_finish(&w, &out)) { /* w.error: Full, Misplaced, BadUtf8, NotFinite */ }
```

The first mistake is kept and the rest do nothing, so check once, at the end. Non-UTF-8 and nan/infinity are refused.

It refuses what RFC 8259 does, plus a lone surrogate escape and a byte order mark (JSONTestSuite: all `y_` accepted, all `n_` refused, the 35 `i_` listed in `tests/json.c`). In build.c: `smith_library(b, "json")` with `arena` and `text`; `tests/json.sh` has a worked one.
