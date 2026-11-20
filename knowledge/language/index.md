# The Smith language

Every feature of Smith, one file each. Anything not in this folder is not in the language.

An example may show a function by its signature alone, where its body does not matter.

The files are a tour. Read them in the order below: each one uses only what the files above it introduced, and ends with a link to the next.

## The basics

- [overview.md](overview.md): what Smith is for, the rules that hold throughout the language, and the four principles its design follows.
- [first-program.md](first-program.md): a program that starts at `main`, prints a line, and is run with `smith run`.
- [functions.md](functions.md): typed parameters, a return type, a block whose last expression is its value, named arguments, defaults, and comments.
- [bindings.md](bindings.md): how `x = e` defines an immutable name, which is never defined again while it can be used, how a binding states its type, and where a name can be used.
- [var.md](var.md): locals that change, declared with `var`, given a new value with `=`, and accumulated into with `+=`, `-=`, `*=`, and `/=`.
- [if.md](if.md): `if` as an expression that chooses between blocks, `else if`, and the words `not`, `and`, and `or`.
- [structs.md](structs.md): structs with named fields and defaults, reading a field, assigning to a field of a `var`, and a definition at the top level of a file.
- [tuples.md](tuples.md): tuples that group values without names, patterns that take them apart and assign, and the empty tuple.
- [enums.md](enums.md): enums that hold exactly one of their cases, each case holding a type or nothing, and how a case is built.
- [match.md](match.md): `match`, which takes a value apart with a pattern and a block for each way it can be, patterns for cases, tuples, and literals, and the rules for covering every case, including a range of whole numbers.

## Types and traits

- [type-parameters.md](type-parameters.md): a name for a type that a function or a type leaves open, how a call or a binding settles it, and a default for one.
- [traits.md](traits.md): a trait as a set of functions over one or more types, an `impl` that supplies them, how a call finds the `impl`, and the defaults of a trait's parameters.
- [bounds.md](bounds.md): what a function or an `impl` needs of its type parameters, written as a bound or as `requires`, and how an `impl` with type parameters is chosen.
- [out-parameters.md](out-parameters.md): a trait parameter marked `out`, which the others determine, and a default for a trait parameter.
- [operators.md](operators.md): an arithmetic operator as a call of a trait function, operands of different types, and `derive`.
- [comparisons.md](comparisons.md): `==` and `!=` through `Eq`, `<`, `<=`, `>`, and `>=` through `Ord`, and the laws every implementation keeps.
- [automatic-traits.md](automatic-traits.md): the `Eq` and `Ord` that a struct, an enum, a tuple, or an array is given, how each orders its values, and `without`.

## Numbers

- [literals.md](literals.md): how a number, a character, a text, `true`, `false`, or an array is written, the trait that backs each, and how a whole number is the range that holds it alone.
- [number-types.md](number-types.md): every whole number as a range `a..b`, the floats, the names `Int`, `Nat`, and `Byte`, and the names of sizes in `std.bits`.
- [arithmetic.md](arithmetic.md): the exact range that `+`, `-`, `*`, and `/` give, the proof asked where a value enters a narrower range, `narrow` and `clamp` where nothing proves it, and `wrapping_` for a range that fills a size.
- [bits.md](bits.md): the functions that and, or, xor, and shift the bits of a whole number and give the exact range of what they make, as arithmetic does, and what a number that may be negative and `bit_not` ask of a range.
- [conversion.md](conversion.md): the `Convert` trait, which turns a value of one type into another where nothing is lost, how it lets two types meet in an operator, and why two whole numbers need none.

## Functions as values, and leaving early

- [lambdas.md](lambdas.md): a function with no name, written where it is passed to a call.
- [function-values.md](function-values.md): a function with a name used as a value, which can be passed, stored, and returned.
- [with.md](with.md): `with`, which passes the rest of a block to a function as a lambda.
- [try.md](try.md): `try`, which continues a function only when a step succeeded, and the `Try` trait that gives it its meaning.
- [pipeline.md](pipeline.md): `|>`, which writes a call after its first argument, `_` for another place, and `try` on a step.
- [return.md](return.md): `return`, which leaves the nearest function or lambda early with a value, and cuts nothing short.

## Effects

- [effects.md](effects.md): an effect as something a function does outside itself, how a signature lists the effects a function may perform, and what a caller must have.
- [effect-declarations.md](effect-declarations.md): how an effect is declared by listing its operations, what a declaration gives, and how a program defines and imports effects of its own.
- [handlers.md](handlers.md): a handler, a function that runs a lambda with an effect handled, what it keeps, and what it may do.
- [provided-effects.md](provided-effects.md): the five effects the standard library declares, the six the compiler gives, and where an effect that nothing handles ends.

## Text, and where a program starts

