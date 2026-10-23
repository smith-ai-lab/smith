---
name: smith-idiom
description: "Idiomatic Smith: how to write, edit, review or port a .smith file so it is not C renamed. Load whenever you write, edit, review or port a .smith file, on 'idiomatic', 'C renamed', 'port this C to smith', 'convert to .smith', or when a .smith file has uint32_t, size_t, #include, #define or smith_arena_push(&a, sizeof(T), alignof(T)) in it."
---

# Write Smith, not C renamed

This skill ships in the smith plugin, from `.agents/skills/smith-idiom/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The language is `smith-lang`; this skill is what to write in it.

A `.smith` file is C, so a renamed `.c` file compiles and means what it meant. That is the first step of a port and never the last: a module that still has the standard C types, an `#include`, a `#define` or a `sizeof`/`alignof` passed to a function is not done. The libraries `lib/smith/arena.smith`, `random.smith`, `text.smith` and `build.smith` in smith's repository are the reference; read one before writing a module.

## The rules

Each rule says what enforces it today. "Review" means nothing checks it: you and the reviewer do, with the counts below.

1. **Sized types.** `u8 u16 u32 u64`, `i8 i16 i32 i64`, `usize`, `isize`, `f32`, `f64`, `bool`; `char` stays `char` (`smith-lang-sized-types`).
   - Not `uint32_t`, `size_t`, `ptrdiff_t`, `unsigned`, `float`, `double`.
   - Enforced by: `smith lint` and the editor, through the clang-tidy check `smith-sized-types`, which reports each C spelling (`uint64_t`, `size_t`, `unsigned long`, `double`, `int`...) with the sized type to write; `char` stays, and `main` is `i32 main(i32 argc, char *argv[])`, with no exception. The compiler accepts both spellings, so a `.c` file renamed to `.smith` with `int main(int argc, char **argv)` still builds and the check reports it, since they are the same types.
2. **Import C headers, never include them.** `import <math.h>;` and write `c::sqrt(x)`, `c::EOF`, `struct c::stat` (`smith-lang-c-interop`).
   - A `c::` name that is a C **macro** (`c::assert`, `c::SIZE_MAX`, `c::isfinite`, `c::INFINITY`) is the preprocessor too. The module that wraps the C API provides it as a function or a constant: `contract::assert(c)`, `math::max(usize)`, `math::is_finite(x)`, `math::infinity(f64)`, `math::nan(f32)`, `math::sqrt(x)` and `math::abs(x)` (`smith-contract`, `smith-math`). A macro of a library that has no module yet is written as the function the library has (`__builtin_memset`) or waits for the module. In the macOS SDK `c::memcpy`, `c::memmove`, `c::memset`, `c::snprintf` and `c::vsnprintf` are macros (the fortified versions): write `__builtin_memcpy`, `__builtin_snprintf` and the like; they are pure functions.
   - Enforced by: `smith lint` and the editor, through the clang-tidy check `smith-preprocessor` (rule 5), which reports an `#include` and every `c::` name that is a macro, with what to write; and the compiler for a bare name that an `import`ed header declared (`` `sqrtf` is a C name; write `c::sqrtf` ``).
3. **Modules import modules.** A library that is a module is used with `import smith::arena;` and `arena::push(...)`, not with its C header (`smith-lang-modules`). A project's own `.smith` files are modules too: `module rolls;`, `export`, `import rolls;`, with no header written by hand (`smith build` writes it for C callers).
   - Enforced by: the compiler for private names (`is private; export it`) and for import cycles; `smith lint` and the editor, through `smith-module-library`, for a library that is a module used through its C header (`import <smith/arena.h>;`, `c::smith_arena_push`, `c::SmithArena`, `import <smith/io.h>;`, `c::SmithIo`), with `import smith::arena;` and the module's name to write (a named instance of a generic, `c::smith_check_equal_i32`, also points to the generic: `test::check_equal(a, b)`).
   - **Every `.smith` file is a module**: `module main;` in `main.smith`, `module primes;` in `primes.smith`, `module primes_test;` in `tests/primes_test.smith` (`tests/` is a root of its own; tests are `export`ed, since a private function is static and the runner calls them from outside), `module build;` in `build.smith` (`export void build(build::Build *b)`). Write the type argument of a generic call, `test::check_equal(i32, a, b)`, as the templates do. `smith lint` and the editor, through `smith-module`, name the line to write from the file's path; the compiler never refuses a file with no module line, so renaming a `.c` file to `.smith` still compiles.
