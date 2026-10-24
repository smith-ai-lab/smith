---
name: smith-dev-build
description: "Building smith itself: the stage0 and the full smith, a fresh clone, smith build, check, install and recovery, clean, and the route check before and after a refactor. Load before building, installing or recovering smith."
---

# smith-dev-build

Change smith itself with the rules in [smith-dev](../smith-dev/SKILL.md); what the compiler, linker and caches are is in [one-compiler](../../../knowledge/one-compiler.md).

smith builds itself with its own `build.c`, so the installed smith must have `smith build`. Pass `-DCC=/opt/homebrew/opt/llvm/bin/clang` to any of these to build smith with Homebrew's clang instead of the installed smith.
## The two smiths, and how to get the full one
smith has clang inside (issue #1): it compiles in its own process, by running clang's own `clang_main` (`shim/clang.cpp`, the only C++ in smith) on the arguments. Two smiths come out of one `smith.c`:
- **The stage0** is `cc -std=gnu23 smith.c -o build/stage0`: no C++, no LLVM, and it runs an external clang (`$SMITH_CLANG`, else Homebrew's). It is all a fresh clone has.
- **The full smith** has `-DSMITH_IN_PROCESS`, the shim, and the archives of our LLVM install linked in with `-Wl,-dead_strip`. `smith build` makes it when the install has `lib/libclangmain.a` (else it makes the stage0); `-DLLVM=` names another install, empty forces the stage0.
From a fresh clone: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0`, then `build/stage0 build llvm` (builds LLVM 23.1.3 into `~/.cache/smith/llvm/23.1.3/install`, about 9 minutes cold, a few seconds when it is built), then `build/stage0 build -DCC=cc` (or `... install -DCC=cc`) links the full smith; set `SMITH_CLANG=/usr/bin/clang` if Homebrew's llvm is not installed, since the stage0 compiles build.c with an external clang. Keep a copy of the stage0 outside `build/` (it survives `smith build clean`) when you work on the full one, and rebuild with the full smith: `cp build/<OS>-<arch>/smith $S/stage1 && $S/stage1 build -DCC=$S/stage1` (a smith compiles smith in its own process).
## Build, test, install
- `smith build` builds `build/<OS>-<arch>/smith` and the `smith-cc` and `smith-c++` links. `CC` is the installed smith, as `smith-cc`, so smith builds smith; `-DCC=/opt/homebrew/opt/llvm/bin/clang` compiles it with Homebrew's clang instead. If the installed smith is broken, use the recovery line below.
- `include/smith/build.h` and `runtime/build_runtime.c` are embedded in smith with `#embed`; keep `cc -std=gnu23 smith.c -o smith` building all of smith (a test checks it).
- `smith fmt` rewrites every C, C++, Objective-C and `.smith` file git sees except what `.clang-format-ignore` names (generated parsers, the fixtures of the tests). `smith check` is the standard check: fmt (dry run), docs, lint (clang-tidy over the compiles in `.smith/build.info`; `build.c` declares the libraries' and tests' sources with `smith_compile_command`; `shim/.clang-tidy` has the C++ glue's rules), test and prove, each reported, and must exit 0 before you finish. They run the tools of the smith that runs them (a full smith: its own LLVM 23; a stage0: Homebrew's). `build.c` is formatted and tidied like the rest, so each of its functions is at most 70 lines.
- Before and after a change, run `smith cc --smith-which -c a.c`, `--smith-which a.metal`, `a.metal b.c` (all after `smith cc`) and `smith version`, and compare. A refactor must not change a route, a message, an exit code or an argv.
- `smith build install` runs the tests, then replaces `~/.local/bin/smith`, which every project's build uses (`-DPREFIX=...` installs elsewhere). Do it only when the owner asks.
- If the installed smith is broken, or in a fresh clone, a C23 compiler and a clang for build.c are all it takes: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc` installs the stage0; add the `build/stage0 build llvm` step before it for the full smith.
- `smith build clean` removes `build/`. smith keeps its own files in `.smith/`, outside `build/`, so a project's clean cannot delete them while the build runs.
- smith builds itself with its own `build.c`, so the installed smith must have `smith build`. `smith build` builds
  `build/<OS>-<arch>/smith` and the `smith-cc` and `smith-c++` links: with the LLVM install of `smith build llvm` (or
  `-DLLVM=`) it links clang in and compiles in its own process, else it builds the stage0, which runs an external
  clang. `smith test` runs the routing tests against stub compilers (each through `smith cc` and through `smith-cc`),
  then the `smith build` tests with a real clang; each `tests/*.sh` is a task of its own (`test-<name>`, with `.test`, which `smith test` runs),
  so they run side by side (skill smith-dev-tests);
  `smith build test-compare` (one of smith's tests, so `smith test` and `smith check` run it) runs the same invocations through the clang of the LLVM smith links (`-DREAL=` names another;
  Homebrew's differs in its version string, so the cases with debug info or bitcode differ against it) and through smith.
- `smith build install` runs the tests, then copies smith and the `smith-cc` and `smith-c++` links to `$PREFIX/bin`
  (and removes an old `smith++` there) (`-DPREFIX=`,
  default `~/.local/bin`), and a build uses it when CC or CXX names smith. A broken install breaks every build,
  smith's included. The way back needs only a C23 compiler and a clang for build.c, and is also the first build of a
  fresh clone: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc` builds
  and installs the stage0 (set `SMITH_CLANG=/usr/bin/clang` if Homebrew's llvm is absent). The `.smith` sources smith's own build compiles are built from their
  committed lowering in `bootstrap/` (`smith build bootstrap` writes it, `check-bootstrap` in `check` keeps it current), so that first build
  needs no knowledge of `.smith` and the protocol generator builds with the stage0. To get the full smith, run
  `build/stage0 build llvm` (about 9 minutes once) and then `build/stage0 build install -DCC=cc`, which links clang in.
  `-DCC=` picks the compiler that builds smith; `smith build clean` removes `build/`.
- `smith build release` (CI only: the `release` job after install) publishes the built smith as the release `stage0-<commit>` (`smith-<os>-<arch>.tar.xz` and `.sha256`) and prunes old ones; `scripts/release.sh` refuses by hand unless HEAD is main's tip. `tests/release.sh` tests it with a stub `gh`. See [release](../../../knowledge/release.md).
