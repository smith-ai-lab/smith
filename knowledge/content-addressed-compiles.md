---
type: design
title: Content-addressed declarations, compiles cached per definition
description: A proposal, not built, to cache the new front end's work per declaration of the checked tree, keyed by a hash that leaves out names as written, formatting and comments, so a reformat or a comment rebuilds nothing, a rename checks nothing again, and a one-function edit rebuilds that function and what uses it; to be built after the switch-over #1631.
tags: [cache, front-end, incremental, proposal]
status: proposal
---
# Content-addressed declarations

**Status: proposal (#2682). Nothing in this document is built, and nothing here is decided.** The owner asked on
2026-11-07: "should we do the unison style approach? Or would that merge conflict with everything?", then "Yeah let's
do the design ticket". The lead's answer, which the owner accepted: design now, build after the switch-over (#1631).
Section 5 lists the decisions the owner makes. The lead asks them on #1406 one at a time; the answers go into
[decisions](decisions.md) and this document then changes from a proposal to a design.

The idea in one paragraph. Today a compile is cached per file: change one byte of a file and the whole file
compiles again, and so does every file that read it. Unison identifies each definition by a hash of its syntax tree,
with the names kept to one side, so a rename or a reformat changes no hash. This proposal does the same for smith's
new front end, with one difference taken from rustc and Zig: a user of a declaration depends on that declaration's
**interface** (its checked signature, its layout, its contract), not on its body. So a change inside one function
body rebuilds that one function, and its callers only when its interface changed.

## 1. What exists today, and what it costs

**The step cache** ([executor](executor.md), `runtime/executor_cache.c`, #621). The unit is one compile step: a command
that writes one object and its depfile. The key is the SHA-256 of the command (the project folder a token), the
compiler's build-id, the environment that moves a compile, and the content of every file the compile read (two
levels, as ccache's direct mode: a manifest of the files read, then a result per variant). A hit restores the object
by a clone. What an edit rebuilds: the whole file, and every file whose compile read it. A comment edit, a reformat
or a rename is a new key. The store is shared by every worktree on the machine (`<cache>/smith/steps`), and CI
saves it on a push to main and restores it everywhere else ([CI caches](ci/caches.md)).

**The effects summaries** (`front/effects_cache.smith`, #2018). The unit is one function of the unoptimized LLVM IR.
The key is an FNV-1a hash of the function's IR: the opcode and line of every instruction, and the callee and global
names. The value is what the body does by itself and the calls it makes. The solver joins the summaries every time,
so a summary stays valid whatever happens to the rest of the build. It lives in memory, for `smith lsp --smith` and a
build's one pass ([effect-inference](effect-inference.md)). Because the line of every instruction is in the hash, a
line added above a function changes that function's summary key.

**The proof cache** (`~/.cache/smith/proofs`, `shim/prove.cpp`, [CI caches](ci/caches.md)). The unit is one function
with a contract. The key is the IR the prover checks, plus the prover and LLVM versions; not the smith binary. A
callee with `smith_ensures` is used through its contract, any other callee is inlined, so the IR the prover checks
already holds what the proof depends on.

**The group's record** (#2994). Asking for a function's key needs its IR, so a group whose proofs were all reused still
paid for compiling every source and loading the IR (49.9 s of a warm `smith prove` of smith itself). A group that ends
with every function reused (so from its second run) keeps `group-<hash>` in the same folder: the files its compiles read (the depfiles of
`-MD`) with a SHA-256 of each, and the lines it printed. The name is a hash of the sources, libraries, flags, budget,
memory and loop options, the prover and LLVM versions, and `smith_import_identity` (the front end, runtime, `lib/std`
and the LLVM pack: the compiler, by content, so a rebuild with the same sources keeps it). The next run reads the files
named, and when every digest holds it prints the record's lines and the summary with `(not compiled)`. A group with
`-v`, `--function`, `--dump`, `--emit-ir`, `--no-cache` or `--front-end=new` (no depfile), one with a failure or a
function proved afresh, and a file that names a macro for the time keep no record and run as
before. The one input a depfile cannot name is a header that a new `-I` folder would now find first; the flags are in
the name.

**The lint cache** (`runtime/lint_cache.c`, [caches](caches.md)). Per file, keyed by the lint identity, the compile
command and the content of the files clang-tidy read.

**The C header cache** (`front/cimport_cache.smith`, #1130). Per header, keyed by the header as written, the flags that
change what clang reads and the clang identity; a hit is checked against every file clang read (size, time, content
hash). It is not wired into `smith cc` yet.

**The old front end.** A `.smith` file is lowered to C by a token hook inside clang ([smith-language](smith-language.md)).
An import reads the imported module's generated C header, and a generic writes one weak hidden instance per list of
types into each file that calls it ([smith-generics](smith-generics.md)). clang has no unit smaller than a file, so the
old front end can never do better than the step cache. This proposal is for the new front end only.

**The new front end** (`front/`, [front-end](front-end.md)). `smith cc --front-end=new` lexes, parses, checks, emits
LLVM IR and writes one object per file (`front/compile.smith`). An import is found by `load_module` and **parsed and
checked from source in every compile that imports it**: compiling `front/types_expr.smith` (6,378 lines) also reads
every module it imports, all the way down. There is no interface file. A generic's instances are made by the checker
(`types::Instance`) and emitted by the importing file as `module::alloc.i32` (#1909). `front/` is 74 files and 57,559
lines; the tree holds 1,565 `.smith` files.

**What it costs: not measured yet.** The design needs four numbers and they are the subject of two open tickets:
- #2677 measures a build of smith cold, warm, as a no-op and after a one-line edit in `front/types_expr.smith`,
  `runtime/executor.c`, `smith.c` and `shim/hook.cpp`, split by kind of step, at `-O1` and `-O0`.
- #2681 adds `--time`, which prints the phases of one `smith cc --front-end=new` compile: lex, parse, fix, import,
  check, effects, emit, LLVM passes, write.

What this design waits for from them, in order: (a) for a one-function edit in a large file, the share of the
compile spent on **imports** (parsing and checking other modules), on **checking and emitting this file**, and on
**LLVM codegen**; (b) the same at `-O0`; (c) the link time of smith, which bounds how many objects a build can afford;
(d) how much of a runner's edit loop is the compile of `.smith` files at all, against C++ shims, `smith.c` and the
link. The known numbers are older and coarser: a warm build of smith on a CI Mac takes 57 s and its tests 41 s
([CI caches](ci/caches.md)), and a runner's edit-and-rebuild loop on `front/` takes 14 to 19 s (#2632). Step 1 of the
build plan (section 6) records the numbers here when both tickets have landed.

## 2. How other systems do it

| System | Unit | What is hashed | What an edit rebuilds |
|---|---|---|---|
| Unison | a definition (term or type); mutually recursive definitions are one cycle, each a member `#hash.n` | the syntax tree after name resolution, with every reference replaced by the referenced definition's hash; names are metadata in the codebase, never hashed. 512-bit SHA3 | nothing for a rename or a reformat. A body change is a new definition with a new hash; `update` makes new versions of every transitive user and type checks them again |
| Zig (incremental, `-fincremental`) | an analysis unit: one declaration's value or type, one function body | each declaration's ZIR (its untyped IR) by a source hash, tracked across edits; analysis records fine dependencies: on a declaration's source hash, its type, its value, a namespace's names, an interned value | the units whose dependencies are outdated, re-analysed in order; a unit whose result is unchanged stops the walk. Zig's own backends and linker patch the output in place, line numbers included |
| rustc (incremental) | a query: `type_of(def)`, `typeck(def)`, `optimized_mir(def)`, ...; objects per codegen unit (a stable partition of a crate's functions) | each query's result by a stable hash (a *fingerprint*); a definition is named by its path (`DefPath`), not its position; spans hashed relative to their enclosing item | the red-green walk: a query whose inputs' fingerprints are unchanged is green and reused without running; one that runs and gives the same fingerprint stops the walk. A codegen unit is compiled again when any function in it changed |
| Go build cache | a package | the action ID: the source files, the compiler, the flags, and for each import the content ID of its **export data** | the edited package; a package that imports it only when its export data (its interface) changed. The link always runs |

What each one teaches smith:
- **Unison** shows that names as written, layout and comments can stay out of every key. It pays for referring to
  dependencies by hash: a body change gives every transitive user a new hash. That is right for a codebase where a
  definition *is* its hash. A compiler that links by symbol name needs only the interface of what it calls.
- **Go** shows the smallest version of the interface rule: a dependent is keyed on the dependency's export data, not on
  its source, so a body edit stops at the edited package. Smith's import model (modules, `pub` exports) maps onto it
  directly, which makes it the first step of the plan.
- **rustc** shows the full version: every result keyed by fingerprints of the results it read, early cutoff when a
  result comes out the same, stable identities by path, spans relative to the item, and codegen units that bound the
  number of objects.
- **Zig** shows the dependency kinds a language with compile-time evaluation and reflection needs (a declaration's
  value, its type, a namespace's names), and that moved line numbers can be patched instead of compiled again.

Sources: Unison, [The big idea](https://www.unison-lang.org/docs/the-big-idea/). Zig, the
[0.14.0 release notes, Incremental Compilation](https://ziglang.org/download/0.14.0/release-notes.html) and
[How Zig incremental compilation is implemented internally](https://ziggit.dev/t/how-zig-incremental-compilation-is-implemented-internally/3543)
(the `src_hash` dependencies and outdated units of `src/InternPool.zig` and `src/Zcu.zig`). rustc, the dev guide's
[Incremental compilation](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html) and
[Incremental compilation in detail](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation-in-detail.html).
Go, [Build and test caching](https://pkg.go.dev/cmd/go#hdr-Build_and_test_caching) and
[`cmd/go/internal/work/buildid.go`](https://go.dev/src/cmd/go/internal/work/buildid.go) (an action ID uses, for each
direct import, the content ID of its export data).

## 3. A proposal for smith

### The unit

A **declaration of the checked tree**, after `@when` has removed what the target does not have (`when::live_module`):
a function, a struct, an enum, a constant, a global, a type function, an `asm` block. Each has a stable **identity**:
its module (from its path, `front/roots.smith`) and its name, plus a number for the few that have no name (an `asm`
block, a `static_assert`) in the order they appear. Two kinds of declaration are made, not written: a **generic
instance** (the generic's identity and its arguments) and a **C declaration** read through `import c` (the header's
identity and the declaration's name).

A file stays the unit of the build graph: `smith build` still runs one compile step per `.smith` file, and the step
cache still serves a file that did not change at all. The declaration store sits inside that step: it makes a compile
of a changed file cheap.

### What the hash covers

Each declaration has three hashes, all SHA-256 over a canonical encoding of the checked tree, with a version:

- **The source hash**: the declaration's tree after name resolution. Resolution (which declaration each name
  means, with no types) runs on every compile of a changed file; it is cheap. The hash covers each reference as the
  referenced declaration's *interface hash*, not its name, and also every type, every literal, the attributes
  (`@inline`, `@c_name`, `@effects`, `@when`'s condition, user attributes), the contract calls, and the `##` doc
  comment *as a separate part* (below). It leaves out the declaration's own name (that is its identity), the names
  of locals as written (a local is its position in the body; parameter names stay in, since a call can name them,
  `f(x, port = 443)`), formatting, `#` comments, and absolute positions (a span is kept relative to the
  declaration's start, so a line added above changes nothing). A reference to the declaration itself, or to another
  member of a cycle of declarations that refer to each other's interfaces (`struct Node:` with a field `?*Node`), is
  hashed as its position in the cycle, as Unison's `#hash.n` members are.
- **The interface hash**: what a user can see, computed from the *checked result*, as Go's export data and rustc's
  fingerprints are. A function's checked signature (parameter and result types, which parameters are `mut`,
  keyword-only parameters and defaults), its declared effect ceiling, its contract (`requires`, `ensures`, `modifies`),
  `pub`; for a struct or an enum its fields or variants with their types, defaults, attributes and layout; for a
  constant its value. Not its name. **And the body**, for a declaration whose body a user reads: an `@inline`
  function, a `const` function evaluated at compile time, a generic (its instances are made from the body), a type
  function.
- **The object key**: the source hash, the interface hashes of everything the declaration depends on (below), the
  build options that move code (the target triple, `-O` level, sanitizers, debug info on or off), the identity of the
  front end, and the identities (symbol names) the declaration defines and refers to. Everything that holds a name,
  the IR, the object and the effects summary, is stored under this key; the checked result, which holds none, is
  stored under the source hash and the dependencies' hashes. So a rename reuses every check, and two declarations
  with the same text share one checked result.

What is never in a hash: the file a declaration is in or its order in the file (only its module), the folder of the
worktree, the time.

`##` docs are a separate part of the source hash. They are read by `f.doc` in reflection and by the editor's hover;
nothing else depends on them. A doc edit then changes only the results that read it: a reflection that reads `.doc`,
and the hover.

### The dependency graph

The checker records, for each declaration, the facts it read and the hash each had, as rustc's queries and Zig's
analysis units do. The kinds:

| Kind | Recorded when | Invalidated when |
|---|---|---|
| `interface(D)` | a call of D, a use of type D, a read of constant D | D's interface hash changes |
| `body(D)` | compile-time evaluation called D, or D is `@inline` and was inlined | D's source hash changes |
| `fields(T)`, `methods(T)`, `parameters(F)` | reflection read them | that list changes |
| `doc(D)` | reflection read `.doc` | D's doc changes |
| `lookup(M, name)` | a name was looked up in module M: a method call's lookup in the type's module, the current module and the built-ins (UFCS), an import's `from ... import`, a name that was *not* found | a declaration of that name appears in M or leaves it |
| `target` | the body reads `target` or has an `if` on it | the triple changes (already in every object key) |
| `header(H, name)` | a C declaration was used | that declaration's cimport record changes |

`lookup` is the one that a design keyed only on hashes would miss. Adding `fn push` to the current module can change
what `xs.push(5)` calls without changing any hash the caller recorded; Zig records the same fact as a dependency on a
namespace's names.

**The walk** is rustc's red-green. A compile of a file parses it (parsing a file is cheap and always runs), computes
each declaration's source hash, and for each declaration:
1. If an entry exists for its source hash and every dependency it recorded still has the hash it recorded, it is
   **green**: its checked result and diagnostics (`c/`) and its IR (`r/`) are read from the store, and nothing runs.
2. Otherwise it is checked again. If its interface hash comes out the same, its users stay green (**early cutoff**):
   a body edit stops here. If not, each user that recorded `interface(D)` is checked again, and so on.

Inferred effects are not in any key that makes code. They decide errors, not code: the effects pass joins the
per-function summaries each build, as it does today, and a summary is keyed by the declaration's object key instead
of a hash of its IR. A summary's call lines become lines relative to the declaration, rebased when a message is
printed, so a moved function keeps its summary.

### Where results are stored, and sharing across worktrees

One store per machine, `<cache>/smith/decls/<front identity>/`, beside `steps/`, under the same rules as every other
cache ([caches](caches.md)): an entry unused for 31 days goes, `SMITH_CACHE_MAX` caps the bytes, `smith cache` shows
it and `smith cache clean decls` empties it. Three parts:
- `c/<xx>/<check key>` (the source hash and the hashes of the dependencies it recorded): the checked form of one
  declaration (the binary tree format of `front/cimport_cache.smith`, extended), its warnings, and its dependencies
  with the hashes it saw.
- `r/<xx>/<object key>`: what holds names: its LLVM IR as bitcode and its effects summary (and, under D2 (b) or (c),
  its object).
- `m/<xx>/<module identity>`: the **module interface**: for each declaration of a module, its identity, source hash,
  interface hash and the key of its result. An importer reads this and never parses the imported module's source
  when its interface is current.

The **front identity** is the hash of the front end's own sources (`front/`) and the LLVM pack, as the lint identity
is the hash of the checks' sources (#2236), not the smith binary's build-id. So a change to `runtime/`, `lib/` or
`smith.c` keeps the store. Writes are a temporary file and a rename, as the step cache's are, so builds in several
worktrees and CI jobs share one store safely. No key holds a path: a module is named from its root, and debug info
names files relative to the project, as `-ffile-prefix-map` does for C today (#1351). CI saves the store with the
step cache on a push to main and every other job restores it ([CI caches](ci/caches.md)).

### LLVM codegen and objects

The store holds each function's IR. What turns it into objects is the decision of section 5 (D2). The options:

- **(a) One object per file, from cached IR.** A compile joins the IR of the green functions (read from the store)
  with the IR of the ones checked again into one LLVM module, and codegen runs over the whole module. Checking and
  emitting are per declaration; codegen is per file. The build graph does not change. At `-O0` codegen is LLVM's fast
  instruction selector, so this may already be most of the win; #2677's split of a compile into phases says.
- **(b) One object per function**, cached in the store. Codegen runs only for what changed. The cost: a file's
  compile has many outputs, which the manifest cannot name in advance, and the link reads one object per function of
  the program (thousands for smith). A file's output becomes an archive of its functions' objects, linked whole
  (`--whole-archive`, `-force_load`), so the graph still has one output per compile.
- **(c) Codegen units**, rustc's: a file's functions are split into a stable number of buckets by a hash of their
  identity (for example one bucket for every 16 functions), each bucket one object in the file's archive. An edit
  compiles one bucket again; the link reads tens of objects per file at most.

Whatever the choice, only dev builds (`-O0`) use per-declaration codegen. A release build (`-O1` and up) keeps one
module per file, so inlining across functions of a file still happens, and is cached per file by the step cache as
today. The owner's direction (#2677, 2026-11-07): "we want to use unoptimized as much as possible, and then only when
we're creating the final release do we want to use optimized."

A generic instance is a declaration of its own in the store, keyed by the generic's interface hash (which holds its
body) and its arguments' interface hashes. It is emitted once per build, not once per using file, with
`linkonce_odr` linkage so a duplicate is still safe at the link.

### smith lsp

`smith lsp --smith` reads the same store. On each change it parses the file (always), computes source hashes, and
checks again only the declarations that are not green: a keystroke inside one function checks that function, and its
users only when its interface changed. Imports come from module interfaces in the store, not from parsing their
sources. Diagnostics are kept per declaration, so the reply for an unchanged declaration costs nothing. An unsaved
buffer's results are kept in the server's memory and never written to the store (D8): the store holds what a build
could make.

## 4. Interactions

- **Generics and type functions.** An instance is a declaration keyed by its arguments (above). A type function's
  result (`fn Pair(T: type) -> type:`) is an instance too: `Pair(i32)` is keyed by `Pair`'s body and `i32`. An edit to
  `list.push`'s body compiles each instance of `push` once and none of the 40 files that call it, unless the edit
  changes the signature. Inferred parameters (`fn sum[T: num](xs: []T) -> T:`) are resolved before the instance key is
  made.
- **Compile-time evaluation and reflection.** A `const` value computed by calling functions records `body(D)` for each
  function the evaluator ran, so a change in one of them computes the constant again, and an unchanged result stops
  there (early cutoff). `fields(T)` records `fields(T)`; an unrolled `for f in fields(T):` depends on the field list,
  not on T's methods. `f.doc` records `doc`. `static_assert` is a declaration with no output, whose result (pass or
  the error) is stored like any other.
- **Effects and ceilings.** A declared ceiling (`@pure`, `@effects(...)`, a parameter's ceiling) is part of the
  interface: a caller that passes a function to a ceilinged parameter is checked against it. Inferred sets are not in
  any object key; the solver runs over the summaries each build (above). An effect error is a diagnostic of the
  build, not of one declaration, so it is never served from the store: the solver is cheap (summaries, not IR).
- **Affine types.** Whether a type is affine is an attribute, so it is part of the type's interface: a type that
  becomes affine checks every user again. The check of a body (`front/affine.smith`) is per function, so it is part of
  the function's result.
- **C header import.** A header's declarations are hashed one by one, by their cimport record (the normalized form the
  reader returns, not the header's text), in the module interface of the header's import. An edit to a header that
  changes one declaration rebuilds the users of that declaration only; a comment in a header rebuilds nothing. The
  flags that change what clang reads stay in the header's key, as `front/cimport_cache.smith` has them. `.c`, `.cpp`
  and `.m` files are compiled by clang, which has no unit smaller than a file: they stay on the step cache.
- **Autodiff.** Enzyme differentiates whole functions at the link, with full LTO and no passes
  ([autodiff](autodiff.md)). A program that uses `std::autodiff` still gets per-declaration checking and IR from the
  store, but its link joins all of its bitcode and runs Enzyme every time. A later step could cache a derivative keyed
  by the bodies of the functions it reached; not in this plan.
- **GPU kernels.** `.metal` files go to Apple's `metal`, per file, on the step cache; nothing changes. If the new front
  end emits kernels later, a kernel is a declaration like a function, and the metallib stays one per file.
- **smith prove.** The proof cache is already keyed by the IR the prover checks. With the store, an unchanged
  function's IR is the same bytes, so its proof is a hit with no other change. The IR a proof checks inlines callees
  that have no `ensures`, so a proof depends on their bodies: it records `body(D)` for each inlined callee and
  `interface(D)` for each callee used through its contract. Since a function's IR is no longer made again when only
  lines moved, proofs stop being re-run for line moves as long as the prover's IR has no debug locations in its key
  (to check in step 9).
- **Sanitizers.** The sanitizer flags are build options, in every object key: a sanitized build and a plain one have
  separate entries, as clang's module cache keeps them apart today ([caches](caches.md)).
- **Debug info.** Every instruction carries a `DILocation` with an absolute line and column (`front/emit_debug.smith`),
  and failure reports read them through the symbolizer ([failure-reports-and-tests](failure-reports-and-tests.md)), so
  wrong lines are wrong failure reports. With spans relative to the declaration, a function below an edit keeps its
  source hash while its absolute lines move. Three ways to keep debug info correct (D3):
  - (a) Put the declaration's start line in the object key. Always correct; every function below an edit in the same
    file compiles again (checking is still reused, since the source hash is unchanged).
  - (b) Store the IR with lines relative to the declaration and rebase it when it is read: the start line is added to
    every location before codegen. Checking and emitting are reused; codegen runs again for the moved functions only.
    With option (a) of D2 (codegen per file) it costs nothing extra, since the file's codegen runs anyway.
  - (c) Patch the object's line table in place, as Zig's own linker does. The least work per edit; it needs a
    DWARF line program per function that smith writes and edits itself, a large piece of work against LLVM's output.
  Column numbers move only when the line itself changed, so they need nothing.

## 5. Decisions for the owner

Each is asked on #1406 one at a time, with this text. The recommendation is the lead's proposal for the owner, not a
decision.

- **D1. What a user depends on: the interface or the whole hash.**
  (a) Unison's Merkle hashes: a declaration's hash includes the hashes of everything it refers to, so any body change
  changes every transitive user. (b) Interface hashes with early cutoff (Go, rustc, Zig): a user depends on what it
  can see; a body edit stops at the edited function. **Recommend (b).** (a) makes a one-function edit rebuild every
  caller up the tree, which is the cost the ticket wants gone; smith links by name, so it does not need hashes as
  identities.
- **D2. The unit of codegen and of objects in a dev build.**
  (a) One object per file from cached per-declaration IR. (b) One object per function, a file's output an archive.
  (c) Stable codegen units of about 16 functions each. **Recommend (a) first, then (c) only if #2677 and step 4's
  numbers show codegen is still most of a one-function edit at `-O0`.** (a) changes nothing in the build graph or the
  link; (b) multiplies the link's inputs by tens of thousands; (c) bounds that and is the next step if needed.
- **D3. How debug info stays correct when lines move.** (a) The start line in the object key. (b) Relative lines,
  rebased when the IR is read. (c) Patch line tables in place. **Recommend (b).** It is correct, reuses checking and
  emitting, and costs nothing extra under D2 (a); (c) is a project of its own for a small gain over (b).
- **D4. Symbols and renames.** (a) Symbols stay the source names (`module.function`): a rename reuses the checking,
  effects and proofs of the renamed function and its callers, but their objects are made again. (b) Private functions
  get symbols named by their hash, with the source name only in debug info, so a rename of a private function
  rebuilds no object. **Recommend (a).** A rename in a file-based language edits every call site's text anyway, codegen
  at `-O0` is the cheap part, and hash-named symbols make every backtrace and `nm` harder to read.
- **D5. Where the store lives and what its identity is.** (a) One store per machine, keyed by the front end's sources
  and the LLVM pack (as the lint identity is). (b) Keyed by the smith binary's build-id, as the step cache is. (c) One
  store per project. **Recommend (a).** With (b), every merge to main (a new release, which builds smith) empties the
  store for everyone; with (a), only a change to `front/` or the pack does, and worktrees and CI share entries.
- **D6. CI and the store.** (a) Saved with the step cache on a push to main, restored by every job, as the step cache
  is. (b) Not shared: each job starts empty. **Recommend (a).** The store is keyed by content and front identity,
  so a restored entry is as good as a made one; the cap (`SMITH_CACHE_MAX`) keeps the saved folder small.
- **D7. When it is built.** (a) After the switch-over #1631, with the old pipeline still in the tree. (b) After the
  old pipeline is deleted (the ticket after #1631). (c) Now, beside the syntax tickets. **Recommend (a).** The work
  touches the checker and the emitter, which every syntax ticket edits now (the reason the owner asked about merge
  conflicts); after #1631 the tree compiles through the new front end, so the store pays off from its first step,
  and nothing in it touches the old pipeline.
- **D8. Does `smith lsp` write to the store?** (a) Never: an unsaved buffer's results stay in the server's memory.
  (b) Yes, everything it checks. **Recommend (a).** Every keystroke would write entries no build will read; reading the
  store is what makes the editor fast.
- **D9. Optimized builds.** (a) Only `-O0` dev builds use per-declaration codegen; `-O1` and the release keep one
  module per file on the step cache. (b) Every build. **Recommend (a)**, after the owner's direction above: inlining
  across a file's functions needs the file's module whole, and the release is built rarely.
- **D10. Generic instances.** (a) Each instance once per build, in the store, `linkonce_odr`. (b) As today: each using
  file emits its own weak copy. **Recommend (a).** It is the use case of a generic used in 40 files: one compile of the
  instance instead of 40, and one copy in the binary.
- **D11. Cached proofs.** The proof cache exists already. Should a proof that ran out of its budget be stored? (a)
  Store proofs and counterexamples, never a timeout. (b) Store a timeout too. **Recommend (a).** The budget is CPU time
  ([proof-budget](proof-budget.md)), but a timeout says nothing about the function, and a proof that once timed out on
  a loaded machine must be allowed to pass.

## 6. A build plan

Each step is one ticket and one pull request, in this order, starting after #1631. Each says what can be measured
when it lands; the plan's measure is `smith build --time` (#2681) on the use cases below, on smith's own tree.

1. **The baseline.** Record here, from #2677 and #2681: for each use case below, the time of the compile and of the
   rebuild, split into imports, checking, emitting, codegen and the link. No code. Measured: the numbers the other
   steps are judged against.
2. **Identities and source hashes.** The checker computes each declaration's identity and source hash;
   `smith cc --front-end=new --dump-hashes` prints them. Tests: a rename of a local, a reformat, a `#` comment and a
   line added above leave every hash the same; a changed field, type or literal changes it; a `##` doc changes only
   the doc part. Measured: the cost of hashing per file (should be under a few per cent of a compile).
3. **Module interfaces.** A compile writes its module's interface (`m/`) and an importer reads it instead of parsing
   and checking the imported source. This is Go's model, and the first step that saves time. Measured: a one-function
   edit in `front/types_expr.smith` with warm interfaces against step 1; the import phase should go to near zero.
4. **Per-declaration results and the walk.** Dependencies recorded by the checker (the table above, `lookup`
   included), results in `r/`, the red-green walk with early cutoff, IR joined per file for codegen (D2 (a)).
   Measured: the one-function edit, the comment edit and the struct field change against step 3; the share left in
   codegen, which decides whether step 6 is needed.
5. **Relative lines** (D3 (b)): spans and debug locations relative to the declaration, rebased when read. Tests: a
   breakpoint and a failure report name the right line after a line is added above (`smith dbg`, a failing test's
   report). Measured: the comment edit and a line added at the top of a file.
6. **Codegen units** (D2 (c)), only if step 4 shows codegen is most of what is left. A file's output an archive of
   unit objects. Measured: the one-function edit, and the link time of smith against step 1.
7. **Generic instances in the store** (D10). Measured: an edit to a generic's body used in 40 files; the binary's size.
8. **Effects summaries from the store**, keyed by the object key, with relative call lines. Measured: the effects
   phase of a no-change compile and of `smith lsp`.
9. **Proofs.** Check that the prover's key is unchanged for an unchanged function after a line move (or make it so),
   and the timeout rule (D11). Measured: `smith prove` after a comment edit in a proved module.
10. **`smith lsp` on the store** (D8). Measured: the time from a keystroke to diagnostics in `front/types_expr.smith`.
11. **CI** (D6): the store in the saved cache. Measured: the hit rate and the compile time of a pull request's build
    that touches one `front/` file.
12. **C declarations one by one** (once `front/cimport_cache.smith` is wired into `smith cc`). Measured: an edit to
    one declaration of a project header used by many files.

**The use cases, and what each should rebuild when the plan is done:**
- A one-function edit in `front/types_expr.smith`: that function's check, emit and codegen (or its unit's); its
  callers only if its signature changed.
- A rename across a module: no checking (names are not hashed); the objects of the renamed declaration and of the
  functions that call it (D4).
- A comment edit: nothing, but a rebase of the moved functions' lines (D3).
- A change of a struct's field: the struct, and every declaration that recorded `interface` or `fields` of it.
- A generic instantiated in 40 files: each changed instance once; none of the 40 files unless the signature changed.
- `smith lsp` on each keystroke: the edited declaration, from interfaces in the store.
- Two worktrees: one store, shared entries, no path in any key.
- CI: the store restored from main's last push; a pull request compiles what it changed.