4. **Generics where C passes a type as `sizeof` and `alignof`.** `arena::push(&a, Foo)`, not `smith_arena_push(&a, sizeof(Foo), alignof(Foo))`; your own `export T *make(arena::Arena *a, constexpr type T)` when a function of yours takes a type (`smith-lang-generics`).
   - Enforced by: review and the `sizeof/alignof` count. The raw functions (`arena::push_raw`) exist for C, which cannot call a generic.
5. **No `#define`; `constexpr` for constants.** `constexpr usize capacity = 1 << 20;`, and `export constexpr` when others use it. An enumeration or a function does what a macro of several lines did. Checks, contracts and `expect_abort` are functions (rule 8), never macros.
   - **No preprocessor at all:** no `#if` (a fact of the target is `target::`, a choice is `if constexpr`; `arena.smith` shows the way: ordinary code behind a `constexpr bool`, so every branch type-checks), no `#pragma` (a flag a library needs is declared by its row of `smith_libraries` and added by the build), no `#error` (`static_assert(false, ...)`), no `#line`, no `#embed`, no `_Pragma`.
   - Enforced by: `smith lint` and the editor, through the clang-tidy check `smith-preprocessor`, which reports every directive and `_Pragma` of a `.smith` file with what to write instead; it is in smith's default `.clang-tidy` and in `smith check`. A `.c` file is left alone, and a line that must keep one says `// NOLINT(smith-preprocessor)`.
6. **Effects come only through capabilities passed down from `main`.** A function that prints takes an `io::Console *`; one that reads a file takes `io::Files *`; one that needs memory takes an `arena::Arena *`; randomness is a `random::Key` passed in. Take the part of `io::Io` the function needs, not all of it, so the signature says what it can do (`smith-io`).
   - Enforced by: `smith lint` and the editor, through the clang-tidy check `smith-effects`: a raw call such as `c::printf` or `c::fopen` outside a capability's own module is reported, with the capability to take instead; `io::make` outside `main` is reported; so is a capability in static storage. It is in smith's default `.clang-tidy` and in `smith check`.
7. **No globals, no `malloc`.** Memory is an arena passed in (`smith-arena`); state is a value passed in.
   - Enforced by: `smith-effects` reports `malloc` and `free`; review for a mutable global the check does not know about.
8. **Checks, contracts and `expect_abort` are plain functions.** `test::check_equal(a, b)`, `contract::requires(low <= high)`, `test::expect_abort()`: a failure's report is read from the call stack, which is why they are not macros (`smith-test`, `smith-contract`, `smith-trace`). Write a check you need as a function too.
   - Enforced by: the libraries: `smith::test` and `smith::contract` define no macros (the `SMITH_CHECK` macros are gone), and `smith-preprocessor` rejects one in a `.smith` file; review.
9. **Module-local names are short, and `[[smith::c_name]]` only where a C API must stay.** Inside `module smith::arena` the type is `Arena` and the function is `push`; the module's name is the prefix, so `arena_push` in `arena.smith` is wrong. `[[smith::c_name("SmithArena")]] export typedef struct {...} Arena;` is for a library whose C callers must not change; a project's own module gets none.
   - Enforced by: review. `readability-identifier-naming` in `smith lint` judges the case of a name as written, not its length or a repeated prefix.
10. **Loops end.** A loop has a count to bound it, so no `for (;;)`, `while (true)` or `do ... while (1)` outside `main`; `main` owns the program's life and calls the rest each step.
    - Enforced by: `smith lint` and the editor, through `smith-bounded-loop` (also in C).