- [interpolation.md](interpolation.md): strings that hold expressions in braces, the `Fmt` trait that turns each into text, and the escapes that write a brace or a quote.
- [repr.md](repr.md): the `Repr` trait, which writes a value as source, and the `Fmt` and `Repr` that structs, enums, and tuples are given.
- [main.md](main.md): the arguments `main` may take, and the `Exit` trait, which turns what `main` returns into the status a program ends with.
- [text.md](text.md): a `str` as bytes known to be valid UTF-8, `utf8`, and pieces of a text.

## Owning a value

- [ownership.md](ownership.md): the `Copy` and `Discard` traits, which say whether a value may be duplicated and whether it may be dropped unused, and the three kinds of value they make.
- [without.md](without.md): `without`, which takes away a trait a type would get automatically.
- [take.md](take.md): lending an argument, `take`, which gives the callee ownership, and a random `Key` that cannot be used twice.
- [resources.md](resources.md): a value with neither `Copy` nor `Discard`, which must be taken exactly once on every path.

## Arrays and their mathematics

- [arrays.md](arrays.md): an array as a fixed number of elements of one type, a size that is a `Nat` and may be any name that cannot change, and a `List` as an array kept with its size.
- [indexing.md](indexing.md): `x[i]`, an index proven to be in range or typed as a range `0..n`, the `Index` trait with its `valid`, and assigning an element.
- [ranges.md](ranges.md): `a..b`, which excludes `b` and is also the type of its numbers, and `x[a..b]`, `x[..k]`, and `x[k..]` as parts of an array.
- [array-patterns.md](array-patterns.md): a pattern in brackets, which takes an array or a list apart with a pattern for each element.
- [for.md](for.md): `for` as a description of an array by the value at every index, and where each index gets its size.
- [element-wise.md](element-wise.md): the operators on arrays that mean what they mean in linear algebra, no broadcasting, and `map`.
- [reductions.md](reductions.md): `sum`, `max`, and `mean`, which reduce a view to a value, and a reduction inside a `for`.
- [accumulation.md](accumulation.md): a `for` whose block accumulates into a `var` with `+=` or `*=`, why `=` is refused there, and an indexed accumulation.
- [linear-algebra.md](linear-algebra.md): `@`, which means `matmul`, how its sizes are checked by the types, a batch as a `for`, and a linear layer.
- [float-rules.md](float-rules.md): float code that is fast and repeatable, how a float reduction is regrouped, `sum_ordered`, and which products are fused.
- [random-arrays.md](random-arrays.md): a random function that fills a whole array from one key, `split_n`, and `keys[i]` inside a `for`.

## Loops

- [loop.md](loop.md): `loop` over a range, which runs a block once for each number in order, the type of its counter, `break`, and what `arith` knows inside it.
- [open-loop.md](open-loop.md): `loop` with no range, which runs until `break` and carries `diverge`, and `loop decreases`, which is proven to end.
- [iterate.md](iterate.md): the `Iterate` trait, which makes a type loopable, how `loop v in x` calls its `each`, and `Flow`, by which a loop is stopped.

## Proofs

- [arith.md](arith.md): what the `arith` procedure proves by itself and from what, what it cannot decide, and what the compiler says when it cannot prove.
- [contracts.md](contracts.md): `requires` and `ensures` on a function, what the caller owes and what the function gives.
- [termination.md](termination.md): every function ends unless it carries `diverge`, and `decreases` on a function that calls itself.
- [proof-fields.md](proof-fields.md): a struct that holds a proof, `Pos`, an index that travels as a range `0..n`, and a fact kept in a type for every round of a loop.
- [check.md](check.md): `check`, which gives a proof of outside data when a test passes, and a failure that is returned.
- [proof-functions.md](proof-functions.md): a proof passed, returned, and held, and a predicate as a function that returns `bool`.
- [statements.md](statements.md): a statement as a type built from what data is built from, a proof as a value of it, and how "for every" is a function type.
- [hole.md](hole.md): `hole`, which stands in for code or a proof not yet written.
- [trust.md](trust.md): the rule that Smith trusts nothing but a small kernel, the four rules that keep a proof honest, and what is still taken on trust.

## Memory and recursion

- [buffers.md](buffers.md): when an array gets a buffer and when it fuses into the code that reads it.
- [arenas.md](arenas.md): the `alloc` effect, which data of a size known only at run time carries, and `with arena()`.
- [recursion.md](recursion.md): a tail call as a loop, other recursion with its frames in an arena, and why the stack cannot overflow.
- [recursive-types.md](recursive-types.md): a type that contains itself, which is how a tree is written, and a graph as a collection plus indices.

## Code as data, and learning

