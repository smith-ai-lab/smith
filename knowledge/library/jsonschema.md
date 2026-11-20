---
type: specification
title: JSON Schema
description: std.jsonschema, a Describer that gives the JSON Schema of a type for the JSON that std.json writes and reads with the same settings, each comment as a description.
status: partial
built:
  - "`std.jsonschema`, with `json_schema(json)` and `schema_text`, with which `Describe[T].describe` gives a JSON Schema of draft 2020-12 for the JSON that `to_json_as` writes and `from_json_as` reads with the same `Json`, with the comment of each type, field, and case as its `description`"
  - "not a `Map`, and no `$schema`; a type of another module has no description of its own, as [Describe](describe.md) says"
---

# JSON Schema

`std.jsonschema` is the [`Describer`](describe.md#the-describer) of JSON Schema for the JSON that [`std.json`](json.md) writes and reads. A type is described to it with `Describe`, and `schema_text` gives the schema as a text. The schema is of draft 2020-12, which the Model Context Protocol takes as its default dialect, so no `$schema` is written.

```
use std.console (console, print)
use std.describe (Describe)
use std.json (Json)
use std.jsonschema (json_schema, schema_text)

// What the note is.
struct Note {
  // What the note is called.
  title: str
  // How much it matters.
  weight: 1..6 = 3
  tags: List[str]
}

fn main() ! console, alloc {
  print(schema_text(Describe[Note].describe(json_schema(Json(cases: .key, names: .written)))))
}
```

This prints, on one line, `{"description":"What the note is.","type":"object","properties":{"title":{"description":"What the note is called.","type":"string"},"weight":{"description":"How much it matters.","type":"integer","minimum":1,"maximum":5},"tags":{"type":"array","items":{"type":"string"}}},"required":["title","tags"]}`.

The schema is of what the same `Json` writes and reads, so a value that `to_json_as` writes fits the schema of its type, and `from_json_as` reads a text exactly when it fits.

| Shape | Schema |
| --- | --- |
| A boolean | `{"type":"boolean"}` |
| A whole number | `{"type":"integer","minimum":`its least`,"maximum":`its greatest`}` |
| A text | `{"type":"string"}` |
| Nothing, `()` | `{"type":"null"}` |
| A value that may be missing | `{"anyOf":[`the value`,{"type":"null"}]}` |
| A sequence | `{"type":"array","items":`the element`}` |
| A tuple | `{"type":"array","prefixItems":[`the parts`],"minItems":`its size`,"maxItems":`its size`}` |
| A struct | `{"type":"object","properties":`each field by its written name`,"required":`the fields that have no default and are not an `Option``}` |
| An enum | `{"oneOf":`one schema for each case`}` |

A field that has a default or is an `Option` is left out of `required`, which is where `from_json` reads a missing one. With `unknown: .refuse`, each object has `"additionalProperties":false`. With `Names.camel`, a field is named as `to_json_as` writes it.

A case with `Cases.key` that holds nothing is `{"const":"name"}`, and one that holds a value is an object with the case's name as its only member. With `Cases.tag`, a case that holds nothing is an object with the tag as its only member, with a `const`, one that holds a struct is the struct's schema with the tag among its properties and required, and any other is an object with the tag and `value`.

**A comment is the `description`.** A field's comment is the description of its value, and where it has none, the comment of its type is. The comment of a struct is the description of its schema, and of a case, the description of its schema in the `oneOf`.
