---
type: convention
title: Adopting smith is incremental from both ends
description: From an existing C project use smith cc, translate the Makefile, rename files one at a time, and from a new project follow conventions and generate only the files you change.
tags: [adoption, conventions]
---
# Adopting smith is incremental from both ends

**Adopting smith is incremental from both ends, and every step is optional** (issue #43). `smith cc` never changes a
default; the defaults that exist apply only where a project has no file and runs a verb that uses one.
- **From an existing C project (configuration, as it is):**
  1. **Use `smith cc` instead of clang** (`CC=smith-cc` in a Makefile, where one program path is needed). For C,
     C++ and Objective-C it is a pure passthrough: smith never adds, removes or reorders flags, so the build cannot
     change.
  2. **Translate the Makefile into a build file** (#44): `smith build --dump-config > build.c` in the folder
     writes a `build.c` that describes the same build explicitly and across platforms, in smith's low-level API, so
     it changes nothing either. A folder with a Makefile and no build file never builds by convention, even beside a
     `main.c`: `smith build` and the verbs stop and say to translate it or delete it.
  3. **Rename `.c` files to `.smith` one at a time,** deciding module by module when to use smith's features.
- **From a new project (convention):**
  1. **Follow the conventions and write no files.** There is no build file and no config file: `smith build`, `run`,
     `test`, `fmt`, `lint`, `prove` and `check` do the right thing (C2y with `-std=gnu2y`, for `_Countof`; strict
     warnings; three profiles: debug with sanitizers, release with ThinLTO, fast with `-DNDEBUG` and ThinLTO (every profile keeps
     the contracts and bounds checks); no unity builds, since Clang modules and their cache parse a header once and ThinLTO inlines across files).
  2. **Generate the file you want to change.** `--dump-config` makes the implicit explicit: `smith fmt --dump-config
     > .clang-format`, `smith lint --dump-config > .clang-tidy`, `smith build --dump-config` for the build file.
  3. **Replace the high-level calls with low-level ones,** a piece at a time. A real project needs no more than the
   high level: one `build::app` per program (`.tool` for the ones `smith build` leaves out), `build::defaults` for
   the flags and per-folder flags every program and test shares, and low-level steps only for what is truly custom
   (Metal shaders, data-baking steps); `build::profile_flags`, `build::source_flags`, `build::sources` and
   `build::objects` let those steps start from the conventions (game and learner are the examples).
- **The two ends meet:** a dumped build file and a hand-written one are the same API at different levels.
