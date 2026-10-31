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
- **Anonymous records**: the `TypeExpr` kind `record { fields, is_union }` is an anonymous struct or union used as a type (a payload variant `struct { I32 a; } one;`); its dump line is `(struct (field ...))` or `(union ...)`.
- **No recursion**: a walk of the tree uses an explicit stack (a module may not recurse).

**Names that differ from the ticket**, forced by today's smith (accepted on #1136):
- `Field` is `FieldDecl`: a parameter of type `Field` makes a function a compile-time one (reflection's `Field`).
- `Identifier { Str text; Span span; }` for lists of words (`Path.segments`, a lambda's captures): `Str xs[:]` is a two-dimensional block, not a list of texts.
- Slices of children are slices of `ExprRef`, `StmtRef`, `DeclRef`, `TypeRef` (typedefs of the pointers): `Expr *xs[:]` in a field or parameter writes a broken C header.
- `Text` (`typedef Str Text`) for strings inside an inline struct of a payload enum's variant, which breaks the lowering with `Str`.
- `make_type` and `record_decl`: `type` and `record` as function names are compile-time functions. There is no generic list builder (`arena::slice_of` fails with a type from another module).

**Limits of today's smith found building it.** A string literal inside a nested initializer of a payload enum is not converted to `Str` across modules (pass names as arguments); a braced initializer is not accepted as an element assignment (`xs[i] = {...}`), only in a declaration, argument or return.

**A limit for importers.** In a module that imports `front::ast`, a `case .name:` label on an ast payload enum makes the compiler read ast's body files ([body-files](body-files.md)), and that fails today (a private `typedef ExprRef ExprRefList[:]` loses its `;`, and a `mem` body says `smith_mem_allocator_name` is undeclared). Importers dispatch with `if (x->kind == .k)`. smith-enum narrowing is also lost once the node pointer is passed to a call, so read the payload fields into locals first.

## front::parse_decl (`front/parse_decl.smith`, #1147)

Declarations and whole files. `parse_decl::declaration(p)` is the hook `parse::Hooks.declaration`: it reads one declaration and consumes its own trailing `;` (a function with a body has none), so callers never expect one; it gives null after an error, with the parser past the bad declaration. `parse_decl::file(p, file)` reads an optional `module x::y;` (recorded in `Module.name` until the keyword is the switch) and declarations to the end of the file. Types come from `parse::type`, initializers from `parse::expression`, bodies from `parse::statement`.

- **Reads**: prefixes in any order (attributes, `export static extern inline constexpr thread_local`); a variable (a type, one name, array suffixes after the name, the first suffix the outermost, `= init`); a function (parameters with array suffixes and attributes, `constexpr Type T`, `auto... rest`, `...`, `()` or `(void)`); `struct`, `union` (bit-fields `U32 flags : 3;`), plain and payload `enum` (`typedef enum { Circle circle; empty; } Shape;`, `enum Small : U8 {...}`), `typedef`; `import` in its three forms; `static_assert`; and `if constexpr (c) { decls } else { decls }`, nested and chained, read with an explicit stack of open blocks (no recursion).
- **Anonymous records**: `struct { ... }` and `union { ... }` as a payload variant or the type of a variable are a `TypeKind.record`. One nested in a field is the type hook's to read, since this module may not recurse.
- **Errors that name the fix**: several names in one declaration, C's function-pointer declarator (`Fn(I32 (I32)) f`), K&R parameters, implicit `int`, an array with no size, and a tag spelled as C does. After one, the declaration is skipped to its `;` or closing `}` and the parse goes on.

## front::cimport (track C)

Declarations read from C headers become these same nodes; its concept comes with #1117 (`c-header-import.md`), and links here once that file exists.
