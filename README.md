# smith

**smith is C minus the footguns, plus compile-time programming.**

One binary: compiler, build, tests, proofs, formatter, linter, debugger and editor support, from one LLVM.

```smith project=first run prove test
// stats.smith
module stats;

import smith::contract;

export i32 largest(i32 xs[:]) {
    contract::requires(countof(xs) > 0);
    i32 best = xs[0];
    for (usize i = 1; i < countof(xs); i++) {
        contract::invariant(1 <= i && i <= countof(xs));
        best = xs[i] > best ? xs[i] : best;
    }
    return best;
}

export typeof(x) clamp(auto x, typeof(x) low, typeof(x) high) {
    contract::requires(low <= high);
    typeof(x) result = x < low ? low : x > high ? high : x;
    contract::ensures(low <= result && result <= high);
    return result;
}
```

```smith project=first
// main.smith
module main;

import smith::console;
import stats;

i32 main() {
    i32 raw[_] = {-3, 1, 9, 4};
    i32 readings[:] = raw;
    i32 top = stats::largest(readings[1:]); // a cut: three elements, shared with raw
    console::print("largest ", top, ", clamped ", stats::clamp(top, 0, 5), "\n");
    return 0;
}
```

```sh
smith run      # builds and runs it: largest 9, clamped 5
smith prove    # proves every contract for every input: ok
```

```output project=first
largest 9, clamped 5
```

## What Smith gives you that C doesn't

### Modules, not headers

```c
// wheel.h: a header, an include guard, and every name in the program is global
#ifndef WHEEL_H
#define WHEEL_H
unsigned spokes(unsigned turns);
#endif
```

```smith
module wheel;

// private: no other file can name it
static u32 twice(u32 n) {
    return n * 2;
}

// callers write wheel::spokes(3)
export u32 spokes(u32 turns) {
    return twice(turns) + 1;
}
```

`import wheel;` brings the module in, and C's own headers come in as `import <math.h>;` with their names under `c::`.
[smith-lang-modules](.agents/skills/smith-lang-modules/SKILL.md), [smith-lang-c-interop](.agents/skills/smith-lang-c-interop/SKILL.md).

### Sized types

```smith
module pixel;

export u32 pack(u8 r, u8 g, u8 b) {
    return (u32)r << 16 | (u32)g << 8 | b;
}
```

`u8` to `u64`, `i8` to `i64`, `usize`, `isize`, `f16`, `f32` and `f64` need no include.
[smith-lang-sized-types](.agents/skills/smith-lang-sized-types/SKILL.md).

### Slices: a pointer that knows its length

```c
#include <stddef.h>
int sum(const int *xs, size_t n); // the length is a second argument, and nothing checks it
```

```smith project=slices run
module main;

import smith::console;

i32 sum(const i32 xs[:]) {
    i32 total = 0;
    for (i32 x : xs) { // each element, with no bounds to get wrong
        total += x;
    }
    return total;
}

i32 main() {
    i32 raw[_] = {1, 2, 3, 4, 5};
    i32 all[:] = raw;
    console::print("all ", sum(all), ", middle ", sum(all[1:4]), "\n");
    return 0;
}
```

```output project=slices
all 15, middle 9
```

In a module an array's brackets say what it is: `i32 xs[3]` has the size written, `i32 xs[_] = {1, 2, 3}` takes it from the initializer (`char s[_] = "abc"`, `i32 m[_][3]` too), and `i32 xs[:]` is a slice; a bare `[]` is an error that names these ([smith-lang-arrays](.agents/skills/smith-lang-arrays/SKILL.md)).

`for (i32 x : xs)` visits each element, `for (usize i, i32 x : xs)` adds its position, and `i32 *p` points at the element; C's `for` stays for a step or a reverse walk ([smith-lang-for](.agents/skills/smith-lang-for/SKILL.md)). A cut `xs[a:b]` is a view of the same elements. `const i32 xs[:]` makes the elements read-only, and `u8 dst[restrict :]`
is C's `restrict`. A parameter written `i32 xs[4]` keeps its 4, where C throws it away:

