---
type: architecture
title: smith's own front end for .smith files
description: A tokenizer, parser, type checker and LLVM IR emitter for .smith, in front/, not wired into smith cc yet, built in small tickets under #1112.
tags: [front-end, parser, ast]
---
# smith's own front end

Plan: #1112 (the plan of record is its first comment). Today's `.smith` files are lowered to C through a token hook in clang; the new front end reads `.smith` itself and emits LLVM IR through LLVM's C API, with no patches on LLVM. It lives in `front/` (modules `front::ast`, later `front::lex`, `front::parse`, `front::types`, `front::emit`, `front::cimport`), is not wired into `smith cc`, and shows its work through tests (`tests/front_*_test.smith`, run by `smith build test-front`, golden files under `tests/front/`). Later front tickets each add a section here.

## front::ast (`front/ast.smith`, #1116)

The syntax tree. Plain data: every type is exported with all its fields. `TypeExpr`, `Expr`, `Stmt` and `Decl` are a `span` and a `kind`, a payload enum with its data in the variant (`e->kind.binary.left`, read where `e->kind == .binary` held). An absent child is a null pointer, a list of children is a slice, a string is a `Str` into the source or the current allocator. `Module` is a name, a file and the declarations.

- **Constructors** copy into the current allocator, so a caller that sets an arena as the allocator frees a tree at once: `new_expr`, `new_stmt`, `new_decl`, `new_type`, and short helpers (`name`, `integer`, `binary`, `call`, `named`, `block`, `function`, `variable`, `record_decl`, `enumeration`, `type_alias`, `import_module`, `import_c`, `new_module`, ...). A constructor that takes a list copies it, so a stack array can be passed.
- **`ast::dump(module)`** is an S-expression, a node per line, two spaces a depth; a node with no statement under it and under 16 nodes is written on one line. Spans are not dumped. The layout is documented at the top of the file; the goldens in `tests/front/ast/` are what parser tickets compare against.
- **For track C**: `Decl.from_c` (read from a C header, reached as `c::name`), `record.opaque` (an incomplete struct), and the `TypeExpr` kind `unsupported_c { spelling }`. A function pointer is `applied Fn(function {...})`; smith has no function-pointer syntax.
- **No recursion**: a walk of the tree uses an explicit stack (a module may not recurse).

**Names that differ from the ticket**, forced by today's smith (accepted on #1136):
- `Field` is `FieldDecl`: a parameter of type `Field` makes a function a compile-time one (reflection's `Field`).
- `Identifier { Str text; Span span; }` for lists of words (`Path.segments`, a lambda's captures): `Str xs[:]` is a two-dimensional block, not a list of texts.
- Slices of children are slices of `ExprRef`, `StmtRef`, `DeclRef`, `TypeRef` (typedefs of the pointers): `Expr *xs[:]` in a field or parameter writes a broken C header.
- `Text` (`typedef Str Text`) for strings inside an inline struct of a payload enum's variant, which breaks the lowering with `Str`.
- `make_type` and `record_decl`: `type` and `record` as function names are compile-time functions. There is no generic list builder (`arena::slice_of` fails with a type from another module).

**Limits of today's smith found building it.** A string literal inside a nested initializer of a payload enum is not converted to `Str` across modules (pass names as arguments); a braced initializer is not accepted as an element assignment (`xs[i] = {...}`), only in a declaration, argument or return.
