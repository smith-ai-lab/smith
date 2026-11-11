---
type: architecture
title: "The new front end: front::ast"
description: "front::ast (front/ast.smith, #1116): one area of smith's own front end (the map is knowledge/front-end.md)."
tags: [front-end]
---
# front::ast (`front/ast.smith`, #1116)

The syntax tree: plain data, every type exported with all its fields. The layout and the dump format are the top comment of the file.

- `TypeExpr`, `Expr`, `Stmt` and `Decl` are a `span` and a `kind`, a payload enum with its data in the variant (`e->kind.binary.left`, read where `e->kind == .binary` held).
- An absent child is a null pointer, a list of children is a slice, a string is a `Str` into the source or the current allocator.
- Constructors (`new_expr`, `binary`, `call`, `block`, `function`, ...) copy into the current allocator, so an arena as the allocator frees a tree at once; a list argument is copied.
- `ast::dump(module)` is an S-expression, a node per line, two spaces a depth; spans are not dumped. The goldens in `tests/front/ast/` are what parser tickets compare against.
- A walk of the tree uses an explicit stack: a module may not recurse.
- Track C nodes: `Decl.from_c` (read from a C header, reached as `c::name`), `record.opaque`, and `TypeExpr` kind `unsupported_c`. A function pointer is `applied Fn(function {...})`.
- Anonymous records: the `TypeExpr` kind `record { fields, is_union }`, dumped `(struct (field ...))` or `(union ...)`.
- Tuple types (A7h): `struct { I32, F32 }` is the kind `tuple { elements }`, dumped `(tuple (named I32) (named F32))`.
- Nodes for smith's own syntax (S0, #1440): flags `is_mut`, `is_const`, `is_once`, `is_pub`, dumped as the words `mut`, `const`, `once`, `pub`.
- New types: `option { inner }` (`?T`), `block_type { element, dims }` (`[5,3]T`, a null size is `(absent)`), and `pointer.is_mut` (`*mut T`).
- New expressions: `named_arg`, `variant.payload`, `tuple_lit`, `array_lit`, `stmt_expr` (an `if`, `match`, block or `for` as a value).
- New statements: `match_stmt` (`MatchArm`, `Pattern`), `for_in`, `for_index` (#2512: `for i, j: body`, the index form with no `in`; the parser reads it on the layout path, the checker and emitter do not read it yet), `destructure`, `if_let`, `yield_stmt` (#1544), `errdefer_stmt`.
- Reused, not new: `p.*` is `unary dereference`, `x.to(T)`, `x.wrap(T)` and `p.cast(T)` are one `cast` node with a form (convert, wrapping, reinterpret) (#1811), `fn[a](../x) { }` is `lambda`, `@name(args)` is an `Attribute` in `Decl.attributes`.
- Goldens for the new nodes: `tests/front/ast/syntax_*.txt`.

**Names that differ from the ticket**, forced by today's smith (accepted on #1136):
- `Field` is `FieldDecl`: a parameter of type `Field` makes a function a compile-time one (reflection's `Field`).
- `Identifier { Str text; Span span; }` for lists of words: `Str xs[:]` is a two-dimensional block, not a list of texts.
- Slices of children are slices of `ExprRef`, `StmtRef`, `DeclRef`, `TypeRef` (typedefs of the pointers): `Expr *xs[:]` writes a broken C header.
- `Text` (`typedef Str Text`) is used for strings inside an inline struct of a payload enum's variant, where `Str` breaks the lowering.
- `make_type` and `record_decl`: `type` and `record` as function names are compile-time functions. There is no generic list builder.

**Limits of today's smith found building it.**
- A string literal inside a nested initializer of a payload enum is not converted to `Str` across modules (pass names as arguments).
- A braced initializer is not accepted as an element assignment (`xs[i] = {...}`), only in a declaration, argument or return.

**A limit for importers.** In a module that imports `front::ast`, a `case .name:` label on an ast payload enum makes the compiler read ast's body files ([body-files](../body-files.md)), and that fails.
- Importers dispatch with `if (x->kind == .k)`; see the note in `front/emit.smith`.
- smith-enum narrowing is also lost once the node pointer is passed to a call, so read the payload fields into locals first.
