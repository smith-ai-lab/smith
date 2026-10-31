---
type: design
title: Lambdas that capture nothing are plain functions
description: A lambda with no captures is lifted by the lowering into a function with a generated name and is the function's address, so it converts to a function pointer; captures come later.
tags: [lambdas, lowering]
---
# Lambdas that capture nothing are plain functions

Issue #955, the first slice of #316. `[](i32 a) -> i32 { return a * 2; }` is an expression whose value is the address of a function the lowering writes (`runtime/lang_lambda.c`). Only what ships is described here; a lambda that captures, `auto f = [...]` keeping the lambda's own type and `lambda(F)` generics are later tickets of #316 (the owner's decision, 2026-10-31: a lambda keeps its own type, and converts to a raw function pointer only where one is expected).

## Forms

`[](parameters) -> type { body }`; `-> type` is optional and the parameters may be left out (`[] { ... }`). A `return` is written, as in C. In a function it is an argument, an initializer, an assignment or after `return`; at file scope it is an initializer, also inside a table's braces. `[` starts an expression only after `=`, `(`, `,`, `?`, `:`, `{`, `&&`, `||` or `return` and is followed by `]` and `(`, `{` or `->`: none of that is valid C, a subscript or a declarator's brackets, so C means what it means (`tests/smith-lang-c/brackets.c`).

## Lowering

- **In a function** C has no function inside a function. The lambda's tokens are kept, the expression becomes `__extension__({ extern R f(P); f; })` and the definition `extern __attribute__((visibility("hidden"))) R f(P) { body }` is written when the function ends (the next token at brace depth 0). The name is `smith_lambda_<hash of the file's path>_<place of the `[`>`: a block can only declare an external function, and the hash keeps two files' lambdas apart.
- **At file scope** the declaration that holds the lambda is read ahead from its first token, the definition (`static`) is written before it and the lambda becomes the name.
- The tokens of a definition are read again as any others, so a lambda in a lambda is lifted from the definition of the first. The raw lowering (`--smith-header`, `--smith-lower`) drops the `[]` of a lambda and writes no body.
- **The return type** without `-> T` is asked of the compiler: the first `return`'s expression, with each parameter a member of a compound literal (`((struct { i32 a; }){}.a)`), because the compiler reads it outside the lambda's scope. A body whose first statement is not that `return` needs `-> T`. A later `return` gets a `_Static_assert` that its type is the first's.
- **Captures**: a use of a name of the enclosing function is undeclared where the body is read again. In a module the lowering knows its own names, and reports the first other name `lookup` finds in the file as a local; elsewhere the compiler's error gets a note. The hook offers every token of a file whose text has `[](`, `[]{` or `[]->` (`hasLambda`, with the tuples' gate).

## Limits

A lambda is at most 900 tokens and a function's lambdas at most 960 in all; a lambda in a `static` local's initializer is not a constant; a lambda in a lambda inside a generic's body is an error (write the inner one as a function).

## In a generic's body

Issue #956. A lambda directly in a generic's body is a function of each instance (#316: "a generic's compile-time parameters are not captures"; this is what lets `http::get(path, handler)` keep routes with different handlers in one table). `runtime/lang_nested.c`, included by `lang_generic.c`, scans the generic's recorded tokens for lambdas (`smith_lang_nested_scan`) and, where the instance is written, writes each at file scope before the instance as `static R <instance>__lambda<k>(P) {...}`, replaying its parts with the instance's substitution (`T`, `constexpr auto f`, `typeof(a)`, `countof(rest)`, `if constexpr` and `for constexpr` work). In the instance's body the lambda is the name, so its address is a function pointer that knows the instance's types. A generic called with two types writes two functions (`expect/generics` shows them).

- **Captures.** The generic's `constexpr` parameters are not captures. A run-time parameter of the generic (every one that is not `constexpr`, `auto x` and `auto... rest` included) used in the lambda is the error that names it; a `typeof(x)`, `sizeof(x)`, `countof(x)` or `a->[rest]` of it is not a use of the value. A local of the generic is undeclared where the function stands, and the compiler says so.
- **The return type** without `-> T` is read at the instance with no query to the compiler (the replay has none): the instance writes `__typeof_unqual__(e)` for the first `return e;` with each parameter of the lambda a member of a compound literal of a struct of its parameters (`((smith_lambda_args){}.a)`), defined before the function, so the compiler reads it once the instance's types exist. As outside a generic, the `return` must be the body's first statement; no `return` (or `return;`) is `void`.
- **`typeof(rest)` and `x->[rest]...`** stay: a lambda cannot say the struct of the arguments or their members. `typeof(rest)` is `struct smith_pack_<instance> { A _0; B _1; };`, defined before the functions when the body asks for it (a scan of the body for `typeof ( rest )`, so no other generic's output changes); an empty `rest` is an error. `x->[rest]...` and `x.[rest]...` in a pattern expansion are `x->_0, x->_1, ...` (`smith_lang_pack_pattern_copies`). `closure(F)` (#400) will store a function with its captures as one value on top of this.
- **Not built**: a lambda in a lambda of a generic (an error), a lambda after a braced `return {...}` of a generic (that form is not read in a generic's body at all), and more than 8 lambdas in one generic. The old spelling, a `static` function defined in a generic's body (#613), is deleted: the lambda replaces it.
