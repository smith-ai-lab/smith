# smith

A drop-in compiler driver for C, Objective-C, C++ and Metal, written in C. Milestone 0, a routing passthrough, works on
macOS: smith builds itself, checkers, learner and game. Next are contracts checked at compile time, then `.smith` files
whose syntax replaces macros; the order is in [ROADMAP.md](ROADMAP.md).

## Decisions (change one only with the owner)

- **One toolchain: upstream LLVM at one pinned release** for the compiler, clang-format, clang-tidy, clangd, lldb and
  libclang. Today that is Homebrew `llvm` and `lldb` 23.1.3, held with `brew pin`. Not Apple's clang: it exists only on
  Apple platforms, has no clang-tidy, and can be neither pinned nor shipped.
- **`.metal` goes to Apple's `metal`** (`xcrun -sdk macosx metal`, or `$SMITH_METAL`), the only Metal compiler. `.metal`
  and other inputs in one command are refused.
- **C, C++ and Objective-C are a pure passthrough.** smith never adds, removes or reorders flags, so it cannot change a
  build. Defaults (`-std=gnu23`, sanitizers) belong to `.smith` files only. `.smith` inputs are an error for now.
- **`smith++` is smith under another name**, as clang++ is to clang. Compiling is identical; linking C++ needs it.
- **smith `execv`s the compiler** with the compiler's full path as `argv[0]` (gcc finds its files from it), so exit
  status, signals and pipes are the compiler's.
- **libc only, no malloc, builds as C17 and C23** (`NULL`, enum constants, no `constexpr`) until smith compiles itself.
- **Contracts are C23 attributes that smith reads through libclang**, loaded with `dlopen` only when a check runs. Not a
  clang plugin: a plugin's `[[smith::requires(...)]]` receives no arguments, and its `__attribute__` form cannot see the
  parameters (tested on clang 22). An unknown attribute fails `-Werror` builds, so the spelling in `.c` files is open.
- **No Apple `-fbounds-safety`.** UBSan and the arenas' ASan poisoning check bounds at run time; contracts will prove them.

## Commands

- `make` builds `build/<OS>-<arch>/smith` and the `smith++` link; `make test` runs the routing tests against stub
  compilers; `make compare REAL=/opt/homebrew/opt/llvm/bin/clang` runs the same invocations through clang and smith.
- In a project: `make CC=smith METAL=smith` (learner, game), or a `clang` symlink to smith first on PATH (checkers).
- `SMITH_CLANG`, `SMITH_CLANGXX` and `SMITH_METAL` pick the compilers, `SMITH_VERBOSE=1` prints the command, and
  `smith --smith-which <args>` prints the choice without running it.

## How to work

- Run the real thing and report numbers. Say plainly what was not tested.
- `~/Documents/checkers`, `~/Documents/virtual-cell/learner` and `~/Documents/game` are the test beds: read them, build
  them in scratch copies, and change their sources only with the owner.
- Small Conventional Commits.

## Bugs the checker must find (`~/Documents/checkers/checkers.c`)

- `get_user_move` tests `from_char <= 'H'` twice and never `to_char`, so column I reads `cells[row][8]` (line 112).
- At end of input `get_user_move` loops forever: `scanf` returns `EOF` and nothing ends the loop.
- `get_valid_diagonals` and `get_valid_jumps` miss legal moves (`else if`; `col > 1`, `row > 1`, `col > 2` off by one):
  419,348 over 200,000 random boards, and no illegal move generated.
- `apply_follow_up_jumps` recurses, bounded by 12 captures.

## Known gaps

- A response file (`@file`) is opaque, so a `.metal` input inside one goes to clang.
- Apple's `metal` output is not reproducible (two identical builds of learner's shaders differ), so compare shaders by
  the command smith runs, not by bytes.
- LLVM 23's ASan library costs about 40 ms of kernel time per process on macOS (Apple's: 3 to 9 ms), so game's
  sanitized tests, which fork once per test, take 13 s against 2.4 s. Whether LLVM 22 had it is not known.
- Homebrew clang compiles against the Command Line Tools SDK, while learner's and game's lint and clang-tidy use Xcode's.