- [code.md](code.md): functions that receive the checked code of an argument when the program is built and return code that takes the place of the call, which is how `expect` is a library function.
- [differentiation.md](differentiation.md): `grad`, a function over code that returns the gradient of a function with respect to its first parameter.
- [differentiable.md](differentiable.md): the `Differentiable` trait, through which each primitive supplies its derivative rule and a function may supply its own backward pass.
- [vector.md](vector.md): `derive Vector`, which gives a struct of float arrays addition, subtraction, and scaling by a number.
- [training.md](training.md): a loss, its gradient, a step of gradient descent, and a loop of steps, shown by training an MLP.
- [devices.md](devices.md): running the same source on a GPU with `with device(gpu)`, what the scope may hold, and where the data lives.

## Tasks

- [tasks.md](tasks.md): a task group opened with `tasks()`, children started with `spawn` and waited for with `join`, and the `spawn` effect.
- [task-sharing.md](task-sharing.md): what a spawned lambda may use: an ordinary value is shared, a `var` is read as it was at the spawn, and a single-use value moves into the child.
- [task-arenas.md](task-arenas.md): each child task's own arena, a result built in the parent's, and a server as one task per connection and one arena per request.
- [task-handlers.md](task-handlers.md): a child runs under the handlers in force where it was spawned, and a handler's operations run one at a time.
- [async.md](async.md): all I/O is async and reads as sequential code, suspending a task under an async handler and blocking under a blocking one.
- [cancellation.md](cancellation.md): ending a group's block is the one way to cancel, and when a cancelled task ends.
- [task-ending.md](task-ending.md): the three things that end a task before its work is done, and what is dropped and closed when one does.
- [task-failures.md](task-failures.md): what `join` gives for a child that failed, `outcome`, which lets a task recover, and a child that is never joined.
- [events.md](events.md): a native application as a task that waits for the next event, with no callbacks, and a window that lives as long as `main`.

## The system outside

- [foreign.md](foreign.md): a foreign declaration with a C signature, called directly in the C calling convention, reaching the operating system and its libraries only.
- [foreign-effect.md](foreign-effect.md): why `foreign` cannot be handled away, how a foreign call is kept behind an effect, and how `print` carries `console` alone.
- [system-frameworks.md](system-frameworks.md): how AppKit and UIKit are reached through the Objective-C runtime's C functions, and how the OS calls back in.

## Modules and projects

- [modules.md](modules.md): a file as a module, and how its path gives the module its name.
- [pub.md](pub.md): everything in a module private unless marked `pub`, on an item or on a single field, and what that lets another module do with a struct.
- [use.md](use.md): `use`, which brings the names of another module into a file from an explicit list, and what is said when a name is missing or unused.
- [prelude.md](prelude.md): the names that are in scope in every file without a `use`, `Int`, `Nat`, and `Byte` among them, and what a file imports for everything else.
- [qualified-names.md](qualified-names.md): a `use` with no list, which brings in the module itself under its last path segment, `as`, and a name two modules share.
- [impl-placement.md](impl-placement.md): an `impl`, which is not imported, is visible everywhere, and lives in the module that defines the trait or the type.
- [projects.md](projects.md): a folder as a project, its root and entry, the names of its modules and its executable, `project.smith`, and what `smith new` creates.
- [command-line.md](command-line.md): the `smith` commands, how `build`, `run`, and `check` take a file and a target, and what each prints when it fails.
- [standard-library.md](standard-library.md): what the standard library covers, which is why outside code is rarely needed, and how its modules are imported from `std`.
- [dependencies.md](dependencies.md): outside code vendored into the project as ordinary modules, listed with its source and hash in `project.smith`, and checked by the build without the network.
- [build-steps.md](build-steps.md): a function listed under `generate` in `project.smith`, which the build runs before compiling to write an ordinary source file.
- [project-tasks.md](project-tasks.md): a public function in `project.smith`, run by name, whose parameters are its command-line flags and whose effects say what it may do.
- [deployments.md](deployments.md): a public function of `project.smith` that installs the handlers that answer a program's effects and calls `main`, run by name and where a built executable starts.

## Tests

- [tests.md](tests.md): tests found by convention, a function named `test_` that returns a `Result`, the `expect` check, and a test file as an ordinary module.
- [random.md](random.md): the `Random` trait, which gives a value of a type that is good at finding mistakes, who has it without writing it, and how to take it away or replace it.
- [properties.md](properties.md): a test that takes a `Key`, or typed parameters, which runs many times on random inputs, is shrunk to the smallest failure, and is proved first when the checker can.
- [running-tests.md](running-tests.md): `smith test`, which runs every test or the ones chosen, in parallel, and what a failure and a run print.

## All of it together

- [transformer.md](transformer.md): a decoder-only transformer in under a hundred lines, built from everything before it.
