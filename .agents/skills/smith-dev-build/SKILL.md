---
name: smith-dev-build
description: "Building smith itself: from the previous release (scripts/stage0.smith in a clone with no smith), smith build, check, install and recovery, clean, the lag rule for smith's own code, and the route check before and after a refactor. Load before building, installing or recovering smith."
---

# smith-dev-build

Change smith itself with the rules in [smith-dev](../smith-dev/SKILL.md); what the compiler, linker and caches are is in [one-compiler](../../../knowledge/one-compiler.md); why smith is built this way is in [smith-bootstrap](../../../knowledge/smith-bootstrap.md).

smith builds itself with its own `build.c`, **with the smith before it**: the installed one, or in a clone with no smith the release of the newest ancestor commit. `smith.c` is one C23 file that links clang, lld and the tools from our LLVM, and the libraries it uses (`arena`, `contract`, `inflate`, `io`, `random`, `text`) are `.smith` modules that `build.c` compiles to objects with `smith-cc` (the default `CC`); there is no copy of them as C and no build by one `cc` command.

## The lag rule
Code compiled into smith itself (`smith.c`, `runtime/`, `build.c`, those libraries and `lib/trace.c`) can use a language feature or build API call only once it is on main, because the smith that builds your branch is a release of an earlier commit. A branch cannot use, in that code, the feature it adds itself: add the feature, merge it, and use it in the next change. A new library API follows the same rule; `lib/smith/format`, `math`, `autodiff`, `test` and `tests/*.smith` are built by the smith under test and use a feature at once. If a build fails on syntax the branch just added, that is the rule.

## From a clone with no smith
`mkdir -p .smith && clang -std=c23 -x c scripts/stage0.smith -o .smith/stage0-tool && .smith/stage0-tool` (plain C23, no shell; needs `git`, `gh` signed in, `tar`, the network): finds the release `stage0-<commit>` of the newest ancestor of HEAD, downloads and checks it into `.smith/stage0/`, and runs its `smith build` with its `bin/` first on `PATH`; pass `smith build` arguments after it (`.smith/stage0-tool install`). It needs LLVM built already: `.smith/stage0/bin/smith build llvm` (about 9 minutes once, into `~/.cache/smith/llvm/23.1.3-r9`). Without it the default target stops and says so; `llvm`, `enzyme` and `clean` still run. `tests/stage0_test.smith` tests the tool with a stand-in `gh`. A clone older than the retained releases pulls main first; `SMITH_STAGE0_RELEASE=<tag>` picks a release.

## Build, test, install
- `smith build` builds `build/<OS>-<arch>/smith` and the `smith-cc` and `smith-c++` links. `CC` is `smith-cc` from `PATH`; `-DCC=<path>/smith-cc` names another smith (an external clang cannot compile the libraries). If the installed smith is broken, use the recovery line below.
- `include/smith/build.h` and `runtime/build_runtime.c` are embedded in smith with `#embed`.
- `smith fmt` rewrites every C, C++, Objective-C and `.smith` file git sees except what `.clang-format-ignore` names (generated parsers, the fixtures of the tests). `smith check` is the standard check: fmt (dry run), docs, lint (clang-tidy over the compiles in `.smith/build.info`; `build.c` declares the libraries' and tests' sources with `smith_compile_command`; `shim/.clang-tidy` has the C++ glue's rules), test and prove, each reported, and must exit 0 before you finish. `build.c` is formatted and tidied like the rest, so each of its functions is at most 70 lines.
- Before and after a change, run `smith cc --smith-which -c a.c`, `--smith-which a.metal`, `a.metal b.c` (all after `smith cc`) and `smith version`, and compare. A refactor must not change a route, a message, an exit code or an argv.
- `smith test` runs the routing tests against stub compilers (each through `smith cc` and through `smith-cc`), then the `smith build` tests with a real clang; each `tests/*.sh` is a task of its own (`test-<name>`, with `.test`, which `smith test` runs), so they run side by side (skill smith-dev-tests). `smith build test-compare` runs the same invocations through the clang of the LLVM smith links (`-DREAL=` names another) and through smith.
- `smith build install` runs the tests, then copies smith and the `smith-cc` and `smith-c++` links to `$PREFIX/bin` (and removes an old `smith++` there) (`-DPREFIX=...`, default `~/.local/bin`); a build uses it when CC names smith. A broken install breaks every build, smith's included. The way back is `sh scripts/stage0.sh install`. Do it only when the owner asks.
- `smith build clean` removes `build/`. smith keeps its own files in `.smith/`, outside `build/`, so a project's clean cannot delete them while the build runs (`.smith/stage0/` is the downloaded release).
- A fresh `build/` has no `smith-cc` yet, so its first build leaves out the lint objects of `tests/*.smith` (they need the smith under test for their headers); the second build adds them.
- `smith build release` (CI only: the `release` job after install) publishes the built smith as the release `stage0-<commit>` (`smith-<os>-<arch>.tar.xz` and `.sha256`) and prunes old ones; `scripts/release.sh` refuses by hand unless HEAD is main's tip. `tests/release.sh` tests it with a stub `gh`. See [release](../../../knowledge/release.md).
