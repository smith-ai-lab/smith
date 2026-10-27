---
type: design
title: Protocol code generated from the official schemas
description: The DAP and MCP protocol code is a build output of scripts/protocol.c, written from the schemas vendored in schemas/, never committed.
tags: [protocol, generated]
---
# Protocol code generated from the official schemas

**The protocol code of `smith mcp` is generated, never written by hand, and never committed** (issues #22 and #466): `scripts/protocol.c` reads DAP's and MCP's
JSON Schemas, vendored in `schemas/`, and writes `.smith/protocol/{dap,mcp}.[ch]` and their tests `test_protocol_{dap,mcp}.c` on `<smith/json.h>`. The
files are build outputs, like the libraries' headers: a step of the build graph per protocol (`build_protocol_steps` in build.c) runs the generator, and
every compile that includes the code (smith.c, `tests/mcp_fake.c`) waits for it. `tests/protocol_test.smith` builds and runs the tests under the sanitizers.

The debugger server of issue #22 (`smith mcp`) speaks two JSON protocols: DAP to `smith dap` (lldb-dap) and MCP to the agent. Neither
is written by hand. `scripts/protocol.c` reads a protocol's JSON Schema and writes C on `<smith/json.h>`. A fresh machine needs no network for it: the
schemas are in git.

## What is where

| Path | What |
| --- | --- |
| `scripts/protocol.c` | the generator, a program of the build like `scripts/pack.c` (built by build.c from `lib/smith/arena.smith`, `contract.smith`, `math.smith`, `mem.smith`, `random.smith` and `text.smith`, and `json.c`, whose tree comes from the allocator in force: the generator puts its arena in force around the parse) |
| `schemas/dap/`, `schemas/mcp/` | the vendored inputs: the schema (`debugAdapterProtocol.json`, `schema.json`) and the project's `LICENSE`, byte for byte as released |
| `runtime/protocol/protocol.h`, `protocol.c` | hand-written: the error a parse reports, the types a schema starts from (string, integer, number, boolean, any JSON), the checks the generated code shares |
| `.smith/protocol/dap.h`, `dap.c`, `mcp.h`, `mcp.c` | generated, not in git: a struct and four functions per type; smith.c includes `dap.c` and `mcp.c` with `-I .smith/protocol -I runtime/protocol` |
| `.smith/protocol/LICENSE-dap.txt`, `LICENSE-mcp.txt` | the generator's copy of the licenses; their first lines are also in the header of every generated file |
| `.smith/protocol/test_protocol_dap.c`, `_mcp.c` | generated tests: a round trip for every type, a refusal for every missing required member |
| `tests/protocol/test_protocol_samples.c`, `protocol_support.[ch]` | hand-written: messages as lldb-dap and an MCP client write them, and what the generated tests share |
| `tests/protocol_test.smith` | copies the generated code and the hand-written files into a scratch project and builds and runs them under the sanitizers (`smith test --test protocol`) |

The generated code is smith's own code for `smith mcp`, not a library for other projects (`lib/` holds the libraries smith ships to other projects).

## The schemas

Vendored from the project's release (a plain tag archive of its repository), unchanged; the release is the one the `source` line of each
generated file names.

| Protocol | Release | Vendored | Size |
| --- | --- | --- | --- |
| DAP | v1.71.0 (the newest tag) | `schemas/dap/debugAdapterProtocol.json` (JSON Schema draft-04, 192 definitions), `schemas/dap/LICENSE` (MIT, Microsoft) | 190 KB |
| MCP | 2026-07-28 (the newest release that is not a draft or a release candidate) | `schemas/mcp/schema.json` (draft 2020-12, 155 definitions), `schemas/mcp/LICENSE` | 181 KB |

DAP's license is MIT (Microsoft). MCP's repository is in a licensing transition from MIT to Apache-2.0 (its `LICENSE` says so, and holds the
Apache text); both permit copying the schema with the notice, which the vendored `LICENSE` is and which the generated files repeat (owner, 2026-10-27:
vendor the schemas and drop the package fetch, which was 44 MB for MCP).

## Regenerating

Nothing is run by hand: the step's inputs are the schema, its license and the generator, so `smith build` writes the files again when, and only
when, one changed (the generator writes a file only when its bytes change, so what includes it is not rebuilt for nothing). A file of this
code committed anywhere fails `tests/generated_files_test.smith` (the paths are in `.gitignore`).

To move to another release: replace the files in `schemas/` with the release's (and its `LICENSE`), change the `source` line in `build_protocols` in
build.c and in `scripts/seed_linux.c`, and `smith build`.

**A Linux machine with no smith:** `scripts/seed_linux.c` builds stage A (smith.c without `-DSMITH_MCP`, so it does not include the protocol code),
compiles the generator with stage A's `smith-cc` and runs it on `schemas/` into `.smith/seed/protocol/`, and then builds stage B (smith.c with
`-DSMITH_MCP`) with it. Not run on Linux yet.

## What a schema becomes

Every type has four functions named for it (`smith_dap_stopped_event_parse`, `_write`, `_equal`, `_random`) with the same shapes as the
hand-written types in `protocol.h`: `bool parse(arena, const SmithJsonValue *, T *, SmithProtocolError *)` reads a tree from
`smith_json_parse`; `void write(SmithJsonWriter *, const T *)`; `bool equal(const T *, const T *)`; `void random(arena, key, T *)`
makes a value from a key. Members keep the schema's names (`threadId`, `request_seq`), not snake case.

