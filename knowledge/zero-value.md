---
type: contract
title: Zero is initialization
description: In a module T t; and T t = {}; both make a zero value, every byte zero, and option and result have a zero that is .none and .error.
tags: [language, idiom, zero]
---
# Zero is initialization

**In a module `T t;` and `T t = {};` mean the same thing: every byte of `t` is zero, padding included, and a union across its whole size** (owner, 2026-10-28: "you can write T t; or you can write T t = {}; and that means the same thing"). The skills, README and `templates/new` teach `T t;`; `T t = {};` stays valid, so C code moved into a module needs no change. Go's answer, "make the zero value useful", is the design: reading a variable never written is not an error and not undefined, it is zero.

**The first spelling is a compile flag.** `smith_module_flags` (`runtime/build_runtime.c`) adds `-ftrivial-auto-var-init=zero` to the compile of every `.smith` file with a `module` line, after the profile's flags, so it wins over the debug profile's `pattern`; the optimizer removes the store wherever the variable is written before it is read. The second is C23's empty initializer. A `.c` file, and a `.smith` file without a `module` line, keep the profile's flags (the C rule: giving undefined behaviour a value changes no valid program). `tests/zero_init_test.smith` fills the stack with garbage and checks that both spellings leave no nonzero byte in a struct with padding and a union.

**Every tool sees the zero, not only the compiler.** A flag is codegen only, so the static analyzer (`smith lint`) called a read of `Option(I32) last;` a garbage value. `ZeroLocals` in `shim/hook.cpp` runs where the array-parameter check runs, when the module's translation unit is complete and before any consumer, in the compiler, clang-tidy and clangd alike: each local without an initializer (no parameter, static, extern, variable-length array, incomplete type) gets an implicit value initializer, so `T t;` is `T t = {};` in the AST. `tests/zero_init_test.smith` checks it through `smith lint`. clangd does not run the static analyzer, so the editor never showed the garbage value and has no test of its own; it gets the same AST from the same pass.

**A library type's zero is useful.** `Option(T)`'s first variant is `none` and `Result(T, E)`'s is `error` (`smith_option_header` and `smith_result_header` in `runtime/lang_enum.c`, and `lib/smith/option.smith`, `result.smith`), so their tag 0 is `.none` and `.error` and a forgotten result never looks like success. A type's default is its zero value, never a constructor function.

Not built yet (issue #415): zeroed `mem::alloc`, the opt-out for a measured hot spot, and the compile error for a pure exported function with no parameters.