11. **C23 spellings.** `nullptr`, `bool`, `{}`, `[[noreturn]]`, `[[maybe_unused]]`, `[[nodiscard]]`, never `NULL`, `_Bool`, `<stdbool.h>`, `{0}` or `__attribute__((noreturn))`; a constant is `constexpr`, not a file-scope `const` with a constant initializer.
    - Enforced by: `smith lint` and the editor, through `smith-c23` and `smith-constexpr` (also in C).
12. **Report friction.** When a skill was wrong, unclear or contradicted another, or something was hard to find, say so in one line at the end of your report, as `smith-development` asks of every agent.

## A C module, and the same as Smith

A module that allocates from an arena, draws random numbers and prints through io. First the C, as an agent writes it from the C API:

```c
// rolls.h
#ifndef ROLLS_H
#define ROLLS_H

#include <stddef.h>
#include <stdint.h>

#include <smith/arena.h>
#include <smith/io.h>
#include <smith/random.h>

#define ROLLS_SIDES 6

typedef struct {
    uint32_t *faces;
    size_t count;
} Rolls;

Rolls rolls_make(SmithArena *arena, SmithKey key, size_t count);
double rolls_mean(const Rolls *rolls);
void rolls_print(SmithConsole *console, const Rolls *rolls);

#endif
```

```c
// rolls.c
#include "rolls.h"

#include <stdalign.h>

// count rolls of a die, each from its own key derived from `key`.
Rolls rolls_make(SmithArena *arena, SmithKey key, size_t count) {
    Rolls rolls = {.faces = smith_arena_push_array(arena, sizeof(uint32_t), count, alignof(uint32_t)), .count = count};
    for (size_t i = 0; i < count; i++) {
        SmithKey roll = smith_key_derive(key, i);
        rolls.faces[i] = (uint32_t)smith_uniform_int64_sample(roll, (SmithUniformInt64){.low = 1, .high = ROLLS_SIDES});
    }
    return rolls;
}

double rolls_mean(const Rolls *rolls) {
    uint64_t sum = 0;
    for (size_t i = 0; i < rolls->count; i++) sum += rolls->faces[i];
    return rolls->count == 0 ? 0 : (double)sum / (double)rolls->count;
}

void rolls_print(SmithConsole *console, const Rolls *rolls) {
    for (size_t i = 0; i < rolls->count; i++) smith_console_printf(console, "%u ", rolls->faces[i]);
    smith_console_printf(console, "(mean %.2f)\n", rolls_mean(rolls));
}
```

Renamed to `rolls.smith` this compiles, and it has 13 standard C types, 7 `#include`s and 2 `#define`s. As Smith it is one file with no header:

```smith
// rolls.smith
module rolls;

import smith::arena;
import smith::io;
import smith::random;

constexpr i64 sides = 6;

export typedef struct {
    u32 *faces;
    usize count;
} Rolls;

// count rolls of a die, each from its own key derived from `key`.
export Rolls make(arena::Arena *arena, random::Key key, usize count) {
    Rolls rolls = {.faces = arena::push_array(arena, u32, count), .count = count};
    for (usize i = 0; i < count; i++) {
        random::Key roll = random::key_derive(key, i);
        rolls.faces[i] = (u32)random::uniform_int64_sample(roll, (random::UniformInt64){.low = 1, .high = sides});
    }
    return rolls;
}

export f64 mean(const Rolls *rolls) {
    u64 sum = 0;
    for (usize i = 0; i < rolls->count; i++) sum += rolls->faces[i];
    return rolls->count == 0 ? 0 : (f64)sum / (f64)rolls->count;
}

export void print(io::Console *console, const Rolls *rolls) {
    for (usize i = 0; i < rolls->count; i++) (void)io::console_printf(console, "%u ", rolls->faces[i]);
    (void)io::console_printf(console, "(mean %.2f)\n", mean(rolls));
}
```

```smith
// main.smith
module main;

import rolls;
import smith::io;

i32 main(io::Io *io) {
    rolls::Rolls dice = rolls::make(&io->arena, io->key, 10);
    rolls::print(&io->console, &dice);
    return 0;
}
```

