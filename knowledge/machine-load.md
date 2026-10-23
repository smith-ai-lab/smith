---
type: design
title: What loads a Mac that builds with smith, and what a test may not depend on
description: Spotlight indexes only a non-hidden build/ folder, Gatekeeper assesses each new executable once, and a test's limits must count CPU time, never wall time.
tags: [macos, tests, load]
---
# What loads a Mac that builds with smith, and what a test may not depend on

**Spotlight.** Measured on macOS 26: `mds` indexes a project's `build/` (75 of its `.o` files were in the index) and does not index a hidden folder (`.smith/`, `~/.cache/smith*`) or the temporary folder (`$TMPDIR`, under `/var/folders`), where smith keeps its scratch and tests' projects. An empty `.metadata_never_index` file in a subfolder is **not** honored (nor `.metadata_never_index_unless_rootfs`, nor the backup-exclusion attribute): files under each were indexed within minutes. A folder whose name ends in `.noindex` is honored. So smith's own folders need no mark; a project that does not want `build/` indexed names its output `build.noindex/` in its `build.c`, or the user adds the folder to System Settings → Spotlight → Search Privacy. smith does not rename `build/`.

**Gatekeeper (`syspolicyd`).** Every executable linked for the first time is assessed once when it first runs, about 10 ms of `syspolicyd` CPU for a small program, whatever its path and content (60 distinct programs: 0.6 s; 60 copies of one: 0.7 s; a rerun: 0). A 235 MB `smith` costs about 0.1 to 0.2 s and 2 s of wall time the first time. A test run therefore pays per executable it builds, and smith reuses an executable that did not change. The user can exempt the programs a terminal or an agent app starts: System Settings → Privacy & Security → Developer Tools, add the app. smith does not change system settings.

**What a test may not depend on.** Under a load of 100 or more, wall time stretches several-fold, so a limit that is wall time ends a test that did nothing wrong. The prover's budget is CPU time; Z3's own timeout (it has only a wall clock) is therefore set to the process's wall backstop, `20 × budget + 30` s, and `with_z3_cpu_limit` ends it at the CPU budget. A test of a CPU limit passes `--wall-timeout` large enough that only the CPU limit can end it. Both were checked at a load of 130 to 250.
