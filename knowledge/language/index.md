# The Smith language

Every feature of Smith, one file each. Anything not in this folder is not in the language.

An example may show a function by its signature alone, where its body does not matter.

The files are a tour. Read them in the order below: each one uses only what the files above it introduced, and ends with a link to the next.

- [overview.md](overview.md): what Smith is for, the rules that hold throughout the language, and the four principles its design follows.
- [functions.md](functions.md): typed parameters, a return type, a block whose last expression is its value, named arguments, and defaults.
- [bindings.md](bindings.md): how `x = e` defines an immutable name, once per function, and how a binding states its type.
- [mutable-locals.md](mutable-locals.md): locals that change, declared with `var`, and `if` as an expression.
- [structs.md](structs.md): structs with named fields and defaults, assigning to a field of a `var`, tuples, and patterns that take them apart.
- [enums.md](enums.md): enums that hold one of their cases, and `match` to take a value apart.
- [traits.md](traits.md): type parameters, traits over one or more types, `impl`, operators as trait functions, and `derive`.
- [numbers.md](numbers.md): literals backed by traits, the numeric types, and integer arithmetic that never overflows silently.
- [conversion.md](conversion.md): the `Convert` trait, which turns a value of one type into another where nothing is lost, how it lets two types meet in an operator, and narrowing a whole number.
- [lambdas.md](lambdas.md): a function with no name, written where it is passed to a call.
- [function-values.md](function-values.md): a function with a name used as a value, which can be passed, stored, and returned.
- [with.md](with.md): `with`, which passes the rest of a block to a function as a lambda.
- [try.md](try.md): `try`, which continues a function only when a step succeeded, and the `Try` trait that gives it its meaning.
- [effects.md](effects.md): effects listed in a signature as permissions, how an effect is declared, how a handler is used and written, and the effects Smith provides.
- [text.md](text.md): where a program starts, `print`, string literals that hold expressions formatted by the `Fmt` trait, and `Repr`, which writes a value as source.
- [ownership.md](ownership.md): the `Copy` and `Discard` traits, lending and `take`, and values that must be used at most once or exactly once.
- [arrays.md](arrays.md): arrays whose sizes are part of their types, indices proven to be in range when the program is built, indexing as the `Index` trait, and ranges.
- [for.md](for.md): `for` as an unordered description of an array, reductions, the operators of linear algebra, `@`, and the rules for float arithmetic.
- [loop.md](loop.md): `loop` for steps that run in order, the `Iterate` trait, and `break`.
- [contracts.md](contracts.md): `requires`, `ensures`, and `check`, what the `arith` procedure can prove, termination, and `hole`.
- [storage.md](storage.md): when an array gets a buffer, the `alloc` effect and arenas, recursion without stack overflow, and types that contain themselves.
- [code.md](code.md): functions that receive the checked code of an argument when the program is built and return code, which is how `expect` and `grad` are library functions.
- [differentiation.md](differentiation.md): `grad`, a function over code that returns the gradient of a function, shown by training an MLP.
- [devices.md](devices.md): running the same source on a GPU with `with device(gpu)`.
- [tasks.md](tasks.md): task groups with `spawn` and `join`, cancellation, async I/O that reads as sequential code, how a task ends, and events.
- [foreign.md](foreign.md): foreign declarations with C signatures, and how system frameworks are reached for native applications.
- [modules.md](modules.md): one file as one module, `pub`, and explicit imports with `use`.
- [projects.md](projects.md): how a folder becomes a project by convention, what `smith new` creates, the standard library, vendored dependencies, build steps and tasks in `project.smith`, and the command line.
- [tests.md](tests.md): tests found by convention, examples and properties driven by a `Key`, and how they run.
- [transformer.md](transformer.md): a decoder-only transformer in under a hundred lines, built from everything before it.
