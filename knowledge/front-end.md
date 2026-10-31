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

## front::emit (`front/emit*.smith`, #1119)

Turns a `front::ast` module into an LLVM module through LLVM's C API: `emit::module(context, &m, options)` returns the verified module or an `Error` with a span. Today it emits one function with integer arithmetic (literals, names, locals, `+ - * / %`, shifts, comparisons, assignment); signed `+ - *` and negation use `llvm.*.with.overflow`, division and remainder check zero and `MIN / -1`, a shift checks its width, all branching to one `trap` block per function; unsigned wraps. `Target` and `Options` pick the triple and the layout; symbols are the module path joined with `::`, exported is external, the rest internal. The tests run the result in LLJIT (`emit_jit.smith`) and compare golden IR (`tests/front/emit/`).

- **Files and owners**: `emit.smith` (passes and the walk), `emit_state.smith` (shared types, `Emitter`, stacks, helpers; a file of its own to break the import cycle), `emit_arith.smith` (this ticket's expressions), `emit_control.smith`, `emit_call.smith`, `emit_memory.smith`, `emit_debug.smith` (stubs that return `not_emitted`, one file per later ticket, no two tickets share a file), `emit_jit.smith` (test helper).
- **The protocol** is the top comment of `front/emit.smith`; its rules in a line each: no recursion, so a work stack and a value stack; a handler pops operands and pushes one value; a node that needs a child first pushes a `then` continuation; pass 1 declares, pass 2 emits bodies; allocas go in the entry block.
- **Handlers** dispatch with `if` chains, never `case .name:` ([#1153](body-files.md)); read payload fields into locals before any call.
- **No arena around LLVM**: LLVM allocates with `malloc`, which smith routes to the allocator in force, so a test must not hold an arena while it emits or runs the JIT.

## front::lex and front::parse (`front/lex.smith`, `front/parse.smith`, #1128)

**The tokenizer** reads the text of a file and gives all its tokens at once, ending with an `end_of_file` token. A `Token` is a `kind` and a `span`; its text is `source[span.start:span.end]` (`lex::text`), never a copy. `lex::tokenize(source)` is a `Lexed` (`Result(Tokens, LexError)`), the first error stopping it; `lex::dump(tokens, source)` writes one token a line, `line:column kind text`, which the goldens in `tests/front/lex/` hold.

- **Kinds** are a plain enum (`lex::plus`, `lex::kw_if`; the dot rule does not reach a struct field today). An identifier is `type_name` when its first character is an uppercase letter and `name` otherwise: the parser needs no symbol table. Each keyword has a kind (`kw_alignas` .. `kw_countof`: C23's, `_Atomic _BitInt _Complex _Decimal32 _Decimal64 _Decimal128 _Generic _Imaginary _Noreturn`, and smith's `module import export defer try countof`); C's type keywords stay keywords. Literals are `integer`, `floating`, `character` and `string`: the tokenizer checks the form (digit separators, suffixes, prefixes `u8 u U L`, escapes) and the parser decodes the value. Punctuators are C's without `#` and `##`, plus `::`; `[:]` is three tokens, `[[` and `]]` two each; there are no trigraphs and no digraphs.
- **Errors**: an unterminated string, character or block comment, a malformed number, a byte that starts no token, and any `#` (there is no preprocessor: the message names `constexpr` and `import`).
- **Comments and white space** make no tokens; doc comments are a later ticket's question.

**The parser core** is a `Parser` (the tokens, the source, a position, the errors, the hooks) and the cursor functions every parser file uses: `peek`, `peek_at`, `advance`, `at`, `accept`, `expect(p, kind, what)` (records `expected <what>, found <text>` and takes nothing), `text`, `error`, `span_from`. Expressions (A3), statements (A4) and declarations (A5) are parsed in three files that call each other only through `Parser.hooks` (`expression`, `type`, `statement`, `declaration`, function pointers) by way of `parse::expression(p)` and its three siblings, so each is tested with stubs before the others exist and A6 wires the real ones. A hook that is not set records an error and gives null. The errors are the current allocator's, as the tree is.

## front::cimport (track C)

Declarations read from C headers become these same nodes; its concept comes with #1117 (`c-header-import.md`), and links here once that file exists.
