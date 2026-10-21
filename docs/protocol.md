# Protocol code generated from the official schemas

The debugger server of issue #22 (`smith mcp`) speaks two JSON protocols: DAP to `smith dap` (lldb-dap) and MCP to the agent. Neither
is written by hand. `scripts/protocol.c` reads a protocol's JSON Schema and writes C on `<smith/json.h>`; the output is
committed, so a plain `smith build`, a fresh clone and the stage0 never need the network.

## What is where

| Path | What |
| --- | --- |
| `scripts/protocol.c` | the generator, a program of the build like `scripts/pack.c` (built by build.c from `lib/arena.c`, `text.c`, `json.c`) |
| `runtime/protocol/protocol.h`, `protocol.c` | hand-written: the error a parse reports, the types a schema starts from (string, integer, number, boolean, any JSON), the checks the generated code shares |
| `runtime/protocol/dap.h`, `dap.c`, `mcp.h`, `mcp.c` | generated: a struct and four functions per type |
| `runtime/protocol/LICENSE-dap.txt`, `LICENSE-mcp.txt` | the schemas' license texts, copied; the first lines are also in the header of every generated file |
| `tests/protocol/test_protocol_dap.c`, `_mcp.c` | generated tests: a round trip for every type, a refusal for every missing required member |
| `tests/protocol/test_protocol_samples.c`, `protocol_support.[ch]` | hand-written: messages as lldb-dap and an MCP client write them, and what the generated tests share |
| `tests/protocol.sh` | builds and runs all of it in a scratch project under the sanitizers (`smith build test-protocol`) |

The generated code is in `runtime/` because it is smith's own code for `smith mcp`, not a library for other projects
(a library joins `lib/` when two projects need it). Nothing includes it yet: `smith.c` does not link it until the server uses it.

## The schemas

Each is a package pinned by hash in `build.c` (`build_protocols`); both are plain tag archives of the project's repository, because a
package is an archive.

| Protocol | Release | Package | Schema in it |
| --- | --- | --- | --- |
| DAP | v1.71.0 (the newest tag) | `https://github.com/microsoft/debug-adapter-protocol/archive/refs/tags/v1.71.0.tar.gz`, `sha256:2b314bf3f3baec7a86401b8e25c2cedf6d6b9faaf1f1ec42ce844628ea7d1f9d` | `debugAdapterProtocol.json` (JSON Schema draft-04, 192 definitions) |
| MCP | 2026-07-28 (the newest release that is not a draft or a release candidate) | `https://github.com/modelcontextprotocol/modelcontextprotocol/archive/refs/tags/2026-07-28.tar.gz`, `sha256:7df4915b4a5cdc999876f5a80b7af9912bdea21490827224e8c7a10f2c81470a` | `schema/2026-07-28/schema.json` (draft 2020-12, 155 definitions) |

DAP's code license is MIT (Microsoft). MCP's repository is in a licensing transition from MIT to Apache-2.0 (its `LICENSE` says so,
and holds the Apache text), and that file is what is kept. The MCP package is 44 MB extracted, the first time on a machine.

## Regenerating, and how a stale file is found

- `smith build protocol` writes the files from the packages (fetched once per machine into the package cache).
- `smith build check-protocol`, part of `smith check`, runs the generator with `--check`: it writes nothing and fails, naming the file,
  when a committed file differs from what the generator writes now. So a change to the generator, to a pinned hash or to a
  schema's pin fails the check until `smith build protocol` is run and its files committed.
- `-DOFFLINE` with a package missing from the cache skips that protocol and says so (the files cannot be checked without the schema).
- The generator writes a file only when its bytes change, so a build that regenerates nothing rebuilds nothing.

To move to another release: change the URL and hash in `build.c` (`smith fetch <url>` prints the hash line) and the schema's path, run
`smith build protocol`, commit.

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
| `string` / `integer` / `number` / `boolean` | `SmithText` (not copied), `int64_t`, `double`, `bool` |
| `enum` of strings | a C `enum`; a string outside it is refused |
| `_enum`, or `anyOf` of an enum and a string (an open enum) | `{ known; SmithText text; }`: `known` is the enum value or `_Other`, `text` the string as written |
| `anyOf` / `oneOf`, `type: [a, b]` | a tagged union `{ kind; union as; }`. Where every alternative is a struct with a constant string member of one name and a value of its own (`method`, `type`), that member picks the alternative; otherwise the alternatives are tried most specific first (most required members), the ones that take any JSON last |
| `type: [T, "null"]` | `{ bool is_null; T value; }` |
| `array` | `{ const T *items; size_t count; }` |
| `additionalProperties: schema` on an object with no properties | `{ const SmithText *keys; const T *values; size_t count; }` |
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
with reason Missing and that member named. They run with `smith_tests`, 100 keys each (500 under the sanitizers in `tests/protocol.sh`),
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
both. The tests: 846 run in 0.5 s (`-O2`), 1.0 s (asan), 7.9 s (asan, 500 keys each); `tests/protocol.sh` as a whole takes 13.5 s with its
four builds. `check-protocol` takes 0.4 s once the packages are cached.

Generating every definition of DAP is more than the 30,000 lines and 1 s of compile that the issue's author asked to be told about. With the
roots the server uses (DAP: initialize, launch, attach, configurationDone, set(Function)Breakpoints, continue/next/stepIn/stepOut/pause,
threads, stackTrace, scopes, variables, evaluate, disconnect/terminate and the events initialized, stopped, output, exited, terminated,
continued, thread; MCP: JSON-RPC, discover, tools/list, tools/call, content blocks) the same generator, restricted to the types they reach, makes
DAP 141 types and about 15,300 lines (9,325 of source) and MCP 80 types and about 8,100 lines. That restriction is not in the generator.