```smith
module shape;

export i32 corner(i32 xs[4]) {
    return xs[3]; // a call with an array of another size is an error
}
```

`smith lint` reports the unsafe C that remains in a module: `setjmp` and `longjmp` (they skip every `defer`), `alloca` and a stack array sized at run time, and an array parameter it cannot check (`i32 a[n]`, `i32 a[const 4]`), each with what to write instead (`smith-unsafe`, [smith-check-lints](.agents/skills/smith-check-lints/SKILL.md)):

```smith
module rows;

export i32 first(i32 xs[:]) { // not `usize n, i32 xs[n]`: a slice carries its count
    return xs[0];
}
```

[smith-lang-arrays](.agents/skills/smith-lang-arrays/SKILL.md),
[smith-lang-slice-qualifiers](.agents/skills/smith-lang-slice-qualifiers/SKILL.md).

A pointer to a slice lets a function advance the caller's, as a cursor does:

```smith
module cursor;

export void skip(const char (*text)[:], usize n) {
    *text = (*text)[n:]; // advances the caller's slice
}
```

[smith-lang-slice-pointers](.agents/skills/smith-lang-slice-pointers/SKILL.md).

One library does what `strcmp`, `memchr` and `memset` did for every element type: `smith::slice` compares, searches, fills and reverses any slice, with no allocation, and `smith::ascii` tests one character with no locale. `slice::find` returns an `option(usize)` where C returned a sentinel, and an index or its absence travels as one value:

```smith
module header;

import smith::ascii;
import smith::slice;

export bool is_setting(const char line[:]) {
    const char prefix[:] = "#";
    return countof(line) > 0 && ascii::alpha(line[0]) && !slice::starts_with(line, prefix);
}

export usize body_length(const char line[:]) {
    // A cut of line: nothing is copied.
    const char body[:] = slice::trim(line, ascii::space);
    return countof(body);
}
```

[smith-slice](.agents/skills/smith-slice/SKILL.md), [smith-ascii](.agents/skills/smith-ascii/SKILL.md).

### Typed memory, not `memcpy`

```c
#include <stddef.h>
#include <string.h>
void copy(float *dst, const float *src, size_t n) {
    memcpy(dst, src, n * sizeof(float)); // the type and the length travel apart from the data
}
```

```smith project=memcopy run
module main;

import smith::console;
import smith::mem;

i32 main() {
    f32 raw[4] = {1.5f, 2.5f, 3.5f, 4.5f};
    f32 copy[4] = {};
    f32 from[:] = raw;
    f32 to[:] = copy;
    mem::copy(to, from); // two slices of one type and one length, checked
    bool same = mem::equal(to, from);
    mem::zero(to[0:2]);
    console::print("same ", same, ", copy[1] ", copy[1], ", copy[3] ", copy[3], "\n");
    return 0;
}
```

```output project=memcopy
same true, copy[1] 0, copy[3] 4.5
```

`mem::copy`, `mem::move`, `mem::zero`, `mem::fill`, `mem::equal`, `mem::bytes(&x)` and `mem::view(bytes, f32)` take slices, so a length that does not match is a checked failure, and they lower to the C functions. In a file with a `module` line the raw `memcpy`, `memmove`, `memset` and `memcmp` are a `smith-mem` finding that names the replacement (`smith lint --fix` applies the mechanical ones); a function that implements a wrapper says `contract::trusted()`. A `.c` file is untouched. [smith-mem](.agents/skills/smith-mem/SKILL.md).

### No preprocessor

```c
#define COUNT 4
#if defined(__aarch64__)
#define LANES 4
#else
#define LANES 2
#endif
```

```smith
module platform;

import target;

constexpr usize count = 4; // `#define count 4` in a .smith file is a lint error
constexpr usize lanes = target::arch == target::aarch64 ? 4 : 2;

u32 lane_bytes() {
    if constexpr (target::arch == target::aarch64) { // the branch not taken is dropped
        return 16;
    } else {
        return 8;
    }
}
```

[smith-lang-target](.agents/skills/smith-lang-target/SKILL.md).

### Compile-time programming

A function runs in the compiler, and its result is a constant or a table baked into the binary:

```smith
module tables;

