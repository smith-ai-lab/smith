---
name: smith-effects
description: "The smith-effects clang-tidy check and the model behind it: a program touches the world only through the direct modules or a capability, a raw effect is a call into a system header. Load on a smith-effects finding in smith lint or the editor, or when defining a capability record."
---

# smith-effects: the model, checked

`smith lint` (and the editor) run the clang-tidy check `smith-effects`, which makes the model a rule: a program touches the world only through the direct modules (`console`, `files`, `process`, `net`, `clock`, `env`, `entropy`) or a capability of its own, so a raw call into the system is reported. It is on in smith's default `.clang-tidy` (a project that does not want it runs `smith lint --dump-config > .clang-tidy` and deletes `smith-effects` from `Checks`). Everything is decided per function from the source; no file is exempt.

The rules:
1. **A raw effect is a call into a system header that is not on the pure allow list** (`<string.h>`, `<math.h>`, `<ctype.h>`, `<stdint.h>`, `<stddef.h>`, `<float.h>`, `<limits.h>`, `<stdbit.h>`, `<stdckdint.h>`, `snprintf`, `vsnprintf`, `sscanf`, the `strto*` family, `qsort`, `bsearch`, `abs`, `div`, `__builtin_*`), or an Objective-C message to a system framework's class or object (`[NSFileManager defaultManager]`). The list is `shim/effects.h` in smith; a call the list does not name is reported. In a `.smith` file the finding names the callee as written (`c::fopen`).
2. **Failure is not an effect:** `abort`, `assert`, `__builtin_trap`, `__builtin_unreachable`, `smith_failure`, the contract calls (`contract::assert` included) and the checks of `smith::test` are allowed everywhere.
3. **Raw effects are allowed in a capability's implementation.** A capability is a type marked `[[clang::annotate("smith::capability")]]`: a window or a GPU device, say. (`random::Key` and `arena::Arena` are values, not capabilities.) Its implementation is the module that defines it: the `.smith` module file, or for C the `.c` file beside the header that defines it (`window.c` with `window.h`; `lib/window.c` or `src/window.c` with a header under the `include/` beside them). Every function there, private helpers included, may make raw calls.
4. **An exported function of that module that reaches a raw effect, directly or through the module's own static functions, must take or return a capability** (or a struct holding one). Otherwise make it `static`.
5. **A main-only constructor may be called only inside `main`:** a constructor marked `[[clang::annotate("smith::main_only")]]`. A test makes fakes anywhere.
6. **No capability in static storage:** a global or `static` local of a capability type, a pointer to one, or a struct that holds one.

One option, `smith-effects.CFiles` (default true): `false` makes the check skip `.c` and `.m` files and read `.smith` only (smith's own `.clang-tidy` does this while its C is ported).

**Marking a project's own capability** (a window, a GPU device), in a `.smith` module (a C header is under "From C"):

```smith
module window;

export typedef struct [[clang::annotate("smith::capability")]] {
    void *handle;
} Window;

// Raw calls into the platform go here, in the module, and every exported function takes a Window *.
export bool is_open(const Window *window) {
    return window->handle != nullptr;
}

// A constructor that may be called only in main.
export [[nodiscard, clang::annotate("smith::main_only")]] Window make() {
    return (Window){.handle = nullptr};
}
```

The rest of the program takes a `window::Window *` like any other capability.

**Fixing a finding:**

| Finding | Fix |
| --- | --- |
| `fopen`, `open`, `stat`, `rename`... touch the world | call `files::read_file`, `files::write_file`, `files::open`... |
| `printf`, `puts`, `fputs(stderr)`, `getchar` | call `console::print`, `console::print_error`, `console::read_line` |
| `time`, `clock_gettime`, `sleep` | call `clock::now`, `clock::monotonic`, `clock::sleep` |
| `fork`, `system`, `popen`, `waitpid` | call `process::run`, `process::spawn` |
| `socket`, `connect`, `send` | call `net::connect`, `net::write`, `net::read` |
| `malloc`, `free` | use `arena::push` in `.smith` (`SmithArena *`, `smith_arena_push` in C) |
| `rand`, `arc4random` | take a `random::Key` and derive from it (smith-random), or `entropy::seed()` |
| `getenv` | `env::get("NAME")` |
| `exit` | return the status from `main` |
| an exported function "reaches `X` but takes no capability" | add the capability parameter it needs, or make it `static` |
| a main-only constructor "may be called only in main" | call it from `main`; a test makes a fake instead |
| a capability "cannot be kept in static storage" | pass it as a parameter |
| an Objective-C message to a framework | wrap the framework in a capability's implementation and take that capability |

Also: [smith-console](../smith-console/SKILL.md), [smith-files](../smith-files/SKILL.md), [smith-process](../smith-process/SKILL.md), [smith-net](../smith-net/SKILL.md), [smith-clock](../smith-clock/SKILL.md), [smith-env](../smith-env/SKILL.md), [smith-entropy](../smith-entropy/SKILL.md).
