---
name: smith-console
description: "Using std::console (import std::console; console::print, print_error, write, write_error, read, read_line, set_handler): stdout, stderr and stdin called directly, typed pieces with no length limit, a closed pipe as a result. Load when code imports std::console or includes <smith/console.h>, or on printing or reading the console in .smith, or capturing what a function prints in a test."
---

# The console: std::console

This skill ships in the smith plugin, from `.agents/skills/smith-console/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there. It uses `smith-format` (the pieces), `smith-result` and `smith-mem`.

`import std::console;` and call it: nothing is passed down from `main`. What a function does to the console is its inferred `console` effect (`smith-lang-effects`), and a test stands in for the console with a handler.

```smith
module greet;

import std::console;
import std::mem;

bool failed(console::Written shown) { // a function of its own reads a result (smith issue #353)
    return shown == .error;
}

void drop(console::LineResult next) {
    if (next == .ok) mem::free(next.ok.text.data); // the caller's memory, from the allocator in force
}

export i32 main() {
    if (failed(console::print("hello, ", 42, "\n"))) return 1; // typed pieces, as format::to; a closed pipe is a value
    drop(console::read_line());                                 // the next line of stdin
    return 0;
}
```

- **`print(pieces...)`** and **`print_error(pieces...)`** write the pieces to stdout or stderr. Each piece is typed as in `format::to` (numbers, `bool`, `char[:]`, strings, structs, `format::hex(...)`): no format string and **no length limit** (a long text is formatted in memory of the allocator in force and given back). They return `console::Written`, a `result(usize, console::Error)`; they are not `[[nodiscard]]`.
- **`write(bytes)`** and **`write_error(bytes)`** take a `const u8 bytes[:]` and write all of it or fail. **`read(buffer)`** fills a `u8 buffer[:]` with what is available (the count, `0` at the end of the input).
- **`read_line()`** returns a `console::LineResult`: a `console::Line` with `text` (no newline, a terminator after it that the count does not count) and `end` (the input was over before any character). The text is the caller's, from the allocator in force. It reads a byte at a time and holds nothing, so `read` and `read_line` mix freely; use `read` for bulk input.
- **Errors are values, misuse is a failure.** `console::Error` is `Closed` (EPIPE, EBADF), `Full` (ENOSPC, EDQUOT) or `Failed`. A `read` into a buffer of no room is a checked failure, never an error value.
- **Test with a fake**: `console::set_handler(&fake.base)` installs a `console::Handler` (C: `SmithConsoleHandler`, `smith_handlers_set_console`) for the thread and returns the previous one; restore it with `defer`. The handler's `write(self, fd, buffer, size)` sees fd 1 and 2, its `read` serves stdin. It catches `printf` and `puts` of other code too. See `lib/std/console_test.smith`.
- **From C**: `<smith/console.h>` has `smith_console_write`, `smith_console_write_error`, `smith_console_read`, `smith_console_line` and `smith_console_set_handler`; the generic `print` is `.smith` only.