static u32 triangle(u32 n) {
    return n * (n + 1) / 2;
}
constexpr u32 t100 = triangle(100);
static_assert(t100 == 5050);

struct Table {
    u32 v[8];
};
static struct Table squares() {
    struct Table t = {};
    for (u32 i = 0; i < 8; i++)
        t.v[i] = i * i;
    return t;
}
constexpr struct Table table = squares(); // a lookup table, baked into the binary
```

[smith-lang-constexpr](.agents/skills/smith-lang-constexpr/SKILL.md).

One function prints any struct, read at compile time:

```smith project=reflect run
module main;

import smith::console;

struct Particle {
    f64 mass;
    i32 id;
    f64 charge;
};

void show(constexpr type T, T *x) {
    for constexpr (usize i = 0; i < countof(fields(T)); i++)
        console::print(fields(T)[i].name, " = ", x->[fields(T)[i]], "\n");
}

i32 main() {
    struct Particle p = {1.5, 7, -0.25};
    show(struct Particle, &p);
    return 0;
}
```

```output project=reflect
mass = 1.5
id = 7
charge = -0.25
```

[smith-lang-reflection](.agents/skills/smith-lang-reflection/SKILL.md).

A pack is the arguments of a call, each with its own type; `if constexpr` picks the code per type, and `twice(args)...` writes one copy per argument:

```smith project=pack run
// meta.smith
module meta;

export typeof(x) twice(auto x) {
    return x + x;
}

export f64 mean(auto... args) {
    f64 total = 0;
    for constexpr (usize i = 0; i < countof(args); i++) {
        if constexpr (is_integer(typeof(args[i])) || is_float(typeof(args[i]))) {
            total += (f64)args[i];
        } else {
            static_assert(false, "mean: no rule for this type");
        }
    }
    return total / (f64)countof(args);
}

export f64 total_of_doubles(auto... args) {
    f64 doubled[_] = {(f64)twice(args)...}; // one element per argument
    f64 total = 0;
    for (usize i = 0; i < countof(args); i++)
        total += doubled[i];
    return total;
}
```

```smith project=pack
// main.smith
module main;

import meta;
import smith::console;

i32 main() {
    console::print(meta::mean(1, 2.5f, (i64)3, 3.5), "\n");
    console::print(meta::total_of_doubles(1, 2.5f, (i64)3), "\n");
    return 0;
}
```

```output project=pack
2.5
13
```

A type with no rule is an error at the call that passed it, not a crash at run time:

```smith project=packerror base=pack rejects
module main;

import meta;

i32 main() {
    return (i32)meta::mean(1, "two");
}
```

```output project=packerror
static assertion failed: mean: no rule for this type
in instance meta::mean(int, char *), requested from here
```

[smith-lang-rest](.agents/skills/smith-lang-rest/SKILL.md), [smith-lang-generics](.agents/skills/smith-lang-generics/SKILL.md).

`auto` also stands for the element of a slice, a pointer or an array: the parameter keeps its shape and each call
writes its own copy, and `typeof(xs[0])` names the element in the rest of the signature:

```smith
module search;

export usize find(const auto xs[:], typeof(xs[0]) v) { // find(ints, 3) and find(floats, 2.5f)
    for (usize i = 0; i < countof(xs); i++)
        if (xs[i] == v) return i;
    return countof(xs);
}

export typeof(*p) first(const auto *p) { // `auto *p` and `auto xs[4]` work the same way
    return *p;
}
```

A concept stands where `auto` does there too (`const arithmetic xs[:]`, `integer *p`): the element must be of it, and a call that gets it wrong is an error at the argument ([smith-lang-concepts](.agents/skills/smith-lang-concepts/SKILL.md)).

[smith-lang-generics](.agents/skills/smith-lang-generics/SKILL.md).

Types are values too, so a function over types runs in the compiler like any other:

```smith
module widths;

static type wider(type a, type b) {
    return sizeof(a) >= sizeof(b) ? a : b; // runs in the compiler
}