| Schema | C |
| --- | --- |
| object with `properties` | a struct; a required member is a member, an optional one is `bool has_x; T x;` |
| `allOf` (DAP's requests extend `Request`, which extends `ProtocolMessage`) | the members of every part, base first, flattened; a later part replaces a member of an earlier one (`Request.command` becomes the constant `"launch"`) |
| a `const`, or an `enum` of one value | no member: the parse requires it, the write puts it, so `type`/`event`/`command`/`method` discriminators cost nothing |
| members the schema does not list (anything but `additionalProperties: false`) | `SmithProtocolExtra additionalProperties`, the keys and raw values, written back; a launch request's adapter attributes and a tool's input schema survive |
| `string` / `integer` / `number` / `boolean` | `struct smith_array_const_char_n` (not copied), `int64_t`, `double`, `bool` |
| `enum` of strings | a C `enum`; a string outside it is refused |
| `_enum`, or `anyOf` of an enum and a string (an open enum) | `{ known; struct smith_array_const_char_n text; }`: `known` is the enum value or `_Other`, `text` the string as written |
| `anyOf` / `oneOf`, `type: [a, b]` | a tagged union `{ kind; union as; }`. Where every alternative is a struct with a constant string member of one name and a value of its own (`method`, `type`), that member picks the alternative; otherwise the alternatives are tried most specific first (most required members), the ones that take any JSON last |
| `type: [T, "null"]` | `{ bool is_null; T value; }` |
| `array` | `{ const T *items; size_t count; }` |
| `additionalProperties: schema` on an object with no properties | `{ const struct smith_array_const_char_n *keys; const T *values; size_t count; }` |
| `{}`, `true`, `{"type": "object"}`, the list of every JSON type | `SmithProtocolAny`, the `const SmithJsonValue *` tree, so nothing is lost; `nullptr` writes as null. An optional `Any` member counts as present when it is `null` |

- **Nothing recurses.** A reference that closes a cycle (`ExceptionDetails.innerException`, MCP's `JSONValue` made of `JSONValue`s) is cut:
  that member is kept as raw JSON, and the type a union of such parts collapses to is `SmithProtocolAny` too. To read it as the type, call that
  type's parse on the tree.
- **Not carried:** `description`, `format`, `minimum`, `maximum`, `maxItems` (a parse does not enforce them; a random value
  ignores them), and a `null` for an optional member that is not nullable reads as absent.
- **Memory:** a parse pushes arrays, maps and extras into the arena it is given, and keeps strings without escapes as slices of the
  input; a failed alternative of a union leaves what it pushed (arenas have no commit).
- **A parse that fails** returns false and writes a `SmithProtocolError`: `reason` (missing, wrong type, bad value, no alternative, arena
  full), the innermost object `type` and member `field` (schema names), and the byte `offset` of the value.
- **Unions the schema does not tell apart** (DAP's `RestartArguments`, MCP's `ServerResult` and `PrimitiveSchemaDefinition`): the same document
  is valid for several alternatives, so what is read may be another alternative than the one written. The generated tests know which types hold
  one (`ambiguous` in the generator) and read the value once first; it must then come back as itself.

## The generated tests

For each generated type, a property test on keys: `random` a value, `write` it, parse the tree, `equal` the value to the one read, and
`write` the one read again and compare the text. For each struct with required members: the document without each in turn is refused,
with reason Missing and that member named. They run with `smith_tests`, 100 keys each (500 under the sanitizers by hand: `./build/tests --seeds 500`),
in three builds: `-O0` under AddressSanitizer and UBSan, `-O2`, and `-O2 -DNDEBUG`, with `-Wconversion -Wshadow -Werror`.

## Numbers

Measured on an Apple M-series Mac with the full smith of this tree.

| | DAP v1.71.0 | MCP 2026-07-28 |
| --- | --- | --- |
| definitions in the schema | 192 | 155 |
| generated types (with arrays, maps, enums, unions) | 294 | 209 |
| lines: header / source / tests | 4,197 / 18,510 / 8,351 | 2,966 / 12,493 / 5,941 |
| compile of the source, `-O0 -g` / `-O1 -g` (smith's flags) / `-O2` / asan+ubsan | 0.52 / 1.22 / 1.38 / 0.82 s | 0.28 / 0.88 / 0.79 / 0.56 s |
| object file at `-O2` (`__text`) | 331 KB | 229 KB |
| added to a stripped executable (everything linked, no dead stripping) | 371 KB | 265 KB |

Both together add 637 KB to an executable with the JSON, text, arena, random and protocol libraries (69 KB). The generator itself takes 0.1 s for
both. The tests: 846 run in 0.5 s (`-O2`), 1.0 s (asan), 7.9 s (asan, 500 keys each); `tests/protocol_test.smith` as a whole takes about 14 s with its
four builds. 

Generating every definition of DAP is more than the 30,000 lines and 1 s of compile that the issue's author asked to be told about. With the
roots the server uses (DAP: initialize, launch, attach, configurationDone, set(Function)Breakpoints, continue/next/stepIn/stepOut/pause,
threads, stackTrace, scopes, variables, evaluate, disconnect/terminate and the events initialized, stopped, output, exited, terminated,
continued, thread; MCP: JSON-RPC, discover, tools/list, tools/call, content blocks) the same generator, restricted to the types they reach, makes
DAP 141 types and about 15,300 lines (9,325 of source) and MCP 80 types and about 8,100 lines. That restriction is not in the generator.