What changed, by rule:
- 1: `uint32_t`, `size_t`, `uint64_t`, `double` became `u32`, `usize`, `u64`, `f64`.
- 2: no `c::` name is left: the C headers it included were smith's libraries, which are modules (`import smith::io;`, `io::Console`, `io::console_printf`, `io::Io`, `io::make`); `<smith/io.h>` is their C API for C callers, and `smith-module-library` reports it in a `.smith` file.
- `main` takes the io (`i32 main(io::Io *io)`): smith makes it and closes it after (smith-io); `i32 main(i32 argc, char *argv[])` with `io::make` is still accepted, and is what a renamed `.c` file has.
- 3: `rolls.h` is gone; `import smith::arena;`, `import smith::io;` and `import smith::random;` replace three includes and their C names (`SmithArena` is `arena::Arena`, `SmithKey` is `random::Key`, `SmithConsole` is `io::Console`). `main` imports `rolls`.
- 4: `smith_arena_push_array(arena, sizeof(uint32_t), count, alignof(uint32_t))` is `arena::push_array(arena, u32, count)`: the size and alignment cannot disagree, and the result is typed.
- 5: `ROLLS_SIDES` is `constexpr i64 sides`.
- 6: `print` takes the `io::Console` and nothing more; `make` takes the arena and the key; only `main` makes the io. `smith lint` would report `c::printf` here.
- 9: `rolls_make` is `rolls::make`; the module name is the prefix.

`smith build` (no build file needed: `main.smith` and `rolls.smith` in a folder) built it with the strict warnings and `smith run` printed `6 1 4 6 3 6 4 5 2 6 (mean 4.30)`; the seed is random unless `SMITH_SEED` is set.

## Porting checklist

Port one file per commit. Run the counts before and after; every line but the last two should reach 0 for a module you ported, and `c:: names` and `raw calls` tell you what is left for the capabilities.

```sh
files=$(git ls-files '*.smith')
n() { cat $files | grep -cE "$1"; }
echo "standard C types  $(n '\<(u?int(8|16|32|64)_t|size_t|ptrdiff_t|unsigned|double|float)\>')"
echo "#include          $(n '^[[:space:]]*#[[:space:]]*include')"
echo "#if               $(n '^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif)\>')"
echo "#define           $(n '^[[:space:]]*#[[:space:]]*define')"
echo "sizeof/alignof    $(n '\<(sizeof|alignof)\>')"
echo "c:: names         $(n 'c::')"
echo "raw calls         $(n 'c::(f?printf|puts|f?open|fread|fwrite|fclose|malloc|calloc|free|memcpy|memset|time|exit|rand)\(')"
```

Set `files` to one path to count one file. The counts are lines, comments included. For the example above `rolls.c` and `rolls.h` together have 13, 7, 1, 2, 1, 0, 0 and the Smith has 0, 0, 0, 0, 0, 0, 0.

- **Standard C types, `#include`, `#define`, `sizeof`/`alignof`: 0.** A `sizeof` that stays measures something that is not a type argument (`sizeof memory` of an array).
- **`#if`: only the questions the compiler answers,** each with a comment saying which.
- **`c::` names** are the calls into C; each should be a library's job (a capability, or `<math.h>` and `<string.h>`, which are pure). `c::` calls into the world outside the program are the next count.
- **Raw calls: 0 outside a capability's module.** `smith lint` reports them; the count is the quick check.
- Then: `smith build`, `smith test` and `smith check` as before the port, and `smith fmt` (`.smith` is formatted as C with `::`).
- Do not claim a module is idiomatic until the counts are in your report.

## Examples in skills are compiled

Every ` ```smith ` block in the plugin's skills is a complete file that `tests/skills.sh` compiles with the smith under test (`smith build test-skills`). A block that is only part of a file is marked ` ```smith fragment ` and skipped; use it sparingly. C is ` ```c `. A block that must fail (an error message's cause) is ` ```text `. `smith-docs` has nothing to do with this; it checks structure and links.
