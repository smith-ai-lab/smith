---
name: smith-dev-syntax-modules
description: "New syntax (#1428): modules as values (`m := import std.math`, `sin := m.sin`), headers as namespaces (`stdio := import c <stdio.h>`, `import cpp`, `import objc`), `pub` and re-exports, built-in attributes (`@inline`, `@affine`, `@c_name`, `@when(target.os == .macos)`), user attributes (`@attribute` structs), build.smith and tests. Load when writing imports, attributes, build files or tests in the new syntax."
---

# Modules, interop, attributes, builds and tests

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Modules are values

```smith
build := import std.build
m := import std.math
sin := m.sin                     // one name: an alias on its own line, a compile-time constant
arena := import std.arena
Arena := arena.Arena
pub routes := import web.routes  // re-exports the module

stdio := import c <stdio.h>
st := import c <sys/stat.h>
vec := import cpp <vector>
foundation := import objc <Foundation/Foundation.h>
```

- Exactly two forms: `name := import a.b.c` for a smith module, found by its path (`geometry/shapes.smith` is `geometry.shapes`; there is no `module` line), and `name := import c|cpp|objc <path>` for a header on the include path.
- An import is the whole right-hand side of a top-level declaration: one per line, never inside a function, no `import x as y`. `.` is the one separator (`::` is gone).

## Headers are namespaces

```smith
stdio.printf("hello world\n")

mut info: st.stat
if st.stat("a.txt", &info) == 0 {
    stdio.printf("%lld bytes\n", info.st_size)
}
```

There is no global `c` bucket: every name comes through its header's binding, including names the header reaches through its own includes; one entity seen through two bindings is one. The language word says how to read the header (C, C++ or Objective-C); after the import line every namespace is used the same way. A `.c` file is C, unchanged.

**`pub`** makes a declaration visible to other modules and to C (it replaces `export`): functions, types, constants, imports, and struct fields ([smith-dev-syntax-privacy](../smith-dev-syntax-privacy/SKILL.md)). Everything else is private to the module. A module's generated C header declares its `pub` names.

## Build files and tests

```smith
build := import std.build

pub fn main(b: *mut build.Build) {
    build.app(b, entry = "main.smith")
}
```

`build.smith` is the only build file, smith's own included; a `build.c` is an error that says to rename it and run `smith lint --fix`. A test is a public function named `test_*` in a `_test.smith` file: `geometry/shapes_test.smith` holds `pub fn test_area() { test.check_near(shapes.area(1), 3.14159, 1e-5) }`.

## Not settled yet

The other facts of `target` (`target.arch`, sanitizers) and whether `target` needs an import, block and doc comments in the generated header, inline assembly, `volatile` and atomics.
