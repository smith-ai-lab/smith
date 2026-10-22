# smith

smith is a whole C toolchain in one statically linked binary: clang, lld, clang-format, clang-tidy, clangd, lldb and a
prover, all from one LLVM build. Around it are a modern standard library for C, contracts that `smith prove` proves, and
`.smith`, which is C with modules and no macros of its own, lowered to C as clang reads it.

A project's build file is `build.c`, a C program that describes the build. smith runs it and runs the result itself.
There is no make, no ninja and no CMake.

## Status

smith is private and pre-release. It runs on macOS on Apple silicon only. It has no release, no installer, no package
manager formula and no Linux or Windows port. The prover, the debugger and the `.smith` language are young. Plans live in
the [GitHub issues](https://github.com/adam-r-kowalski/smith/issues), and nowhere else. This page describes what smith
does today.

## Install from source

You need a Mac with Apple silicon, Xcode or the Command Line Tools, and a C23 compiler (`cc`). From a clone:

```sh
mkdir -p build
cc -std=gnu23 smith.c -o build/stage0          # the stage0: smith without clang inside
build/stage0 build llvm                        # builds LLVM 23.1.3, about 9 minutes, once
build/stage0 build install -DCC=cc             # builds the full smith and installs it
```

The stage0 runs an external clang. If Homebrew's `llvm` is not installed, set `SMITH_CLANG=/usr/bin/clang`. `build llvm`
puts its work in `~/.cache/smith/llvm/23.1.3-r2` and survives `smith build clean`.

`build install` runs the tests, then copies three files to `~/.local/bin` (`-DPREFIX=` changes the folder):

- `smith`
- `smith-cc` and `smith-c++`, links to smith for a Makefile's `CC` or a `compile_commands.json`

Put `~/.local/bin` on your `PATH`. To update, pull and run `smith build install` with the installed smith. If an install
ever breaks, the commands above rebuild it from the stage0.

On first use smith unpacks clang's builtin headers and runtimes to `~/.local/share/smith`. It never removes that folder.

## A two-minute tour

### A new project

```
$ smith new hello
smith new: hello
  created  main.smith
  created  primes.smith
  created  tests/test_primes.smith
  ...
smith new: hello: 10 created, 0 kept
  git: created a repository on main with one commit
$ cd hello && smith run
[1/6] smith-cc --smith-header primes.smith -o .smith/include/primes.h
...
the first 5 primes add up to 28
hello from hello
```

`smith new` writes a starting point you own, in `.smith` (`--language c` writes C): `main.smith`, the module
`primes.smith` (which `main.smith` and the test import), a test in `tests/`, `.gitignore`, an `AGENTS.md` and a
`knowledge/` folder, a skill for the project's docs, and `.claude/settings.json`. It builds once and commits on `main`.
There is no build file and no config file: smith builds by convention, and uses its own style and checks until you dump
them (`smith fmt --dump-config > .clang-format`, `smith lint --dump-config > .clang-tidy`, `smith build --dump-config`
for the build file). The project is C2y with strict warnings, and has three builds: `smith build` (debug, with
AddressSanitizer and UBSan), `smith build release` (optimized, assertions on) and `smith build fast` (assertions off).

### Test, check, format, lint

`smith test` runs the tests in the files under `tests/`, and a new project has some. A test is a plain function; this
is `tests/test_primes.smith`:

```c
import <smith/test.h>;
import primes;

void test_the_first_five_primes_add_up_to_28(void) {
    c::smith_check_equal_i32(primes::sum(5), 28);
}
```

```
$ smith test
[1/11] find the tests
...
[11/11] .smith/program --run fffffffffffe6c14 --input build/tests --arg build/tests --description test

slowest:
      0.02 s  the_first_five_primes_add_up_to_28
      0.00 s  no_primes_add_up_to_nothing

2 passed, 0 failed in 0.08 s
```

`smith check` is everything that must pass before a commit: formatting, lint, the docs check and the tests, and whatever
else the project adds. It prints the tests' result and, last, clang-tidy's progress:

```
$ smith check
[1/4] .smith/program --run fffffffffffe6c14 --input build/tests --arg build/tests --description test
...
2 passed, 0 failed in 0.08 s
smith docs: 1 concepts, 0 problems
[4/4] smith lint --quiet -p . main.smith primes.smith tests/test_primes.smith
...
```

`smith fmt` rewrites the sources to `.clang-format`, and `smith lint` runs clang-tidy with `.clang-tidy`. With no
arguments they run the project's tasks. With arguments they are clang-format and clang-tidy, unchanged:
`smith fmt -i main.c`.

### Debug, prove, docs

`smith dbg` is lldb, from smith's own LLVM:

```
$ smith dbg -b -o 'b primes.smith:12' -o run -o 'frame variable' -o kill build/debug/hello
Breakpoint 1: where = hello`primes::sum + 436 at primes.smith:12:24, address = 0x0000000100000aec
Process 46027 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
-> 12  	        total += first[i];
(usize) count = 5
(const i32[5]) first = ([0] = 2, [1] = 3, [2] = 5, [3] = 7, [4] = 11)
(i32) total = 0
(usize) i = 0
```

`smith dap` is lldb-dap for editors. Debugging needs Developer mode on the Mac; see the
[smith-debug skill](.agents/skills/smith-debug/SKILL.md).

`smith prove` proves contracts, and until you write a contract there is nothing to prove:

```
$ smith prove
smith: prove: nothing to prove: no function with smith_requires, smith_ensures or smith_invariant
```

A contract is a call, `smith_requires`, `smith_ensures` or `smith_invariant`, that stops
the program when false, like `assert`:

```c
#include <smith/contract.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);
    int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high);
    return result;
}
```

```
$ smith prove clamp.c -- -I .smith/include
1 proved, 0 failed in 0.02 s
```

The prover checks the LLVM IR with Alive2, Bitwuzla and Z3. It also proves that nothing in the function is undefined
behaviour. Each function gets about 10 seconds. `smith prove` with no arguments proves every function with a contract; a function at
the program's edge (io, inline assembly, a library) starts with `smith_trusted();`, which makes its callers use its
contract and lists it as trusted, never as proved.

`smith docs` checks a project's `AGENTS.md`, `knowledge/` and `.agents/skills/` against one format:

```
$ smith docs .
smith docs: 1 concepts, 0 problems
```

`smith check` runs it for you in a project made by `smith new`.

## `.smith` in one example

```c
// foo.smith
module foo;

