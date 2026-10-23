---
name: smith-build-warnings
description: "Relaxing one of the conventions' warnings in a build: which -W flag, per folder or per file, without giving the others up. Load on a warning-as-error that the conventions turn on."
---

# Relaxing one of the conventions' warnings

The conventions compile with `-Wall -Wextra -Wpedantic ... -Werror`, then your flags, so a `-Wno-...` in `.flags` (or the folder's list) wins and the rest stay on. Turn off as little as you can: a folder's list for files that break a rule on purpose (a linter's fixtures are empty or odd translation units), the app's `.flags` for a construct the whole program uses (a float in a `static_assert`, which clang calls a GNU constant-folding extension under `-Wpedantic`). The two game needed:

```smith fragment
module build;

import smith::build;

export void build(build::Build *b) {
    build::defaults(b, &(build::Defaults){
        .folders = (const build::Folder[]){{.path = "fixtures", .flags = build::list(b, "-Wno-empty-translation-unit", nullptr)}, {}},
    });
    build::app(b, &(build::App){
        .entry = "main.smith",
        .name = "hello",
        .flags = build::list(b, "-Wno-gnu-folding-constant", nullptr),   // static_assert(0.5f + 0.5f == 1.0f)
    });
}
```

Find the flag's name in the diagnostic: clang ends it with `[-Wname]`, and `-Wno-name` turns it off. Check with `smith build -t commands <target>` that the flag is last on the files you meant, and `-Werror=name` puts one back.

Also in this family: [smith-build-levels](../smith-build-levels/SKILL.md), [smith-build-levels-high](../smith-build-levels-high/SKILL.md), [smith-build-levels-low](../smith-build-levels-low/SKILL.md).
