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

## A module has no warnings

A `.smith` file with a `module` line is compiled with a baseline as errors, after your flags, so no `-Wno-` removes one: lossy implicit conversions (`conversion`, `sign-conversion`, `shorten-64-to-32`, `float-conversion`), `sign-compare`, `parentheses`, `unsequenced`, `implicit-fallthrough`, `return-type`, `return-stack-address`, `dangling`, `incompatible-pointer-types`, `int-conversion`, `cast-qual`, `cast-function-type-strict`, `sizeof-pointer-memaccess`, `sizeof-array-argument`. Each has a written fix (a cast, `[[fallthrough]];`, parentheses, matching types), so none has a disable. A `.c` file and a file with no `module` line keep today's flags. Heuristic diagnostics are yours to opt into in `build::defaults`, by name without `-W`:

```smith fragment
build::defaults(b, &(build::Defaults){
    .warnings = build::list(b, "padded", nullptr),   // smith lint and the editor show it; the build is quiet and passes
    .errors = build::list(b, "shadow", nullptr),     // fails the build, in modules only
});
```

Also in this family: [smith-build-levels](../smith-build-levels/SKILL.md), [smith-build-levels-high](../smith-build-levels-high/SKILL.md), [smith-build-levels-low](../smith-build-levels-low/SKILL.md).