constexpr type big = wider(u8, i64);
static_assert(sizeof(big) == 8);
```

[smith-lang-type-values](.agents/skills/smith-lang-type-values/SKILL.md).

### Contracts, and a proof that refuses a call

A contract is a call that stops the program when false, and `smith prove` proves it for every input, as the first program
shows. It also proves what C never checks: here `copy` takes `restrict` slices, and the call in `shift_in_place` hands it
two that overlap.

```smith project=bytes refuse
module bytes;

import smith::contract;

export void copy(u8 dst[restrict :], const u8 src[const restrict :]) {
    contract::requires(countof(dst) >= countof(src));
    for (usize i = 0; i < countof(src); i++) {
        contract::invariant(i <= countof(src));
        dst[i] = src[i];
    }
}

export void shift_in_place(u8 buf[:]) {
    contract::requires(countof(buf) == 5);
    copy(buf[1:5], buf[0:4]); // refused at this call
}
```

```output project=bytes
FAIL  bytes::shift_in_place
the call of bytes::copy passes `dst` (restrict) and `src` slices or ranges that may overlap
```

<!--
```smith project=bytes
module main;

import bytes;

i32 main() {
    u8 raw[5] = {1, 2, 3, 4, 5};
    bytes::shift_in_place(raw);
    return 0;
}
```
-->

[smith-contract](.agents/skills/smith-contract/SKILL.md), [smith-prove](.agents/skills/smith-prove/SKILL.md).

#### A proof removes the check

Every index and cut of a slice is checked in every build, and only a proof takes one away: `smith prove` writes the
checks it proved can never fail to `.smith/proved/<source>.pts`, and a `release` or `fast` build leaves exactly those
out. Debug keeps every check, and so does a source edited since the proof.

```smith
module first;

import smith::contract;

export i32 head(const i32 xs[:]) {
    contract::requires(countof(xs) >= 1); // every caller is proved to meet it
    contract::ensures(true);              // asks for the proof of `xs[0]`'s check
    return xs[0];                         // proved: gone from `smith build release`
}
```

[smith-prove](.agents/skills/smith-prove/SKILL.md), [smith-lang-arrays](.agents/skills/smith-lang-arrays/SKILL.md).

### Tests whose signature says what they touch

```smith project=first
// tests/stats_test.smith
module stats_test;

import smith::random;
import smith::test;
import stats;

export void test_clamp_cuts_a_big_value() { // (void): pure, it can touch nothing
    test::check_equal(stats::clamp(9, 0, 5), 5);
}

export void test_clamp_always_lands_inside(random::Key key) { // a property test: 100 random keys
    i32 x = (i32)random::uniform_int64_sample(key, (random::UniformInt64){.low = -1000, .high = 1000});
    i32 result = stats::clamp(x, 0, 5);
    test::check(0 <= result && result <= 5);
}
```

An `(io::Io *io)` test gets a real io in a scratch folder of its own. [smith-test](.agents/skills/smith-test/SKILL.md).

### Run only the tests a change can affect

```sh
smith test --changed    # the tests this diff reaches; the rest are "unchanged"
```

The first run builds the tests (and the program they start) with line counters and runs every test once. Each test then keeps a record in `.smith/records` of the functions it ran, by hash of their source, so an end-to-end test that runs your command line program is selected exactly when a function it ran changed, with no git history, no CI and nothing declared. A test with no record, a failed one and one whose record no longer matches run again, besides the tests whose imports changed; a plain `smith test` is unchanged. [smith-run-test](.agents/skills/smith-run-test/SKILL.md).

### Capabilities and formatting

A function's effects are inferred from its body and its callees, so a function that prints says so and a pure one cannot.
`format` writes its pieces by their types, with no format string to disagree with them.

```smith
module report;

import smith::console;

// it prints: its effects include the console
export void report(i32 top) {
    console::print("largest ", top, "\n");
}

// it cannot: it is pure
export i32 twice(i32 x) {
    return x * 2;
}
```

[smith-io](.agents/skills/smith-io/SKILL.md), [smith-format](.agents/skills/smith-format/SKILL.md).

### Gradients, no framework

```smith project=gradient run
module main;

