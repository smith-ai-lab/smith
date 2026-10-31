---
name: smith-json-pull-write
description: "The pull parser and the bounded writer of smith's json: reading without a tree and writing a document to a fixed buffer. Load when parsing a large or streamed JSON or producing JSON."
---

# Pull, and write

## Pull, without a tree

```smith
import smith::text;
import <smith/json.h>;

bool has_model(const char input[:], c::SmithJsonLimits limits) {
    c::SmithJsonReader reader = c::smith_json_reader_make(input, limits);
    for (c::SmithJsonToken t = c::smith_json_read(&reader); t != c::SmithJsonToken_End; t = c::smith_json_read(&reader)) {
        if (t == c::SmithJsonToken_Error) return false; // reader.error
        if (t == c::SmithJsonToken_Key && !c::smith_json_is(&reader, "model")) (void)c::smith_json_skip(&reader); // skip a value
    }
    return true;
}
```

`smith_json_decode(&reader)` decodes the last string or key (a string with escapes comes from `malloc`, the allocator in force); `smith_json_is` compares one with no allocation. `smith_json_skip` jumps over a value.

A number is kept as its text, checked for JSON's grammar only. Convert it yourself with `text::parse_i64`, `parse_u64` (exact; out of range is false, and `1e2` is not an integer) or `parse_f64` (correctly rounded; `1e999` is false).

## Write

```smith
import smith::text;
import <smith/json.h>;

bool write_pi(const char (*out)[:]) {
    char buffer[4096];
    c::SmithJsonWriter w = c::smith_json_writer_make(buffer, sizeof buffer, 2); // 0 for compact, else spaces per level
    c::smith_json_write_object_begin(&w);
    c::smith_json_write_key(&w, text::make("pi"));
    c::smith_json_write_f64(&w, 3.14159); // the fewest digits that read back as the same bits
    c::smith_json_write_object_end(&w);
    return c::smith_json_writer_finish(&w, out); // false: w.error, Full or BadUtf8
}
```

The first failure of the data is kept and the rest do nothing, so check once, at the end: `Full` (retry with a bigger buffer) or `BadUtf8` (the text is data). A call that cannot be right panics with a report in every build: a value where a key is due, an end that closes nothing, a key outside an object, infinity or nan, finishing before the document is complete.

`smith_json_write_value(&w, tree)` writes a whole tree from `smith_json_parse` back as one value (strings escaped again, numbers as they were written, members in order, duplicate keys kept). It is how a protocol keeps a field it does not look inside: store the `const SmithJsonValue *`, write it back later.

It refuses what RFC 8259 does, plus a lone surrogate escape and a byte order mark (JSONTestSuite: all `y_` accepted, all `n_` refused, the 35 `i_` listed in `tests/json_test.smith`). In build.c: `smith_library(b, "json")` with `text`; `tests/build_fixtures/library_units/build.c` has a worked one.

Also in this family: [smith-json](../smith-json/SKILL.md).
