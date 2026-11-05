---
name: smith-dev-syntax-modules
description: "New syntax (#1428): imports (`import std.math`, nothing from std without one, `import std.math as m`, `from std.math import sin`, `pub import`), headers as namespaces (`import c <stdio.h>`, `import cpp`, `import objc`, `cstr.of`), `pub`, globals (`let mut` at the top level), `extern`, build.smith and tests. Load when writing imports, globals, build files or tests in the new syntax."
---

# Modules, interop, globals, builds and tests

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Imports

```smith
import std.math                     # binds math, the last part of the path
import std.arena as a               # a different name
from std.math import sin, cos       # names into this module: sin(x), and x.sin()
pub import web.routes               # re-exports the module

import c <stdio.h>                  # binds stdio, the header's file name without .h
import c <sys/stat.h> as st
from c <stdlib.h> import malloc, free
import cpp <vector>
import objc <Foundation/Foundation.h>
```

- A smith module is found by its path (`geometry/shapes.smith` is `geometry.shapes`; there is no `module` line). A header is found on the include path; `c`, `cpp` or `objc` says how to read it.
- Imports are top-level statements, one per line. `as` is required where the default name is not a valid name (`<x-y.h>`); `smith fmt` drops an `as` that repeats the default. `.` is the one separator (`::` is gone).
- Nothing from std is in scope without an import: `import std.mem`, then `mem.alloc(u8, 10)`. A name brought in with `from` belongs to the current module for method calls ([smith-dev-syntax-methods](../smith-dev-syntax-methods/SKILL.md)).

## Headers are namespaces

```smith
let mut info: st.stat = undefined   # a C struct, filled by the call
if st.stat(cstr.of("a.txt"), &info) == 0:   # cstr.of: a str to a NUL-terminated C string
    stdio.printf("%lld bytes\n", info.st_size)
```

There is no global `c` bucket: every name comes through its header's name (or `from`), including names the header reaches through its own includes; one entity seen through two names is one. A `.c` file is C, unchanged.

## pub, globals and extern

```smith
pub const VERSION = "1.4"
let mut requests: u64 = 0           # a module global; pub let mut shares it
extern fn legacy_init(flags: u32) -> i32    # defined in C
extern let mut errno: i32
extern fn printf(f: *u8, ...) -> i32   # symbol = own name; `...` in an `extern fn`, args C scalars; a definition may end in `...` too (functions skill)
```

`pub` makes a declaration visible to other modules and to C (it replaces `export`): functions, types, constants, globals, imports and struct fields ([smith-dev-syntax-privacy](../smith-dev-syntax-privacy/SKILL.md)). A module's generated C header declares its `pub` names, with their doc comments. There is no function-local `static`.

## Build files and tests

```smith
import std.build

pub fn main(b: *mut build.Build):
    build.app(b, entry = "main.smith")
```

`build.smith` is the only build file; a `build.c` is an error that says to rename it and run `smith lint --fix`. A test is a public function named `test_*` in a `_test.smith` file: `geometry/shapes_test.smith` holds `pub fn test_area(): test.check_near(shapes.area(1), 3.14159, 1e-5)`.