import smith::autodiff;
import smith::console;

struct Line {
    f64 m;
    f64 b;
};

f64 loss(struct Line *line, f64 x, f64 y) {
    f64 error = line->m * x + line->b - y;
    return error * error;
}

i32 main() {
    struct Line line = {2, 1};
    struct Line g = autodiff::gradient(loss, &line, 3.0, 5.0);
    console::print("d/dm = ", g.m, ", d/db = ", g.b, "\n");
    return 0;
}
```

```output project=gradient
d/dm = 12, d/db = 4
```

[smith-autodiff](.agents/skills/smith-autodiff/SKILL.md).

## A module has no warnings

In a module (a `.smith` file with a `module` line) every warning of a fixed baseline is an error, and each has a written way to say you meant it, so there is no disable comment: a lossy implicit conversion, `if (x = y)`, a case that falls through without `[[fallthrough]];`, a cast that drops `const`, a missing return. A `.c` file keeps its flags. Other checks (`padded`, `shadow`) are yours to opt into with `.warnings` or `.errors` in `build::defaults`.

```smith project=constcast rejects
module main;

i32 main() {
    const char *name = "smith";
    char *copy = (char *)name;
    return copy[0];
}
```

```output project=constcast
cast from 'const char *' to 'char *' drops const qualifier
```

[smith-build-warnings](.agents/skills/smith-build-warnings/SKILL.md), [module-diagnostics](knowledge/module-diagnostics.md).

## Port your C project, step by step

Every step is optional and leaves the project building. The project here is a Makefile, `main.c`, `greet.c` and `greet.h`.

<!--
```make project=port file=Makefile port
CC ?= cc
CFLAGS ?= -O2 -Wall

app: main.o greet.o
	$(CC) $(CFLAGS) -o app main.o greet.o

%.o: %.c greet.h
	$(CC) $(CFLAGS) -c $< -o $@
```

```c project=port file=greet.h
#ifndef GREET_H
#define GREET_H
void greet(const char *name);
#endif
```

```c project=port file=greet.c
#include <stdio.h>
#include "greet.h"

void greet(const char *name) { printf("hello, %s\n", name); }
```

```c project=port file=main.c
#include "greet.h"

int main(void) {
    greet("smith");
    return 0;
}
```
-->

1. **Compile with smith**, nothing else changes: `CC=smith-cc make`. [smith-adopt](.agents/skills/smith-adopt/SKILL.md)
2. **Translate the Makefile**: `smith build --dump-config > build.c`, then `smith build`. Read the top of `build.c` for what
   it could not translate. [smith-adopt-build](.agents/skills/smith-adopt-build/SKILL.md)
3. **Check**: `smith check` is formatting, lint, the docs check, the tests and the proofs, in one command.
   [smith-check](.agents/skills/smith-check/SKILL.md)
4. **Rename one file**: `git mv greet.c greet.smith`. A `.smith` file with no `module` line means what it means in C.
5. **Make it a module**: `greet.h` and `greet.c` become one file, and callers `import greet;`.
   [smith-adopt-prove](.agents/skills/smith-adopt-prove/SKILL.md)

```smith project=ported run
module greet;

import smith::console;

export void greet(const char name[:]) {
    console::print("hello, ", name, "\n");
}
```

```output project=ported
hello, smith
```

<!--
```smith project=ported
module main;

import greet;

