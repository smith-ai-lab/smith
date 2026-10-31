---
name: smith-lang-c-interop
description: "C into .smith: `import <math.h>;`, `c::printf`, `c::EOF`, `c::WIFEXITED(s)`, `struct c::stat`, `extern` for a symbol no header declares. Load on 'is a C name; write c::', 'is a C macro', 'names a C tag', `#include` in a .smith file, or `import \"h\";`. Calling a smith module from C or C++ is smith-lang-c-headers."
---

# C headers in `.smith`

```smith
import <math.h>;
import <stdio.h>;

f32 length(f32 x, f32 y) {
    return c::sqrtf(x * x + y * y);        // a C name is always written c::name
}

bool at_end(i32 ch) {
    return ch < 0; // EOF is a C macro: c::EOF compiles, and smith lint (smith-preprocessor) reports it
}
```

In C you `#include` and the names are bare; here `import <h>;` (or `import "h";`, looked for beside the file first) includes the header and binds `c`: `c::sqrtf(x)`, `c::EOF`, `c::WIFEXITED(s)`, `struct c::stat`. `import <h>;` with `c::` names, never `#include`, is a rule of `smith-idiom`; a smith library (`import smith::arena;`) is used as a module, not through its C header. `c::` names C only in a file that imports a header; otherwise `c` is any identifier.

## Rules

- **A macro is the preprocessor.** `c::EOF` compiles, but `smith lint` reports it (`smith-preprocessor`): the module that wraps the C API provides what the macro stood for as a function or a constant (`contract::assert`, `math::max(usize)`; `files` writes `errno` as what it is on each platform). Until there is one, a line keeps the macro with `// NOLINT(smith-preprocessor)`.
- **Bare C names are errors** in every `.smith` file, a module or not. An import always puts the header's names under `c::`. Names from `#include`d headers stay bare, as in C, so plain C in a `.smith` file means what it means in a `.c` file; a file with no `module` line lowers nothing else (no prefix, nothing `static`).
- **Inside an attribute** (`[[gnu::format(printf, 1, 2)]]`, `__attribute__((format(printf, 1, 2)))`) a word is the attribute's own and needs no `c::`.
- **A symbol no header declares** (macOS's `<unistd.h>` has no `environ`), or one a project's C file defines, is declared in the module with `extern` and no `export`: `extern char **environ;`, `extern u32 triple(u32 x);`. It keeps its C name, is private and not in the header, and links to the C definition. A header's own variables work with `c::` (`c::optind`, `c::stdout`, `c::errno`). `export extern` is another thing (`smith-lang-exports`).
- **The sized types** (`u32`) are typedefs only in the `.smith` file; a generated header says `uint32_t`. A project's `typedef uint32_t u32;` in a C header is fine in a `.smith` file that includes it.

## Errors

- `` `sqrtf` is a C name; write `c::sqrtf` `` (a function, variable, type, tag or macro)
- `` `struct stat` names a C tag; write `struct c::stat` ``
- `` `EOF` is a C macro; write `c::EOF` ``
- `` `uint64_t` is a C type name; write `u64` `` (likewise `size_t`, `ptrdiff_t`: it names the sized type)

Calling a smith module from C or C++, `__asm__` labels and the header: `smith-lang-c-headers`.
