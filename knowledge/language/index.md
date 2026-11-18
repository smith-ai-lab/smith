# The Smith language

Every feature of Smith, one file each. Anything not in this folder is not in the language.

An example may show a function by its signature alone, where its body does not matter.

The files are a tour. Read them in the order below: each one uses only what the files above it introduced, and ends with a link to the next.

- [overview.md](overview.md): what Smith is for, the rules that hold throughout the language, and the four principles its design follows.
- [first-program.md](first-program.md): a program that starts at `main`, prints a line, and is run with `smith run`.
- [functions.md](functions.md): typed parameters, a return type, a block whose last expression is its value, named arguments, defaults, and comments.
- [bindings.md](bindings.md): how `x = e` defines an immutable name, once per function, how a binding states its type, and where a name can be used.
- [var.md](var.md): locals that change, declared with `var`, given a new value with `=`, and accumulated into with `+=`, `-=`, `*=`, and `/=`.
- [if.md](if.md): `if` as an expression that chooses between blocks, `else if`, and the words `not`, `and`, and `or`.
- [structs.md](structs.md): structs with named fields and defaults, reading a field, assigning to a field of a `var`, and a definition at the top level of a file.
- [tuples.md](tuples.md): tuples that group values without names, patterns that take them apart and assign, and the empty tuple.
- [enums.md](enums.md): enums that hold exactly one of their cases, each case holding a type or nothing, and how a case is built.
- [match.md](match.md): `match`, which takes a value apart with a pattern and a block for each way it can be, patterns for cases, tuples, and literals, and the rules for covering every case.
- [traits.md](traits.md): type parameters, traits over one or more types, `impl`, operators as trait functions, and `derive`.
- [literals.md](literals.md): how a number, a character, a text, `true`, `false`, or an array is written, the trait that backs each, and how the type of a literal is decided.
- [number-types.md](number-types.md): the whole number and float types, what each holds, and why a number has exactly one type.
- [arithmetic.md](arithmetic.md): integer arithmetic that never overflows silently, a plain `+`, `-`, or `*` that compiles only when its result provably fits, and `wrapping_`, `saturating_`, and `checked_` where it cannot be proven.
- [bits.md](bits.md): the functions that and, or, xor, and not the bits of a whole number and shift them, with a shift proven to stay inside the type.
- [conversion.md](conversion.md): the `Convert` trait, which turns a value of one type into another where nothing is lost, how it lets two types meet in an operator, and narrowing a whole number.
- [lambdas.md](lambdas.md): a function with no name, written where it is passed to a call.
- [function-values.md](function-values.md): a function with a name used as a value, which can be passed, stored, and returned.
- [with.md](with.md): `with`, which passes the rest of a block to a function as a lambda.
- [try.md](try.md): `try`, which continues a function only when a step succeeded, and the `Try` trait that gives it its meaning.
- [effects.md](effects.md): an effect as something a function does outside itself, how a signature lists the effects a function may perform, and what a caller must have.
- [effect-declarations.md](effect-declarations.md): how an effect is declared by listing its operations, what a declaration gives, and how a program defines and imports effects of its own.
- [handlers.md](handlers.md): a handler, a function that runs a lambda with an effect handled, what it keeps, and what it may do.
- [provided-effects.md](provided-effects.md): the five effects the standard library declares, the six the compiler gives, and where an effect that nothing handles ends.
- [interpolation.md](interpolation.md): strings that hold expressions in braces, the `Fmt` trait that turns each into text, and the escapes that write a brace or a quote.
- [repr.md](repr.md): the `Repr` trait, which writes a value as source, and the `Fmt` and `Repr` that structs, enums, and tuples are given.
- [main.md](main.md): the arguments `main` may take, and the `Exit` trait, which turns what `main` returns into the status a program ends with.
- [text.md](text.md): a `str` as bytes known to be valid UTF-8, `utf8`, and pieces of a text.
- [ownership.md](ownership.md): the `Copy` and `Discard` traits, lending and `take`, and values that must be used at most once or exactly once.
- [arrays.md](arrays.md): an array as a fixed number of elements of one type, a size that is a `Nat` and may be any name that cannot change, and a `List` as an array kept with its size.
- [indexing.md](indexing.md): `x[i]`, an index proven to be in range when the program is built, the `Index` trait with its `valid`, and assigning an element.
- [ranges.md](ranges.md): `a..b`, which excludes `b`, and `x[a..b]`, `x[..k]`, and `x[k..]` as parts of an array.
- [array-patterns.md](array-patterns.md): a pattern in brackets, which takes an array or a list apart with a pattern for each element.
- [for.md](for.md): `for` as an unordered description of an array, reductions, the operators of linear algebra, `@`, and the rules for float arithmetic.
- [loop.md](loop.md): `loop` over a range, which runs a block once for each number in order, the type of its counter, `break`, and what `arith` knows inside it.
- [open-loop.md](open-loop.md): `loop` with no range, which runs until `break` and carries `diverge`, and `loop decreases`, which is proven to end.
- [iterate.md](iterate.md): the `Iterate` trait, which makes a type loopable, and how `loop v in x` calls its `each`.
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