export u32 twice(u32 x) {
    return x * 2;
}
```

```c
// main.smith
import <stdio.h>;
import foo;

int main(void) {
    u32 n = foo::twice(21);
    c::printf("twice 21 is %u\n", n);
    return 0;
}
```

```
$ smith cc main.smith foo.smith && ./a.out
twice 21 is 42
```

- `import <stdio.h>;` includes a header and puts its names under `c::`, so you write `c::printf`. A bare `printf` is an
  error that says to write `c::printf`.
- `u32` and the other sized types (`u8` to `u64`, `i8` to `i64`, `usize`, `isize`, `f16`, `f32`, `f64`) are typedefs of
  the standard types.
- `module foo;` makes the file a module. `export` makes a declaration public; everything else in the module is private.
  `foo::twice` reaches a member with `::`.

Plain C in a `.smith` file means what it means in a `.c` file. The full language is in
[docs/smith-lang.md](docs/smith-lang.md).

## Editors

**Neovim 0.12.** This repository is the plugin. It adds the `.smith` file type, tree-sitter highlighting and the `smith`
language server and debug adapter configs. It enables nothing you did not ask for.

```lua
vim.pack.add({ "git@github.com:adam-r-kowalski/smith" })
vim.lsp.enable("smith")
```

See [docs/neovim.md](docs/neovim.md).

**Claude Code.** The plugin brings smith's skills and declares `smith lsp` for C, C++, Objective-C and `.smith` files.
`smith new` writes a `.claude/settings.json` that enables it. To add it by hand:

```
/plugin marketplace add adam-r-kowalski/smith
/plugin install smith@smith
```

Start with the [smith skill](.agents/skills/smith/SKILL.md), the map. `smith` must be on the `PATH` of the shell that
starts Claude Code.

**Other editors.** Run `smith lsp` as the language server and `smith dap` as the debug adapter. Do not pass
`--query-driver` to `smith lsp`. VS Code, Helix, Zed and Emacs settings are in the
[smith-check](.agents/skills/smith-check/SKILL.md) and [smith-debug](.agents/skills/smith-debug/SKILL.md) skills.

## The libraries

They are plain C with standard types, no malloc, and no global state. A project compiles the ones it wants from source
with its own flags (`smith_library` in `build.c`). A library joins smith when two projects need the same thing.

- [arena](.agents/skills/smith-arena/SKILL.md): memory without malloc.
- [io](.agents/skills/smith-io/SKILL.md): the one way a program touches the world: console, files, clock, processes, the
  network. A fake io is there for tests.
- [test](.agents/skills/smith-test/SKILL.md): checks that print both values, parallel workers, property tests on keys.
- [contract](.agents/skills/smith-contract/SKILL.md): `smith_requires`, `smith_ensures`, `smith_invariant` and `smith_assert`.
- [math](.agents/skills/smith-math/SKILL.md): `is_finite`, `sqrt`, `abs`, and the largest integer, infinity and NaN of a type.
- [random](.agents/skills/smith-random/SKILL.md): random numbers from keys, so a run can be replayed.
- [text](.agents/skills/smith-text/SKILL.md): lines, words and numbers in memory.
- [json](.agents/skills/smith-json/SKILL.md): a pull parser, an arena tree and a bounded writer.

`<smith/http.h>`, a small HTTPS client for downloading a file, and `<smith/trace.h>`, stack traces for failure reports,
are in `include/smith` and have no skill of their own.

## Where to read more

- [AGENTS.md](AGENTS.md): the decisions, and why each was made.
- [docs/](docs/): [build-c.md](docs/build-c.md) (the build file), [smith-lang.md](docs/smith-lang.md),
  [neovim.md](docs/neovim.md) and [debugging.md](docs/debugging.md).
- [.agents/skills/](.agents/skills/): short guides to each verb and library.
- [GitHub issues](https://github.com/adam-r-kowalski/smith/issues): everything planned, and the only place plans live.
