---
type: design
title: Translating a Makefile
description: smith build --dump-config reads GNU make's common subset and writes a build.c in the low-level API, reporting what it cannot read.
tags: [make, dump-config]
---
# Translating a Makefile

**A Makefile is translated into a `build.c` by smith's own reader of GNU make's common subset** (`runtime/make*.c`, issue
#44, [the build file](build-file.md)): `smith build --dump-config` in a folder with a Makefile and no build file
prints it (also beside a `main.smith` or `main.c`: the Makefile is the project's real build), in the low-level API.
An empty `build.c` or `build.smith` counts as none, since the redirect makes it before smith runs. No vendored make (GNU make is GPLv3, and the owner does not want any license forced
on smith). The Makefile is evaluated at translation time, with what a command line, the environment and the platform
can change kept as options and tests in build.c; a construct outside the subset is reported by name, file and line,
never guessed.

Step 2 of the adoption ladder (AGENTS.md, issue #44) should be a command, not a day of hand translation. In a folder
with a `Makefile` (or `GNUmakefile` or `makefile`, the first of them in that order, by exact name) and no `build.c` or
`build.smith`, the implicit build is the Makefile, and `smith build --dump-config > build.c` prints it as a build file.
An empty `build.c` or `build.smith` counts as none, because the shell makes it before smith runs. With a `main.smith`
or `main.c` beside the Makefile the command still translates the Makefile, and `smith build` and the verbs stop and say to
translate it or delete it. The output is the low-level API
(`<smith/build.h>`), nothing new, so it is read, edited and built like any `build.c`.

- **smith's own reader, in smith's C** (`runtime/make*.c`, about 5,000 lines, no malloc, nothing recursive, no vendored
  code). GNU make is GPLv3, which would force its license on smith, and kati is tuned to Android's dialect. smith already
  reads ninja, gzip, xz and tar itself.
- **It reads the Makefile as make does, with the files that are there, at translation time.** Variables (`=`, `:=`,
  `::=`, `?=`, `+=`, `override`, `define`, `${X}`, `$(X:a=b)`), the functions (`wildcard patsubst subst filter filter-out
  addprefix addsuffix basename notdir dir suffix sort foreach if or and strip words word wordlist firstword lastword
  join call shell abspath realpath origin flavor value`), explicit rules, order-only prerequisites, pattern rules and
  static pattern rules, the automatic variables (`$@ $< $^ $+ $? $* $| $(@D) $(@F)`), `@` and `-` and continuation lines in
  recipes, `.PHONY`, `.DEFAULT_GOAL` and the first target, make's built-in C and C++ rules (compile, link, and compile
  and link in one), `include` and `-include`, `ifeq ifneq ifdef ifndef else` with `else ifeq`, `$(MAKE)`, and
  `$(eval ...)` for a line that is only the call (the usual `$(foreach ...,$(eval $(call template,...)))`; the
  reader takes the text after the line that held it). A `$(wildcard)` is the files it matches when the translation is
  written, so adding a file means translating again; the header comment says so.
- **What stays open until build.c runs is a "hole"**, a C expression carried in the text. The platform (`$(shell uname
  -s)`, `uname -m`, `nproc`, `sysctl -n hw.ncpu`, `pwd`) is `b->os`, `b->arch`, `b->cpus` and `b->root`; the project's folder
  (`$(CURDIR)`, `$(abspath x)`) is `b->root`; `$(HOME)` and the like are `smith_env`; and a variable the Makefile never
  assigns, or sets with `?=`, is a command-line knob: `make DEBUG=1` is `smith build -DDEBUG=1`, declared at the top of
  `build()` with `smith_option_value` (the option, else the environment, else the Makefile's default), and listed in
  a comment at the top of the file. A conditional that tests a hole keeps both branches: a variable that differs becomes
  a conditional expression of the two values, and a rule becomes a step that `build()` declares only under the test
  (two recipes for one target under different tests are reported). A hole that is a whole word is split as a shell would
  by a helper (`make_words`) that the file carries only when it needs it. A value that depends on a hole cannot go through
  `patsubst`, `sort` and the like: that is reported.
- **Compiles, links, archives and the rest.** A recipe line that is a compiler (cc, gcc, clang, smith, c++ ...) with `-c`,
  one source and `-o` is a `smith_object` (`-MMD`, `-MP`, `-MF` and `-MT` are dropped: `smith_object` writes the depfile);
  one that links objects the Makefile compiles is a `smith_program`, with the words before the objects as flags and the
  words after as libraries; the compiler is `smith-cc` or `smith-c++`. Any other recipe is a `smith_step` whose inputs
  and output are the rule's prerequisites and target: one simple command is its words, anything with a pipe, a redirect, a
  substitution or a glob, or several lines, is `sh -c` of the lines (`set -e` first; `-` lines end with `|| true`). An
  `ar` is such a step, and the link that names its `.a` gets it as a library. `FORCE` as a prerequisite is `.always`.
  `mkdir -p` rules and lines are dropped: smith makes the folder of an output.
- **Targets.** The default goal (or `.DEFAULT_GOAL`) is `smith_default`, with an alias like `all` replaced by what it groups;
  `clean` is dropped (`smith build -t clean`); a phony target with a recipe is a `smith_task` and one without is a
  `smith_alias`; a phony target named after a verb (`test`, `check`, `fmt`, `lint`, `run`, `docs`, `prove`) is `smith run` when it
  only runs a program a prerequisite makes, and otherwise a task named `make-<verb>` with a comment saying why (and `.test =
  true` for `test`), because build.c may not name a task after a verb. `$(MAKE) goal` is the goal as a prerequisite, and `$(MAKE) -C
  dir goal` reads the Makefile in `dir` too: its targets the goal needs join the graph named `dir/name` and their
  commands run in `(cd 'dir' && ...)`.
- **Readable.** A variable that a recipe uses as whole words becomes a named array (`const char *const *build_warnings =
  ...` for `WARNINGS`), an option is `build_cflags` for `CFLAGS`: `build_` and the name in lower case, as smith's naming
  rules ask (two names that differ only in case get a number), the arrays that name other arrays are defined after
  them, and each declaration has a comment with the Makefile line and name it came from.
- **Clean.** The file passes `smith lint` and `smith fmt --dry-run` with smith's defaults: it is written through
  `smith fmt`, and a file that would not fit a function of 70 lines (`readability-function-size`) is written as a
  function for each run of targets (`part_1`, `part_2`, ...), with the options, the arrays and the objects as `static`
  variables at the top, a list of more than 30 words in pieces (`name = smith_cat(b, name, ...)`), and `build()` calling
  the parts. A single declaration longer than a function (one recipe of a hundred lines) cannot be split, and lint says
  so. `tests/make_test.smith` lints and format-checks every translation.
- **Reported, not guessed**, on standard error as `smith: Makefile:12: not translated: ...` and in a comment at the top of
  the file: `vpath` and `VPATH`, `.SECONDEXPANSION`, double-colon rules, target-specific and pattern-specific variables,
  `$(eval)` in a recipe, `export`, `!=`, a variable on a `$(MAKE)` command line, `$(shell ...)` other than the platform
  (it is run during the translation and frozen at its output, which is the machine's), a `-include` of a Makefile that
  a rule makes (its variables stay empty: it is generated, and not read), a `$(wildcard $(OPTION))` (read with the option at its
  default) and a suffix rule. The translation still exits 0 and the file still builds.
- **Notes**, in the same comment: `-include` of dependency files (`.d`) is dropped, since smith tracks headers itself.
- **Checked** by `tests/make_test.smith` (the units runner, `smith test`): one small Makefile for each construct, the build.c checked for what
  it says, built, and compared with what make makes (the values a program prints, the files a recipe writes, and the
  compile and link commands against `make -n -B`).

How it compares with what the owner wrote by hand for the same projects (the parents of smith c4cd067, game 53cd679, checkers
bb75749 and virtual-cell b233391): the default build has the same compile and link commands (sources, flags,
outputs) as `make -n -B` for smith, checkers, learner and, apart from the meshes below, game. The hand-written files
are better in ways a reader of text cannot be: they declare the files a recipe makes besides its target, write
generated sources from build.c instead of from a recipe run every time, split a script into steps with their own
inputs, and defer (`smith_defer`) what a generated Makefile (game's `build/meshes.mk`, from `-include`) would define.