i32 main() {
    greet::greet("smith");
    return 0;
}
```
-->

6. **Then, when each helps**: slices instead of pointer and length, contracts, and `smith prove`. When every file is a
   `.smith`, delete `build.c`: `main.smith` at the root is built by convention.

## The toolchain

| Verb | What it does | Skill |
|---|---|---|
| `smith new` | writes a project: a program, a test and what agents need | [smith-new](.agents/skills/smith-new/SKILL.md) |
| `smith run`, `smith test` | run the program, or its tests (`smith test --changed`: only those a diff reaches) | [smith-run-test](.agents/skills/smith-run-test/SKILL.md) |
| `smith build` | builds by convention, or runs `build.c` / `build.smith` | [smith-build](.agents/skills/smith-build/SKILL.md) |
| `smith check` | formatting, lint, docs, tests and proofs before a commit | [smith-check](.agents/skills/smith-check/SKILL.md) |
| `smith fmt`, `smith lint` | clang-format and clang-tidy, with smith's defaults | [smith-check](.agents/skills/smith-check/SKILL.md) |
| `smith prove` | proves contracts with Alive2, Bitwuzla and Z3 | [smith-prove](.agents/skills/smith-prove/SKILL.md) |
| `smith cc`, `smith c++` | clang, in smith's process; `smith-cc` is the link for `CC=` | [smith-cc](.agents/skills/smith-cc/SKILL.md) |
| `smith dbg`, `smith dap` | lldb, and lldb-dap for editors | [smith-debug](.agents/skills/smith-debug/SKILL.md) |
| `smith mcp` | the debugger as tools for an agent | [smith-mcp](.agents/skills/smith-mcp/SKILL.md) |
| `smith lsp` | clangd, for C, C++ and `.smith` | [smith-lsp](.agents/skills/smith-lsp/SKILL.md) |
| `smith docs` | checks `AGENTS.md`, `knowledge/` and the skills | [smith-docs](.agents/skills/smith-docs/SKILL.md) |
| `smith fetch`, `smith cache` | packages, by URL and hash, and the caches | [smith-packages](.agents/skills/smith-packages/SKILL.md) |

The libraries are [arena](.agents/skills/smith-arena/SKILL.md), [io](.agents/skills/smith-io/SKILL.md),
[format](.agents/skills/smith-format/SKILL.md), [text](.agents/skills/smith-text/SKILL.md),
[slice](.agents/skills/smith-slice/SKILL.md), [ascii](.agents/skills/smith-ascii/SKILL.md),
[json](.agents/skills/smith-json/SKILL.md), [random](.agents/skills/smith-random/SKILL.md),
[math](.agents/skills/smith-math/SKILL.md), [test](.agents/skills/smith-test/SKILL.md),
[contract](.agents/skills/smith-contract/SKILL.md) and [autodiff](.agents/skills/smith-autodiff/SKILL.md): no malloc, no
global state, and a project compiles the ones it uses from source. Start with the [smith skill](.agents/skills/smith/SKILL.md),
the map.

## Install

smith is private and pre-release, and runs on macOS on Apple silicon only. You need Xcode or the Command Line Tools, `git`
and `gh` signed in. A clone builds with the release of its newest ancestor commit, which `scripts/stage0.c` (plain C23, built with Apple's clang) downloads
(`stage0-<commit>`, checksum checked):

```sh
git clone git@github.com:adam-r-kowalski/smith && cd smith
mkdir -p .smith && clang -std=c23 -x c scripts/stage0.c -o .smith/stage0-tool
.smith/stage0-tool llvm      # builds LLVM 23.1.3 (r9) into ~/.cache/smith/llvm, about 9 minutes, once
.smith/stage0-tool install   # builds the full smith and installs smith, smith-cc and smith-c++ in ~/.local/bin
```

Put `~/.local/bin` on your `PATH`. To update, pull and run `smith build install`. smith unpacks clang's builtin headers to
`~/.local/share/smith` on first use and never removes them.

**Editors.** Claude Code: `/plugin marketplace add adam-r-kowalski/smith` then `/plugin install smith@smith` brings the skills
and `smith lsp`. Neovim 0.12: this repository is the plugin ([knowledge/neovim.md](knowledge/neovim.md)). Any editor: run
`smith lsp` as the language server and `smith dap` as the debug adapter.

## Where to read more

[AGENTS.md](AGENTS.md) holds the decisions, one line each; [knowledge/](knowledge/index.md) what smith is, one concept per
file; [.agents/skills/](.agents/skills/) how to do each thing; the [GitHub issues](https://github.com/adam-r-kowalski/smith/issues)
everything planned, and the only place plans live.
