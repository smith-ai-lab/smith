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

A lambda is at most 900 tokens and a function's lambdas at most 960 in all; a lambda in a `static` local's initializer is not a constant; a lambda in a generic's body is not lowered yet.
