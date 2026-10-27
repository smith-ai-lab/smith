---
name: smith-lang-named-arguments
description: "Named arguments in .smith: f(a, .name = value), the trailing arguments of a call as the designated initializer of the function's last struct parameter, zero for what a call leaves out, an options struct instead of many parameters. Load when a call writes `.name = value` as an argument, when designing a function with options, or on 'named arguments', 'a positional argument after a named one', 'named arguments fill the last parameter', 'named arguments need a function with a prototype'."
---

# Named arguments

A function with many options takes them as a struct, its **last parameter**. A call fills the struct with `.name = value` arguments, which is C's designated initializer, and **what a call leaves out is zero**:

```smith
module main;

import smith::console;

typedef struct {
    /// seconds, 0 for none
    i32 timeout;
    /// 0 for the usual 3
    i32 retries;
    bool verbose;
} Options;

static i32 connect(i32 port, Options options) {
    i32 retries = options.retries == 0 ? 3 : options.retries;
    return port + options.timeout * retries + (options.verbose ? 1 : 0);
}

i32 main() {
    i32 a = connect(80, .timeout = 5); // retries, verbose: zero
    i32 b = connect(80, .verbose = true, .timeout = 5); // any order
    i32 c = connect(80, (Options){.retries = 1}); // the struct written out still works
    console::print(a, " ", b, " ", c, "\n");
    return 0;
}
```

`connect(80, .timeout = 5)` is lowered to `connect(80, (Options){.timeout = 5})`. `.name = value` is invalid C as an argument, so the C rule holds. Works through a module name too (`options::open(.width = 3)`).

## Rules

- **Zero is the default.** There are no declared defaults: design the struct so zero means the usual (`retries == 0` is 3). A value with no sensible zero is a positional parameter, because it is required.
- **Positional first, then named.** A positional argument after a named one is an error. The named ones fill the function's **last** parameter, so every parameter before it is given positionally; a call that gives fewer or more is an error naming the parameter count.
- **The last parameter is a struct** (a typedef name or `struct Tag`), complete and passed by value. A pointer, an integer or `...` is an error.
- **Not a generic yet.** A generic function (`constexpr type T`, `auto x`) is an error: `is a generic function: it does not take named arguments yet`. When generics take them, the target will be a last parameter written as a concrete struct; an `auto` parameter never, since it takes its type from its argument and `.name` has none.
- **A misspelled name** is the compiler's own error (`field designator 'widht' does not refer to any field in type 'Options'`), at the name.
- **Doc comments** on the fields (`///`) document each option; reflection reads them.
- Only a call of a name (`f(`, `m::f(`) is read. A call through a field or a pointer (`o.f(...)`, `(*p)(...)`) is not.

## Not yet

A trailing lambda after the named arguments (#316), and generic functions.

Overview: `smith-lang`. Reference: [knowledge/named-arguments.md](../../../knowledge/named-arguments.md).
