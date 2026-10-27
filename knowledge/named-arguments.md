---
type: design
title: Named arguments are a designated initializer of the last parameter
description: A call's trailing .name = value arguments become the compound literal of the function's last struct parameter, so options are named, optional and zero by default, with no overloading and no default-argument machinery.
tags: [language, calls]
---
# Named arguments are a designated initializer of the last parameter

**Decision** (issue #317; owner, 2026-10-25: "you are effectively using designated initializers ... you can have default parameters because they get the zero value ... we are following zero is initialization"). `html::div(&page, .class = "card")` is `html_div(&page, (DivOptions){.class = "card"})`. `.name = value` is invalid C as a call argument, so the C rule holds ([smith-language](smith-language.md)), and every addition lowers to plain C.

- **One rule.** The trailing `.name = value` arguments of a call are the designated initializer of the callee's **last** parameter, a struct. Defaults are zero (C's rule); there is no declared default. Option structs are designed so zero is the usual value; a required value is a positional parameter. Positional arguments come first, then the named ones; a positional argument after a named one is an error. A misspelled name is clang's own error.
- **Not the last parameter: no.** A named argument targets the last parameter only; the positional arguments before it must be all the other parameters (the error names the count). Reaching into the middle would need the call to say which parameter, which is new syntax.
- **A generic's `auto` parameter is never the target** (owner's delegate, 2026-10-27). `auto x` takes the type of its argument, and `.name = value` has no type until a struct is chosen: the target of a generic will be a last parameter written as a concrete struct type. Generic callees are an error for now (`is a generic function: it does not take named arguments yet`); the instance machinery types arguments from captured tokens, which a `.name = value` is not.
- **Lowering** (`runtime/lang_named.c`, called from `smith_lang_process_token` for an identifier): the host sets `maybe_named` for a file whose text has `. name =` right after a `(` or `,` that a `(` encloses (`hasNamedArgument`, shim/hook.cpp), and the lowering is then offered every token. At a callee (a name, or `m::f`) it peeks to the closing parenthesis for an argument that starts `. name =` at the call's own depth, asks the compiler the callee's type (`expr_type` of the name as the module writes it, `smith_lang_tuple_resolve`), takes the spelling of the last parameter from the printed prototype, checks it is a struct (`type_info`), and writes `(T){` before the first named argument and `}` before the `)` as synthetic tokens. A lone `.`, a member call and a call whose arguments are not all in the queue are left to the compiler.
- **Not yet.** A trailing lambda after the named arguments (#316), generic callees, and a named-argument call inside a generic's body.

Corpus: `named-arguments`, `error-named-misspelled`, `error-named-stray`, `error-named-count`, `error-named-not-struct`, `error-named-generic`. Skill: `smith-lang-named-arguments`.
