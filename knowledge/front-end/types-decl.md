---
type: architecture
title: "The new front end: front::types_decl"
description: "front::types_decl (front/types_decl.smith, #1220 J1d): one area of smith's own front end (the map is knowledge/front-end.md)."
tags: [front-end]
---
# front::types_decl (`front/types_decl.smith`, #1220 J1d)

Declarations, statements and bodies, joining J1a to J1c. `types_decl::check_module(module, target)` checks a whole module and returns the `Checked`; the API is the top comment of the file.
- It wires the hooks (`install`), runs `types::check` with `Options.bodies = check_body`, checks module-level initializers and lambda bodies, then takes the hooks out.
- Statements: blocks (a scope each), expression statements, locals (a written type or `Auto`), `if`, `while`, `do`/`while`, `for (init; cond; step)`, `return`, `defer`, labels, `break`, `continue`, `goto`.
- A condition is checked against `Bool`; a `return` value against the result type. The walk is an explicit work stack.
- Lambdas are checked after the body that holds them: the hook keeps a copy of the scopes, and `drain` walks each body with the parameters from the expected `Fn(F)`.
- `switch` is typed (#1916): the value, each case value against it (a payload enumeration's variants are not checked by name) and each case body in a scope of its own. A type used as a value is `Auto`, and `countof` of an `Auto` is accepted: the instance decides. Not yet typed: `static_assert` in a body, `if constexpr` and `for constexpr`, a local type or function, a `c::` name (#1124). A function that falls off its end is not reported.
- Spelling (K5, #1347): `Char` is a builtin 8-bit unsigned type of its own; `char` and `bool` in a type position are errors in today's syntax. A character literal has type `Char`.
- `const` and `constexpr` bindings are known at compile time (#1510, C1): `const a = n` with `n` not constant is an error naming `const a` (`require_constant`).
- In smith's own syntax (`is_new`): a name that hides an enclosing one is an error naming the earlier declaration by byte offset; `let _ = f()` declares nothing.
- A local, a parameter, a loop name or a binding that is the name of a module the file imports (the last part of its path or its `as` name) is the shadowing error too, naming the import (`hides_module`, #3026); the name is still declared, so its uses read the local and report nothing more. Tests: `tests/front_types_module_test.smith`.
- A call whose result is not `Void` and is unused is an error (`write let _ = ...`), judged once the function has a parameter or an earlier local in that syntax.
- `let (q, r) = f()` and `let (q, mut r) = f()` (#1558, `destructure_statement`) split a tuple: `_` discards one, a count that differs from the tuple's is an error naming both.
- `undefined` (K3b, #1334) is taken only as a local's whole initializer with the declared type (`types_expr::is_undefined`, `local_variable`).
- Tests: `tests/front_types_decl_test.smith` (all of `lib/std/ascii.smith` checks clean), goldens `tests/front/types/decl_*.txt`.
- Writing the checker so `smith lint --fix` converts it (#2669, part B): the fixer binds a payload (`.v(v) := k else: return`) only for a single guard `if (k != .v) return;` (write `x->kind != .v`, never `!(x->kind == .v)`), when the alias is not reassigned, no local, parameter or variant has the name of the variant, and the test is not in value position. Where several reads need one guard, use a one-guard accessor (`binary_left`, `block_item`, `variable_init`, ...) in `types_decl`. Build a payload enum in a local (`ast::DeclKind x = {...}`) and not inside a compound literal. A construction names every pointer field (`.test = nullptr`), since the fixer cannot zero a left-out pointer; an array field or a global array cannot be zero-initialised at all, so allocate a slice (`mem::alloc_slice`) in the function that makes the value (`Checked.bindings`, `State.tails`). An `Option` field cannot be narrowed: read it through a local first. A `Result` pattern takes an `else` (test: `test_front_types_payload_result_binding_takes_an_else`).
