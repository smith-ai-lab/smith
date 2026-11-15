# The Smith language

Every feature of Smith, one file each. Anything not in this folder is not in the language.

The files are a tour. Read them in the order below: each one uses only what the files above it introduced, and ends with a link to the next.

- [overview.md](overview.md): what Smith is for, the rules that hold throughout the language, and the four principles its design follows.
- [functions.md](functions.md): typed parameters, a return type, a block whose last expression is its value, named arguments, and defaults.
- [bindings.md](bindings.md): how `x = e` defines an immutable name, once per function.
- [mutable-locals.md](mutable-locals.md): locals that change with `var` and `:=`, and `if` as an expression.
- [structs.md](structs.md): structs with named fields, tuples, and patterns that take them apart.
- [enums.md](enums.md): enums that hold one of their cases, and `match` to take a value apart.
- [traits.md](traits.md): type parameters, traits over one or more types, `impl`, operators as trait functions, and `derive`.
- [numbers.md](numbers.md): literals backed by traits, the numeric types, and integer arithmetic that never overflows silently.
- [lambdas.md](lambdas.md): lambdas, trailing blocks, `with` to pass the rest of a block to a function, and `try` to continue only on success.
- [effects.md](effects.md): effects listed in a signature as permissions, how an effect is declared, how a handler is used and written, and the effects Smith provides.
- [text.md](text.md): where a program starts, `print`, and string literals that hold expressions formatted by the `Fmt` trait.
- [ownership.md](ownership.md): the `Copy` and `Discard` traits, lending and `take`, and values that must be used at most once or exactly once.
- [arrays.md](arrays.md): arrays whose sizes are part of their types, `Fin` indices that need no bounds check, and ranges.
- [for.md](for.md): `for` as an unordered description of an array, reductions, element-wise operators, `@`, and the rules for float arithmetic.
- [loop.md](loop.md): `loop` for steps that run in order, the `Iterate` trait, and `break`.
- [contracts.md](contracts.md): `requires`, `ensures`, and `check`, what the `arith` procedure can prove, termination, and `hole`.
- [storage.md](storage.md): when an array gets a buffer, the `alloc` effect and arenas, recursion without stack overflow, and types that contain themselves.
- [differentiation.md](differentiation.md): `grad` as a compile-time call that returns the gradient of a function, shown by training an MLP.
- [devices.md](devices.md): running the same source on a GPU with `with device(gpu)`.
- [tasks.md](tasks.md): task groups with `spawn` and `join`, async I/O that reads as sequential code, running out of memory, and events.
- [foreign.md](foreign.md): foreign declarations with C signatures, and how system frameworks are reached for native applications.
- [modules.md](modules.md): one file as one module, `pub`, and explicit imports with `use`.
- [projects.md](projects.md): how a folder becomes a project by convention, what `smith new` creates, the standard library, vendored dependencies, build steps and tasks in `project.smith`, and the command line.
- [tests.md](tests.md): tests found by convention, examples and properties driven by a `Key`, and how they run.
- [transformer.md](transformer.md): a decoder-only transformer in under a hundred lines, built from everything before it.
