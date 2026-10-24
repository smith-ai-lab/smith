---
name: smith-effects
description: "The smith-effects clang-tidy check and the model behind it: a program touches the world only through capabilities passed down from main, a raw effect is a call into a system header, io::make is main_only. Load on a smith-effects finding in smith lint or the editor, or when defining a capability record."
---

# smith-effects: the model, checked

`smith lint` (and the editor) run the clang-tidy check `smith-effects`, which makes the model of smith-io-capabilities a rule: a program touches the world only through capabilities passed down from `main`, so a function's signature says what it can do. It is on in smith's default `.clang-tidy` (a project that does not want it runs `smith lint --dump-config > .clang-tidy` and deletes `smith-effects` from `Checks`). Everything is decided per function from the source; no file is exempt.

The rules:
1. **A raw effect is a call into a system header that is not on the pure allow list** (`<string.h>`, `<math.h>`, `<ctype.h>`, `<stdint.h>`, `<stddef.h>`, `<float.h>`, `<limits.h>`, `<stdbit.h>`, `<stdckdint.h>`, `snprintf`, `vsnprintf`, `sscanf`, the `strto*` family, `qsort`, `bsearch`, `abs`, `div`, `__builtin_*`), or an Objective-C message to a system framework's class or object (`[NSFileManager defaultManager]`). The list is `shim/effects.h` in smith; a call the list does not name is reported. In a `.smith` file the finding names the callee as written (`c::fopen`).
2. **Failure is not an effect:** `abort`, `assert`, `__builtin_trap`, `__builtin_unreachable`, `smith_failure`, the contract calls (`contract::assert` included) and the checks of `smith::test` are allowed everywhere.
3. **Raw effects are allowed in a capability's implementation.** A capability is a type marked `[[clang::annotate("smith::capability")]]`: `io::Io`, `io::Console`, `io::Files`, `io::Clock`, `io::Processes`, `io::Net` and `io::Args` (the process's inputs: the live environment), and the handles `io::Op`, `io::Capture`, `io::Child` and `io::Connection`. (`random::Key` and `arena::Arena` are values, not capabilities.) Its implementation is the module that defines it: the `.smith` module file, or for C the `.c` file beside the header that defines it (`io.c` with `io.h`; `lib/io.c` or `src/io.c` with a header under the `include/` beside them); `smith::io` is itself a `.smith` module, so it is its own implementation. Every function there, private helpers included, may make raw calls.
4. **An exported function of that module that reaches a raw effect, directly or through the module's own static functions, must take or return a capability** (or a struct holding one). Otherwise make it `static`.
5. **A main-only constructor may be called only inside `main`:** `io::make`, marked `[[clang::annotate("smith::main_only")]]`. `io::make_test` is not: tests make fakes anywhere. The io form of `main` (`i32 main(io::Io *io)`) is lowered to a `main` that calls `io::make`, so it satisfies the rule; the capability comes from its parameter.
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
| `fopen`, `open`, `stat`, `rename`... touch the world | take an `io::Files *` and call `io::files_read`, `io::files_write`... |
| `printf`, `puts`, `fputs(stderr)`, `getchar` | take an `io::Console *`: `io::console_printf`, `io::console_print`, `io::console_read_line` |
| `time`, `clock_gettime`, `sleep` | take an `io::Clock *`: `io::clock_now`, `io::clock_wall`, `io::clock_sleep` |
| `fork`, `system`, `popen`, `waitpid` | take an `io::Processes *`: `io::processes_run`, `io::processes_spawn` |
| `socket`, `connect`, `send` | take an `io::Net *`: `io::net_connect`, `io::net_send`, `io::net_receive` |
| `malloc`, `free` | take a `SmithArena *`: `smith_arena_push`, or `arena::push` in `.smith` |
| `rand`, `arc4random` | take a `SmithKey` and derive from it (smith-random) |
| `getenv` | `io::args_env(&io.args, "NAME")` |
| `exit` | return the status from `main` |
| an exported function "reaches `X` but takes no capability" | add the capability parameter it needs, or make it `static` |
| `io::make` "may be called only in main" | take the `io::Io *` (or the part) as a parameter; a test uses `io::make_test` |
| a capability "cannot be kept in static storage" | pass it as a parameter from `main` |
| an Objective-C message to a framework | wrap the framework in a capability's implementation and take that capability |

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-fake](../smith-io-fake/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).
