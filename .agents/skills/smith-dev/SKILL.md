---
name: smith-dev
description: "Start here for any change to smith itself (smith.c, runtime/, shim/, lib/, patches/, build.c, tests/): which small dev skill to load for building, LLVM, tests, verbs, the language, libraries, C style and docs."
---

# smith-dev

[knowledge/decisions.md](../../../knowledge/decisions.md) lists every decision, [knowledge](../../../knowledge/index.md) says what smith is, and GitHub issues hold the planned work. These skills are for changing smith only and are not in the plugin. To use smith as a compiler, load [smith](../smith/SKILL.md); to edit smith's own `build.c`, also load [smith-build](../smith-build/SKILL.md).

| To | Load |
| --- | --- |
| follow the habits every change shares (the smith under test, scratch folders, one `smith check`, the friction report) | [smith-dev-rules](../smith-dev-rules/SKILL.md) |
| build, check, install or recover smith, from the previous release, and the lag rule | [smith-dev-build](../smith-dev-build/SKILL.md) |
| change LLVM, patches, the embedded files, lld, the tool shims or Enzyme | [smith-dev-llvm](../smith-dev-llvm/SKILL.md) |
| add, run or port a test of smith | [smith-dev-tests](../smith-dev-tests/SKILL.md) |
| add or change a verb or the compiler links | [smith-dev-verbs](../smith-dev-verbs/SKILL.md) |
| change what `smith new` writes or the default `.clang-format` and `.clang-tidy` | [smith-dev-new](../smith-dev-new/SKILL.md) |
| change the `.smith` language, its hook or its tools | [smith-dev-language](../smith-dev-language/SKILL.md) |
| add a library or port one to `.smith` | [smith-dev-library](../smith-dev-library/SKILL.md) |
| write smith's own C | [smith-dev-c](../smith-dev-c/SKILL.md) |
| change a concept or a skill | [smith-dev-docs](../smith-dev-docs/SKILL.md) |
| be the owner's one point of contact: lanes, handing out work, decisions | [smith-dev-lead](../smith-dev-lead/SKILL.md) |
| run the factory: roadmap, fundamentals before features, test beds, the owner's Mac | [smith-dev-factory](../smith-dev-factory/SKILL.md) |
| run one lane of tickets as its orchestrator | [smith-dev-orchestrate](../smith-dev-orchestrate/SKILL.md) |
| write a ticket someone with no context can do | [smith-dev-ticket](../smith-dev-ticket/SKILL.md) |
