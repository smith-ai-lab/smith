---
name: smith-idiom-port
description: "The porting checklist from C to idiomatic Smith, step by step with what to count before and after. Load when porting a .c file to .smith or when a reviewed .smith file still has uint32_t, size_t, #include or #define."
---

# Porting checklist

What changed, by rule:
- 1: `uint32_t`, `size_t`, `uint64_t`, `double` became `u32`, `usize`, `u64`, `f64`.
- 2: no `c::` name is left: the C headers it included were smith's libraries, which are modules (`import smith::io;`, `io::Console`, `io::console_printf`, `io::Io`, `io::make`); `<smith/io.h>` is their C API for C callers, and `smith-module-library` reports it in a `.smith` file.
- `main` takes the io (`i32 main(io::Io *io)`): smith makes it and closes it after (smith-io); `i32 main(i32 argc, char **argv)` with `io::make` is still accepted, and is what a renamed `.c` file has.
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

Also in this family: [smith-idiom](../smith-idiom/SKILL.md), [smith-idiom-before](../smith-idiom-before/SKILL.md), [smith-idiom-after](../smith-idiom-after/SKILL.md).
