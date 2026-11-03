---
type: architecture
title: smith's own front end for .smith files
description: A tokenizer, parser, checker and LLVM IR emitter for .smith in front/, reachable through smith cc --front-end=new (experimental, off by default), built in small tickets under #1112 and #1428.
tags: [front-end, parser, ast]
---
# smith's own front end

Plan: #1112 (the plan of record is its first comment), smith's own syntax and the wave tickets: #1428. Without a flag, a `.smith` file is lowered to C through a token hook in clang.
The new front end reads `.smith` itself and emits LLVM IR through LLVM's C API, with no patches on LLVM. It lives in `front/`.

`smith cc --front-end=new` (`front/compile.smith`, #1221, #1578) is the way in: experimental, off by default; without it a `.smith` file still goes through the token lowering.
The pipeline, one module group per section below:

- **lex, parse**: text to a `front::ast` tree (`lex`, `parse`, `parse_expr`, `parse_type`, `parse_stmt`, `parse_decl`, `parser`).
- **fix**: the fixes parse and check errors carry, applied by `smith lint --fix`.
- **check**: the tree to types (`types`, `types_expr`, `types_convert`, `types_decl`, `types_attr`, `types_fn`, `types_iter`, `types_stream`).
- **emit**: the checked tree to an LLVM module (`emit*.smith`), then an object (`emit_object`).
- **drive**: `compile` and `roots` join the stages for `smith cc`; `cimport` reads C headers ([c-header-import](c-header-import.md)).

Each file's top comment is the detail; this page is the map. The work shows through tests (see Tests at the end).
The old lowering (`runtime/lang*.c`, `shim/`) is never touched by front-end tickets.

## front::ast (`front/ast.smith`, #1116)

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
- New statements: `match_stmt` (`MatchArm`, `Pattern`), `for_in`, `destructure`, `if_let`, `yield_stmt` (#1544), `errdefer_stmt`.
- Reused, not new: `p.*` is `unary dereference`, `x.to(T)`, `x.wrap(T)` and `p.cast(T)` are one `cast` node with a form (convert, wrapping, reinterpret) (#1811), `fn[a](x) { }` is `lambda`, `@name(args)` is an `Attribute` in `Decl.attributes`.
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

**A limit for importers.** In a module that imports `front::ast`, a `case .name:` label on an ast payload enum makes the compiler read ast's body files ([body-files](body-files.md)), and that fails.
- Importers dispatch with `if (x->kind == .k)`; see the note in `front/emit.smith`.
- smith-enum narrowing is also lost once the node pointer is passed to a call, so read the payload fields into locals first.

## front::lex and front::parse (`front/lex.smith`, `front/parse.smith`, #1128)

**The tokenizer** gives all the tokens of a file at once, ending with an `end_of_file` token. A `Token` is a `kind` and a `span`; its text is `source[span.start:span.end]` (`lex::text`).
- `lex::tokenize(source)` is a `Lexed` (`Result(Tokens, LexError)`), the first error stopping it; `lex::dump(tokens, source)` writes `line:column kind text`, held by `tests/front/lex/`.
- `lex::tokenize_new` is the same for smith's own syntax; the new parser calls it, `parser::parse` still calls `tokenize`.
- Kinds are a plain enum (`lex::plus`, `lex::kw_if`). An identifier is `type_name` when its first character is uppercase, else `name`: the parser needs no symbol table.
- Keywords are C23's plus smith's `module import export defer errdefer try countof`; C's type keywords stay keywords. Literals are `integer`, `floating`, `character`, `string`.
- The tokenizer checks the form of a literal (separators, suffixes, prefixes, escapes) and the parser decodes the value. `0o755` is octal (#1900); a `.` after the digits of an integer that starts a word which is no exponent or suffix is a method call (`3.to(u8)` is `3`, `.`, `to`), while `1.e5` and `1.f` stay floating.
- Punctuators are C's without `#` and `##`, plus `::`, `:=`, `=>`, `@` and `.*` (one token). `[:]` is three tokens, `[[` and `]]` two each; no trigraphs or digraphs.
- The keywords `fn pub mut match in as fn_once type yield` exist only through `tokenize_new`; `tokenize` keeps them names, because today's code uses them as names.
- Each `Token` has `line_break`, recorded only. `lex::ends_statement(token)` is Go's rule: a break ends a statement after a name, literal, `)`, `]`, `}`, `return`, `break` or `continue`.
- `lex::layout(source, tokens)` (#1808) turns the tokens of `tokenize_new` into tokens with layout, not yet read by the parser: `newline` ends a statement, `indent` follows a `:` that ends a line when the next line is one level (4 spaces) deeper, and each level a line drops is a `newline dedent`; the end of the file closes the open levels. Inside `(`, `[` or `{`, after a binary operator at a line's end, and on blank or comment-only lines, line breaks and indentation count for nothing. Errors: a tab in indentation, a level that is not a multiple of 4, an unexpected indent. Layout tokens have no text and a span of no width.
- `lex::dump_lines` is `dump` with ` [eol]` after such a token (`tests/front/lex/new-syntax.tokens`).
- In `tokenize_new` (#1812, first half) a comment is `# text` or `##` to the line's end; `//` and `/* */` are errors with a fix (`parser::parse_new` carries the edit `//` to `# `, and `fix::fix_text` writes `#` for every `//` comment once the text reads in the new syntax), and `#word` with no space stays the old-directive error. `tokenize` keeps `//` and `/* */`.
- Errors: an unterminated string, character or block comment, a malformed number, a byte that starts no token, and any `#` (the message names `constexpr` and `import`).

**The parser core** (`front/parse.smith`) is a `Parser` (tokens, source, position, errors, warnings, hooks) and the cursor functions: `peek`, `peek_at`, `advance`, `at`, `accept`, `expect`, `text`, `error`, `span_from`.
- Expressions, statements and declarations are parsed in separate files that call each other only through `Parser.hooks` (`parse::expression(p)` and its three siblings); an unset hook records an error and gives null.
- A `ParseError` has `fix` (a list of `Edit`: a span and its replacement text). `parse::error_fix` records one in `Parser.errors`; `parse::warning_fix` one in `Parser.warnings`.
- A warning is an old form that still compiles; `parser::Parsed.warnings` carries them, and tests that count `errors` ignore them.
- The errors are the current allocator's, as the tree is.

**Fix-its** (S6, #1442, `front/fix.smith`): a `ParseError` has `fix` (a list of `Edit`: a span and its replacement text). `parse::error_fix(p, span, message, edits)` and `parse::warning_fix` record one; `warning_fix` appends to `Parser.warnings` (`parser::Parsed.warnings`, ignored by `front/compile.smith`); the old forms get fixes ticket by ticket (#1428). `parse_type` (F1, #1506) records one warning per old type with one edit that writes the whole type in smith's own syntax: lowercase built-ins and sized types for C's names, `*T`, `[]T`, `[N]T` and `[a,b]T` for the suffixes, `(A, B)` for `struct { A, B }`, `fn(A) -> R` for `Fn(R (A))` (the parameter names of `Fn(R (A a))` are dropped, #1957), with `const` dropped; `volatile`, `restrict`, `long double` and a variadic function type are warnings with no edit. While fixing, `fn(` written by an earlier pass is read as a function type. `fix::apply_fixes(source, errors, warnings)` (errors first, then the warnings with the wider span first, so a cast or a declaration is mended whole before a type inside it and the next pass mends that type) takes each fix whose edits overlap none already taken (the first error wins; the rest count as skipped), `fix::fix_text` parses and applies until a pass applies nothing (at most 8), and `smith lint --fix FILE.smith...` (`runtime/front_flag.c`, no `--` among the arguments) runs that over files and prints `path: N fixes applied`. Tests: `tests/front_fix_test.smith`.

### parse_expr and parse_type (`front/parse_expr.smith`, `front/parse_type.smith`, #1145)

`parse_expr::expression(p)` and `parse_type::type(p)` are the hooks. Goldens: `tests/front/parse_expr/`, `tests/front/parse_type/`.
- Expressions: C's precedence and associativity with no comma operator; no recursion (operators wait on two stacks). `try e` is prefix. A `c::` path is a value, never a cast.
- A lambda parameter needs a type in today's syntax. `u8"..."` is a plain string; `u`, `U` and `L` strings and characters are errors.
- Reflection and packs (#1264): `x.[f]` and `x->[f]` are `reflect_member`; `pattern...` after a call argument or list item is `expand`; a second `...` is an error.
- `_Generic(control, T: value, default: value)` is `generic_select` (#1285); `typeof(T.[f])` reads its argument as an expression (#1284).
- New syntax (S5, #1446, with `Parser.new_syntax`): `named_arg` (`f(a = 1)`), a capitalized path called with named arguments is a construction (`Point(x = 1)`), `.name(payload)`, `(a, b)`, `[1, 2, 3]`, `p.*`.
- `x.to(T)`, `x.wrap(T)` and `p.cast(T)` are method-call spellings of the built-in conversions (#1811): `to` checks (a conversion that can lose a value traps), `wrap` keeps the low bits of an integer and never traps, `cast` reinterprets a pointer. They are read in both modes and `T` is parsed as a type. `x as T` is an error whose fix writes `x.to(T)` (`p.cast(T)` for a pointer type, the operand in parentheses unless it is a postfix chain); `&x` is `address_of`.
- `fn(x) { body }` and `fn[a](x: i32) -> R { body }` are lambdas; a `fn` with no body is a type (`fn_has_body` scans to the `{`). `if`, `match`, `for` and `{` start `stmt_expr`.
- An old lambda `[x](I32 a) -> I32 { }` has a fix-it (#1771, `lambda_warning` in parse_expr): the bracket group becomes `fn` (`fn()` with no parameters) and each `T a` becomes `a: T`; the result type has its own fix. Without it the new parser read the brackets as an array called with `(I32 a)`.
- A trailing block (#961): a `{` on the same line as a call's `)` is a lambda with no parameters, appended as the last argument; a condition head takes none at its top level (`Parser.head`).
- Old expression forms are warnings with fixes (F4, #1509): `(T)x` to `x.to(T)` (`x.cast(T)` for a pointer type), `p->x` to `p.x`, `*p` to `p.*`, `c ? a : b` to `if c { a } else { b }`, `(P){.x = 1}` to `P(x = 1)`, `{1, 2}` to `[1, 2]`, `i++;` to `i += 1`. A variable of a named type with an untyped list of only `.field = value` items, `P p = {.x = 1};`, is `p := P(x = 1)` (#1767); a `{` followed by `.name =` is that list even in smith's own syntax, where a block never starts that way.
- Forms with no mechanical fix stay warnings with no edits (a typed list with positional items, `++` as a value, a method chain). Two edits at one offset clash, so a nested fix lands on the next pass.
- `{1, 2}` is read as an array even where it initializes a struct (`P p = {1, 2};`): the parser has no types, so that one needs the checker.
- Types: qualifiers, paths, `auto`, `struct X`, `typeof`, `*`, `[:]`, `[N]`, applied types `Option(I32)`; `T[:][3]` is a slice of arrays. `Fn(R (P...))` is a function type. `Result(T, E)` (and its older name `Checked(T, E)`) is the checked result type (#1918).
- A pointer to a slice or array is written `T[:] *p` and `T[4] *p` (A7i, #1344; A7u, #1390); C's `T (*p)[:]` is an error that says what to write.
- Slice qualifiers (#1265): `T[const :]`, `T[restrict :]`, `T[const restrict :]` qualify the slice; `[static :]` and `[volatile :]` are errors.
- A call of a type function as a type (A7n, #1358): `maybe(I32) f()` reads as an `applied` type, decided by `parse_type::call_type_ahead`; `f(x);` stays an expression.
- Types in smith's own syntax (S2, #1443): `*T`, `*mut T`, `?T`, `[5]T`, `[_]T`, `[]T`, `[5,3]T`, `(A, B)`, `fn(A) -> R`, `fn_once(A) -> R`; lowercase built-ins are `named`.
- `parse_type::starts_new_type` says where a new type starts; `starts_type` is unchanged, so `*p = 1;` is still no declaration.
- Tuple types: `parse_type::tuple_ahead` scans tokens at the braces' depth: a `,` or `}` before any `;` makes a tuple, a `;` a record (`struct {}` is a record).

### parse_stmt (`front/parse_stmt.smith`, A4)

`parse_stmt::statement(p)` is the `statement` hook. Goldens: `tests/front/parse_stmt/`; tests: `tests/front_parse_stmt_test.smith` with stub hooks.
- It reads every statement form (blocks, `if`, `if constexpr`, loops, `switch`, jumps, labels, `defer`, `errdefer`, `static_assert`) and a declaration-or-expression with no symbol table.
- Nesting is an explicit stack of frames; after an error the rest of the statement is skipped and an `empty` placeholder stands in, so one mistake gives one error.
- New syntax (S4, #1445): a statement ends at `;`, at the end of its line or before `}`; `return` at the end of a line has no value.
- `if c { } else if d { } else { }` and `while c { }` take no parentheses and require braces; `if let .some(x) = e { }`; `for x in xs { }`; `for (i, x) in e { }`.
- `match s { .a => v }` is one arm per line, patterns joined by `,`. Old and new forms are told apart by fixed lookahead (`new_header`, `new_for`).
- Bindings (#1810 of #1428): `let a = 3`, `let b: i32 = 4`, `let mut c = 0`, `let _ = f()`, `const X = v` go through `parse_decl`'s `starts_binding`; `let` is the name `let` read in context (`let_at`: a name, `mut` or `(` follows), not a keyword. A binding with no `= value` is an error that says to give one or write `= undefined` (an `extern let` has none); `let (q, mut r) = g()` is `destructure`, `mut` marking one name (`Identifier.is_mut`). `name:` is a binding only with `Parser.new_syntax` (else a label).
- `yield x` (#1544) is `ast::yield_stmt`, dumped `(yield <value>)`; whether it stands in a generator is the checker's (#1545).
- `errdefer <statement>` is `errdefer_stmt` (A7l, #1356), read like `defer`; that it needs a function returning a `result` or `option` is the checker's rule.
- Old statement forms are warnings with fixes (F3, #1508): a `;` before a line break or `}` is removed; `if (c) s` and `while (c) s` lose parentheses and gain braces.
- C's three-part `for (init; cond; step) { body }` that is not a counting loop is `{ init; while cond { body step; } }` (#1768): the whole `for` is one edit, so the init's own fix runs in the next pass. A body with a `continue` (the step would be skipped), a body without braces and `for constexpr` get a warning with no edits.
- `for (T i = a; i < b; i++)` becomes `for i in range(a, b)`, `for (T x : xs)` becomes `for x in xs`; a fix's edits all apply or none.
- A `switch` whose labels are all `case .name:` with one expression statement and `break` each becomes `match` (#1569); any other `switch`, `goto` and labels keep a warning with no fix.
- `impl`, `&mut` and Rust's `?` at the start of a statement are warnings naming the smith form. A postfix `.` that starts a line after a finished statement does not continue it.
- A type used by its tag is a type name: `struct Pair a = {...};` is a variable of type `Pair`; a lone lowercase tag (`struct stat`) is C's and the error says `struct c::stat`.
- A range `for (T x : xs)` is read with `parse_decl`'s help: `for_stmt.range` holds `xs`, `init` the element and `index` the index declaration.
- GNU spellings are errors (#1363): extended `__asm__` says to use `optimize::opaque(x)`; a type used as a value, `name name` at a statement start, is a declaration (A7o, #1360).

### parse_decl (`front/parse_decl.smith`, #1147)

`parse_decl::declaration(p)` reads one declaration and consumes its own trailing `;`; it gives null after an error, with the parser past the bad declaration. `parse_decl::file(p, file)` reads a whole file.
- Reads: prefixes (attributes, `export static extern inline constexpr thread_local`), variables, functions (`constexpr Type T`, `auto... rest`, `...`), `struct`, `union` (bit-fields), plain and payload `enum`, `typedef`.
- Also `import` in three forms, `static_assert`, and `if constexpr (c) { decls } else { decls }` read with an explicit stack of open blocks.
- Anonymous records: an unnamed field `union { I32 a; F32 b; };` is read by `parse_fields` with an explicit stack of open records (16 deep, then an error, #1286).
- An enumerator that is a name alone is a plain value; a variant is a payload only when a type comes before its name (`Circle circle;`, #1267).
- Import alias (A7k, #1353): `import lib::text as tx;` keeps `alias`; `as` is a keyword only right after an import path (`parse_decl::at_as`).
- Imports (#1809): `import a.b.c` / `import a.b.c as x` (and `pub import ...`) are one-line statements that read into an `import_module` declaration, `alias` being `x` (empty when absent) and the path joined by `.` (`parse_import`); `import c <h.h>` / `import cpp <h>` / `import objc <h>` (`as name` optional) read into `import_c` with `language` and `alias`, the default name being the header's file name without the extension (`as` is required when that is not a name); `from a.b import x, y` and `from c <h.h> import x` read into `import_from`, and the checker binds each name in the module (`declare_from_names`; nested imported modules do not bind theirs yet). An `as` that repeats the default name is a warning whose fix removes it. The old `m := import a.b` is gone. `import a::b;` and `import a::b as m;` still read and record a warning that writes `import a.b` / `import a.b as m`; every `::` in a path (`parse_type::path`, an `@a::b` attribute) records `parse::warn_separator`, whose fix writes `.`. `.` before a type name continues a type path after a lowercase name (`build.Build`); `Color.Red` stays a member. `import(a.b).f(1)` as an expression waits for the checker ticket that reads modules as values.
- `Type T` without `constexpr` (A7m, #1357) parses as an ordinary parameter; the checker reports it (N1, #1339: write `constexpr Type T`).
- Globals (#1956): the fixer writes `export T x = v;` as `pub let mut x: T = v`, a `constexpr` one as `pub const x: T = v` and a global with no `export` with no `pub` (`let mut x: T = v`); a local stays `let`. After each pass `fix_text` writes the `//` comments as `#` once the text lexes as smith's own syntax, so the next pass does not read fixed text with the old parser (which dropped the `const` of `pub const x: T = v // c`). Test: `tests/front_fix_global_test.smith`.
- Declarations in smith's own syntax (S3, #1444; `let` since #1810): `let a = v`, `let a: T = v`, `let mut a: T = v`, `const X = v`, `pub let mut`, `extern let mut`, `pub`, `@attr`, `fn name(a: T, mut b: T, const n: usize, T: type, rest: ...) -> R { }`.
- Also `struct Name { field: T }` and `enum Name: u8 { low = 2 }`, one member per line with no `;` or `,`. New is told from old by the first one or two tokens.
- Keywords as names (#1770): after `.` a keyword is a member name (`x.module`, `t.type`) and in a struct body a field may be named with one (`module: str`); both positions are unambiguous. A local or parameter named with a keyword is still an error, and `int : 3` stays an unnamed C bit-field.
- A declaration ends with its line; a `;` or `,` there is an error with a fix that deletes it. After an error the parse skips to the end of the line (`recover_line`).
- `parse_decl::at_destructure(p)` and `destructure(p)` read `let (q, mut r) = f()` for the statement parser; `declaration` on one is an error.
- Old declaration forms are warnings with fixes (F2, #1507): `module a::b;` removed, `export` to `pub`, `I32 a = 3;` to `a: I32 = 3`, `R f(A a) {...}` to `fn f(a: A) -> R {...}`.
- Likewise fields and enum members to one per line, `typedef struct {...} N;` to `struct N {...}`, and `[[gnu::x]]` or `[[std::x]]` attributes to `@name`.
- No mechanical fix, so a warning with no edits: a prototype, a parameter with no name, an array suffix of `[]` or `[const :]`, a file-level array with no value or a `{...}` list (#1898), a bit-field, `extern`, a typedef whose tag has another name. An array suffix on a variable, field, parameter or alias moves in front of the type (`I32 xs[3]` is `xs: [3]i32`).
- Errors that name the fix: several names in one declaration, a function-pointer declarator, K&R parameters, implicit `int`, an array with no size, a C keyword that names a parameter (A7q, #1361).
- `__attribute__` is an error at each name (#1348, A7j): `[[std::x]]` where the std set has one (`gnu_to_std`), else "there is no Smith spelling of `format` yet".
- `[[std::inline]]`, `[[std::noinline]]`, `[[std::cold]]` (#1420) are ordinary attributes; `emit` turns them into `alwaysinline`, `noinline` and `cold`.

### parser (`front/parser.smith`, #1219)

`parser::parse(file, source)` reads a file into a `Parsed` (the `ast::Module` and the error list): tokenize, make a `Parser`, install the four hooks, call `parse_decl::file`. It never aborts.
- `parser::parse_new(file, source)` is the same for smith's own syntax (`tokenize_new`, `new_syntax`) and also returns the warnings.
- Blocks are `:` and indentation (#1845, split into #1856 to #1859). While they land, `parser::parse_new` chooses one of two paths per file in `takes_layout`, and nothing else branches on it: a file with an indented block goes through `lex::layout` and sets `Parser.layout` (the layout path: a statement ends with `newline`, a block is `:` then `indent` ... `dedent`), any other file keeps today's tokens and brace blocks. #1859 deletes the brace path and `takes_layout`. `parse_stmt` reads `if c:`, `else:`, `else if`, `while c:`, `for x in xs:`, `match s:` with `.a: v` arms, `if let`, `defer:` and `defer f` on the layout path, in an indented block or on the same line (`if a: x else: y`); `parse_decl` (#1857) reads `struct Name:` (fields indented, one to a line; `struct Name` alone is opaque, a `:` with no block is empty), `enum Name:` and `enum Name(u8):` (members indented) and `fn f(...) -> r:` (an indented block, one statement on the line, or nothing for an empty body) on the layout path, skipping the `newline`s between declarations and after an attribute on its own line; `parse_expr` (#1858) still reads braces, so a whole file takes the layout path only once it does. The programs under `tests/front/e2e/` are written in `:` blocks (#1859, first piece); a statement or binding whose last piece is an indented block (`f(x):`, `let r = do:`) ends at that block's `dedent`.
- `parse_expr` reads the same layout path (#1858): `fn(x): x * 2` and `fn(x) -> R:` with an indented block, `do:` as a block value (its last line is the value), the trailing block `f(x):` with an indented block under it, and the trailing lambda `f(xs) fn(a, b): ...` on the line of the `)`. A bare `name:` block call and a trailing block with its body on the same line are not read yet. After a `dedent` no postfix operator continues the operand, so `.b` starts the next match arm. `parse_type` has no block of its own.
- `compile::file` picks by the first token: the keyword `module` means today's syntax (`parse`, its warnings not shown), any other file is smith's own (`parse_new`).
- A lexer error is the one error, with an empty module.
- The corpus run, `tests/front_parser_test.smith`, parses every `.smith` file of `tests/smith-lang/cases` and checks one outcome per file against `tests/front/parser/cases/<case>.txt` (#1352).
- An outcome is `ok`, `error: <message> (line:column) [class]`, `C, out of scope` or `skipped: <issue>`; classes (`gap_a1`, `removed_b`, ...) are described at the top of the test.
- A rejected file with no class fails the test. `FRONT_UPDATE=1` rewrites only the case files whose text changed and keeps each class on disk.

## Fix-its and `smith lint --fix` (`front/fix.smith`, #1442, #1516)

`smith lint --fix FILE.smith...` (`runtime/front_flag.c`, no `--` among the arguments) mends files in place and prints `path: N fixes applied`.
- `fix::apply_fixes(source, errors, warnings)` applies the fixes of the errors, then of the warnings; a fix is all its edits or none, taken when no edit overlaps one already taken.
- `fix::fix_text(file, source)` parses and applies until a pass applies nothing (at most 8); `fix::fix_file(path)` does it on a file; `fix::fix_run` is what the driver calls.
- The parse fixes are F2 (declarations), F3 (statements) and F4 (expressions) above, each with its goldens `old_*.txt`; F1's are the warnings those record.
- The checker's fixes (M1, #1516): `fix_text` runs `types_decl::check_module` when the parse has no error that is not a warning; a `types::Error` has a `fix` too (`types::fail_fix`).
- A discarded value (#1915): a cast to `Void` (`(void)x;`) is refused with the fix `_ = x` (`discard_fix` in `front/types_decl.smith`: the cast's head, up to its operand, becomes `_ = `).
- Assigning to a name declared without `mut` inserts `mut ` before its name (a local or parameter, not `const`, not after an attribute).
- Writing through a `*T` turns the declaration's `*T` into `*mut T`; when that pointer is `p := &x` of a constant `x`, the fix adds `mut` to `x`.
- No fix removes a `mut`. In smith's own syntax `*T` is read-only (`pointee_const`) and `*mut T` writable.
- Preprocessor lines (#1748, `front/fix_directive.smith`): the error of the first `#` carries one fix for every simple line in the file: `#define N 4` becomes `const N = 4` and `#include <h.h>` becomes `import c <h.h>`. `#if defined(__APPLE__)` / `defined(__linux__)` (also `#ifdef`, `#ifndef`, `!`, `#elif`, `#else`) around whole top-level declarations becomes `@when(target.os == .macos)` (`.linux`, `!=` for the `#else`) on each declaration, the `#if`/`#else`/`#endif` lines removed (#1801); an `#if` about anything else, inside a function, nested or around another directive keeps the error with no fix, as do `#pragma`, `#error`, a function-like `#define` and `#include "h"`.
- Method calls (#1797, `front/fix_method.smith`, skill `smith-lang-methods`): after a clean check, `fix::checker_fixes` makes one fix for each call `m.f(a, ...)` where `m` is an imported module, `a` has a record type `m` defines and `f` is a `pub` function of `m` taking that type first; it writes `a.f(...)` (the receiver of `&x` is `x`). It needs the modules read, so `fix_run` sets a loader over `src/` and `lib/`; without a loader (`fix::set_loader`) nothing is rewritten. Tested by `tests/front_fix_method_test.smith`.
- Tests: `tests/front_fix_test.smith`, `tests/front_fix_mut_test.smith`.

## The checker

### front::types (`front/types.smith`, #1220 J1a)

The types of the front end and the checker's entry point; the API is the top comment of the file.
- `Ty` is interned in a `Table`: two types are the same exactly when their `TypeRef`s are equal. A record and an enumeration are nominal; every other type is equal by structure.
- `const` on a pointee, array element or slice element is part of the type; `const` on a variable is `Symbol.is_const`.
- Sizes belong to the target: `layout(&target, t)` takes a `Target`, never the host. `Usize` and `Isize` are the pointer's width.
- `Scopes` is a stack of `Symbol`s (`declare`, `lookup`, `lookup_path`); a name from a C header is declared as `c::name`.
- Records and enumerations are declared first, then typedefs, then fields, signatures and variables, so use before declaration works for records only.
- `types::check(module, options)` returns a `Checked`: the table, the scope, the errors and a side table of expression types (`set_type`, `type_of`), not a field of the ast.
- `options.bodies` is the hook that checks function bodies (`types_decl` fills it). A construct not typed is reported as `not yet typed: <kind>`.
- `typeof` (#1747): `typeof(x)` is the type of the parameter or variable `x` in scope, and a function's parameters are in scope for the ones after them and for its result (`fn larger(a: num, b: typeof(a)) -> typeof(a)`); `typeof(T)` is `T` for a plain type name. Any other operand is `not yet typed` (types.smith cannot reach the expression checker, and has no recursion).
- Tuples (#1557): the kind `tuple` is interned by its elements (`tuple_of`), shown `(I32, Bool)`, its slots named `0`, `1`, so `t.0` is a `member`. Named tuples (#1912, piece 1 of #1820): `(count: i32, ms: f64)` is a tuple type whose slots carry the written names (`tuple.names`), `(count = 3, ms = 1.5)` is a `tuple_lit` whose items are `named_arg` nodes; the name is part of the type's identity (names and types in order), `t.count` is a `member`, and the emitter finds its index in the checker's slots. Naming some elements and not others is a parse error.
- `stream::Stream(T)` (#1613) is the type kind `Kind.stream`, interned by `types::stream_of`, shown `stream::Stream(i32)`, one pointer in layout.
- Smith's own syntax (#1512, C5): `Checked.new_syntax` is set around types written there (`Param.is_new`, `variable.is_new`, `FieldDecl.is_new`).
- There the lowercase built-ins (`i8`..`usize`, `f32`, `f64`, `bool`, `char`, `str`, `void`, `type`) equal `I8`..`Usize`, `F32`, `F64`, `Bool`, `Char`, `Str`, `Void`.
- In a function with smith's own syntax the same names are also types inside its expressions: `x.to(bool)`, `sizeof(char)`, a lambda's `fn(q: bool) -> bool: q` (#1960).
- The concepts `int float num signed unsigned pointer` are `concept_type` types (an error as a field type). In today's syntax `char`, `bool` and C type names are errors.
- `[5,3]T` equals `[5][3]T`, `[,3]T` is a slice of `[3]T`, `[,]T` is a `block`, `?T` is the option type, and `Line := []u8` is an alias by substitution.
- A module-level `a := 3` takes `i32` (`f64` for a float literal) from a literal initializer. A field's `is_new` is not read yet (#1531): a field always counts as new.
- Not yet typed: `typeof`, a bit-field, `if constexpr`, a compile-time or rest parameter. An array length is typed when it is an integer literal, a constant (`const`, or a `let` that is not `mut`) whose initializer is one, or `+ - * / % << >>` of these (#1918); the emitter still wants a literal (#1917).
- No recursion: `resolve`, `layout` and `show` use explicit stacks. Tests: `tests/front_types_test.smith`, golden `tests/front/types/`.

### front::types_expr (`front/types_expr.smith`, #1220 J1b)

`types_expr::infer(c, e)` types from the expression alone; `types_expr::check(c, e, expected)` where a type is expected. Both record every sub-expression's type with `types::set_type`.
- An error returns the invalid type, which silences the expressions around it.
- Down: `expected` types an unsuffixed literal when it fits (`U8 big = 300;` is an error naming the range), `null`, `.none`, `.red`, a `{...}` list, a lambda and both sides of `?:`.
- Next to a typed operand (`x + 1`) a literal takes the operand's type; any other pair of different types is an error that names the cast to write.
- Typed: literals, names, operators, assignment, `.` and `->`, `a[i]`, `a[i:j]`, calls (count checked), casts, `sizeof`, `alignof`, `countof`, `?:`.
- A payload enumeration (#1774) is an `enumeration` whose `has_payload` is set: each slot's type is what the enumerator carries (`void` for a bare one). `.circle(x)` checks `x` against it (`.circle` alone or `.empty(x)` is an error), a `match` arm `.circle(c)` binds `c` as that type, and coverage is by enumerator. The emitter still says `not emitted yet: a payload enumeration`.
- Not yet (`not yet typed: <kind>`): `try`, a type as a value, nested designators. A name from another module is typed only when the checker is given a loader (next bullet); without one it stays `not yet typed`.
- Modules are values (#1677): `types::Options.load` (`check_module_with(m, target, load)`) gives the tree of a dotted path (`std.math`). `import a.b as m` binds a `module_name` symbol, and the declarations of `a.b` (and of the modules it imports, once each, so a cycle ends) are declared in the same table under the prefix `a::b::`; `m.name`, `m.b.name` and a type path `m.T` read through the module symbols (`types::through_modules`), and only a `pub` name is found. A missing name is "`a.b` has no public `x`" with the closest name. `sin := m.sin` and `Arena := m.Arena` are a second symbol for the same declaration (a direct alias, no copy). A module name used as a value (`mut x := m`, passed, compared) and a local named like a module are errors. Generic functions of an imported module are not declared yet. The driver (#1714) gives the checker a loader (`compile::load_module`: `a/b.smith` under the source folder, then the library folder, named by its path) and emit calls a function through a module value (`m.sin(x)`, `m.re.sin(x)`, `sin := m.sin` then `sin(x)`): it declares an external prototype named by the module's path and the function (`a::b::sin`, the symbol the old pipeline uses) when the first call needs it, and a module alias is not emitted as a global. A record of an imported module is emitted (#1935): a type `m.Vec` is looked up through the module symbols (`emit::type_of_imported`) and declared once under the checker's name (`geom::vec::Vec`, `memory::imported_record`), and the signature of an imported function is typed in its own module's scope (`Emitter.import_scope`, so `fn dot(a: Vec)` of that module names its record). Fields are read in the importer's scope, so a field naming another record of that module is not emitted yet. Not yet: an enumeration of an imported module, and aliases of types.
- `Hooks.convert` (set with `set_hooks`) is asked whenever a value of type `from` meets `to`; unset, the default is exact equality. `types_decl::install` wires `types_convert::convert` in.
- Values (#1511, C2a): `if`, `if let`, `match` and a block used as a value (`stmt_expr`) are typed here; the last line of a block is its value.
- Every branch has the expected type; a branch with no value makes the whole `void`. `match` is exhaustive over an enumeration or `?T`: a missing case is listed, a repeat is an error.
- `if let .some(x) = o` binds `x` in its `then`. Locals, `return` and loops inside such a block go to `Hooks.statement`.
- Construction (#1513, C2b; #1813): `Point(1, y = 2)` is a call whose callee names a struct: positional arguments fill the fields in order, then named ones in any order (Python's rules); a positional argument after a named one, more positional arguments than fields, an unknown field and a field given twice are errors that name the struct. A field left out takes its default (`port: u16 = 443` on the field, #1847); with no default it is an error naming the struct and the field, and `T()` is valid only when every field has one. A default must be a compile-time constant (a literal, a `const`, an operation on them): a call or an earlier parameter is the error "a default must be a compile-time constant". A parameter takes a default the same way (`fn connect(host: str, port: u16 = 443)`), may be left out by position or by name, and a parameter without a default may not follow one with a default, except after the `;` of the list (#1913): `fn connect(host: str, port: u16 = 443; timeout: u32 = 30, retry: u32)` reads the parameters after `;` as keyword-only (`Param.is_keyword`, set in `parse_new_function`); the checker (`check_keyword_only`) says "`timeout` is keyword-only; write `timeout = ...`" for a positional argument in such a slot and, for one without a default, "`retry` is keyword-only and has no default" when the call leaves it out, and `required_arguments` counts those parameters apart from the positional ones. A struct with any private field is built outside its module only by a function of that module, named in the error (`has private fields; build it with a function of `lib.math`: `init``); a struct of `pub` fields is built anywhere. The parser reads `Name(` followed by a literal, a `name =` or `)` as a construction; `Point(a, b)` with names is read as a type application (`type_value`), and the checker builds the call it stands for when the path names a struct and every argument is a name or a value (`type_value_call`, checked in its place); a type function (`Pair(i32)(1, 2)`) is untouched.
- Gathered and spread arguments, parser half (#1983, of #1914 and #1820): a parameter `xs...` is a positional pack and `; kw...`, the last parameter after the `;`, gathers the leftover named arguments (`Param.is_pack`, `Param.is_gather`); at a call, `...` arguments after a `;` are named spreads (`expand.named`), and a second `;` is an error. The checker only reports "not yet: positional packs" or "not yet: gathered keywords" for a declaration that has one; spreading is #1984 and gathering #1985.
- A call to a function may name its arguments (`area(h = 2, w = 1)`); unknown, repeated or already-filled names are errors.
- `[1, 2, 3]` is an array of its elements' type, or checked against an expected array or slice.
- In smith's own syntax (`Symbol.is_new`) constant is the default: assigning to it says "`x` is constant: declare it `mut x`"; `&x` is a `*mut T` only when `x` is `mut`.
- Writing through a `*T` says to take a `*mut T`; `p.x` needs no `->`. A variable of today's syntax keeps today's rules.
- No recursion: a work stack of frames (node, expected type, stage). Tests: `tests/front_types_expr_test.smith`, golden `tests/front/types_expr/typed.txt`.
- Rewrite goldens with the runner's `--update`: `build/<OS>-<arch>/front-project/build/tests --update --test 'front_types_expr_test::*golden*'`.

### front::types_convert (`front/types_convert.smith`, #1220 J1c)

The conversion rules (#414's baseline, #398) as pure functions over `types::Ty`; the rules are the top comment of the file.
- `convert(table, from, to, lit)` says whether a value may stand where `to` is expected; `lit` (`no_literal()`, `constant(n)`, `constant_unsigned(n)`) lets `U8 small = 200;` pass.
- `cast_ok(table, from, to, lit)` says whether `(to)x` may be written. Both return a `Verdict {ok, kind, message}`; `kind` is the `How` of the rule (`widening`, `constant_fits`, `to_some`, ...).
- Implicit conversions never lose a value (owner, 2026-11-01, #1328): same-sign widening, unsigned to wider signed, `F32` to `F64`, an integer whose bits fit the mantissa.
- Every conversion that could round, every float to integer, `Bool` to integer and enumeration to integer needs a cast; const is added, never dropped.
- A fixed array never converts to a bare `T *`; it converts to a slice `T[:]` keeping its count. Only `Char[:]` converts to `Str`; `U8[:]` and `I8[:]` convert by the cast `(Str)xs`.
- A value converts to `Option(T)`'s `.some` and `Result(T, E)`'s `.ok`; `Option`, `Result` and `null` do not convert to a `T`.
- A written cast that can lose a value asserts that it fits, checked in every build (`Verdict.checked`; `smith prove` removes the check when it proves it).
- That covers integer narrowing, a sign change out of range and float to integer; widening, same width and sign, constants that fit, float casts, enumerations, `Bool` and pointers have none.
- `types::cast_checked(r, expr)` returns `{needed, from, to}` for the emitter (K2). A constant cast that can never fit is a compile error that suggests `wrapping::to(T, x)`.
- Casts between enumerations, `Bool` and integers, and pointers are explicit; a pointer and an integer cast only at pointer width; dropping const and casts to `Void` are refused.
- Tests: `tests/front_types_convert_test.smith`, one per message.

### front::types_decl (`front/types_decl.smith`, #1220 J1d)

Declarations, statements and bodies, joining J1a to J1c. `types_decl::check_module(module, target)` checks a whole module and returns the `Checked`; the API is the top comment of the file.
- It wires the hooks (`install`), runs `types::check` with `Options.bodies = check_body`, checks module-level initializers and lambda bodies, then takes the hooks out.
- Statements: blocks (a scope each), expression statements, locals (a written type or `Auto`), `if`, `while`, `do`/`while`, `for (init; cond; step)`, `return`, `defer`, labels, `break`, `continue`, `goto`.
- A condition is checked against `Bool`; a `return` value against the result type. The walk is an explicit work stack.
- Lambdas are checked after the body that holds them: the hook keeps a copy of the scopes, and `drain` walks each body with the parameters from the expected `Fn(F)`.
- `switch` is typed (#1916): the value, each case value against it (a payload enumeration's variants are not checked by name) and each case body in a scope of its own. A type used as a value is `Auto`, and `countof` of an `Auto` is accepted: the instance decides. Not yet typed: `static_assert` in a body, `if constexpr` and `for constexpr`, a local type or function, a `c::` name (#1124). A function that falls off its end is not reported.
- Spelling (K5, #1347): `Char` is a builtin 8-bit unsigned type of its own; `char` and `bool` in a type position are errors in today's syntax. A character literal has type `Char`.
- `const` and `constexpr` bindings are known at compile time (#1510, C1): `const a = n` with `n` not constant is an error naming `const a` (`require_constant`).
- In smith's own syntax (`is_new`): a name that hides an enclosing one is an error naming the earlier declaration by byte offset; `let _ = f()` declares nothing.
- A call whose result is not `Void` and is unused is an error (`write let _ = ...`), judged once the function has a parameter or an earlier local in that syntax.
- `let (q, r) = f()` and `let (q, mut r) = f()` (#1558, `destructure_statement`) split a tuple: `_` discards one, a count that differs from the tuple's is an error naming both.
- `undefined` (K3b, #1334) is taken only as a local's whole initializer with the declared type (`types_expr::is_undefined`, `local_variable`).
- Tests: `tests/front_types_decl_test.smith` (all of `lib/std/ascii.smith` checks clean), goldens `tests/front/types/decl_*.txt`.

### front::types_fn (`front/types_fn.smith`, #1515, C4)

Functions and lambdas in the checker; the rules are the top comment of the file. A lambda against an expected `fn(A) -> R` has that type, like one against `Fn(F)`.
- A parameter of type `fn_once(A) -> R` is linear (#1108): `check_once(c, params, body)` follows every path; types_decl calls it per function and types_expr per lambda.
- Reported: a second use ("used again"), a path that leaves before any use (`return`, end of body, `try`: "not used on every path"), a use inside a loop ("once per turn").
- A use is a call, or the name passed on or captured; an `fn(A) -> R` parameter may run any number of times.
- A stored lambda (`Fn(F)`) passes where `fn(A) -> R` is expected when the signatures are the same (#1649); `conform` in types_expr says so before the convert hook. A different signature is an error naming both; `fn_once` stays apart.
- A lambda written as an argument of a call is non-escaping and captures by reference (`note_arguments(args)` tells which are arguments).
- Any other lambda is stored (a variable, field or result): captures are copied by value, with no capture list (lead's decision, 2026-11-02).
- `check_lambda(c, e)` refuses a stored lambda that captures a local pointer or slice into the current function's stack (`p := &x`, `s := xs[1:3]`), naming the capture. A pointer parameter is allowed.
- Not yet: `fn_once` locals and fields, a lambda with no captures as a C function pointer, how a stored lambda's captures are freed (asked of the lead).
- No recursion: the walk is a work stack. Tests: `tests/front_types_fn_test.smith`.

### front::types_stream (`front/types_stream.smith`, #1545)
- A generator is a function whose written result is `stream::Stream(T)` and whose body has a `yield` outside any lambda; a stream-returning function with no `yield` is an ordinary function.
- `yield x` is valid only in a generator, and `x` converts to `T` by the ordinary conversion. Elsewhere (a lambda included) it is an error that says to return a stream or remove the yield.
- A generator ends with a plain `return`; `return v` is an error that says to `yield v` first. Its body is checked as the body of a function with no result.
- `stream::next(s)` on a local `stream::Stream(T)` has type `option(T)`; `stream::close(s)` closes it (#1723). The call rule lives in `types_expr` (`stream_callee`), because `types_stream` imports `types_expr`.
- No recursion: the search for a `yield` walks statements with a fixed stack. Hooked from `types_decl`; not wired into `smith cc`. Tests: `tests/front_types_stream_test.smith`.

### front::types_iter (`front/types_iter.smith`, #1514, C3)

Iteration in the checker; the rules are the top comment of the file.
- `for x in xs` binds `x` to the element of an array, slice or block, or a byte of a `str`. A number, pointer, record or option is "not iterable" and the error names the fix (`range(n)`, `p[0:n]`).
- `range`, `refs`, `enumerate` and `zip` are functions returning streams, known by name when nothing in scope has that name (until the library has them).
- `range(a, b)` yields its integers; an unsuffixed literal argument takes the type of the non-literal ones (`range(0, n)` with `n: u32` yields `u32`; all literals give `i32`; mixed non-literals are an error, #1606).
- `refs(xs)` yields pointers; `zip` and `enumerate` yield tuples, so one name is refused and `for (i, x) in ...` is "not yet typed" (#1557).
- The loop's names live in the loop's scope and may not shadow. `zip` of unequal lengths is a checked failure at run time (the lead's recommendation, not yet the owner's word, #1514).
- `types_iter::built_in()` also takes `stream::Stream(T)` as iterable with element `T` (#1613); the `yield` rules are `types_stream` (#1545) and the emitter is #1546.
- Not yet: a `for` that builds a value, `continue` as a filter, `sum(for ...)`, user iterators and `utf8::chars`. Tests: `tests/front_types_iter_test.smith`.

## front::emit (`front/emit*.smith`, #1119)

`emit::module(context, &m, options)` turns a `front::ast` module into a verified LLVM module through LLVM's C API, or an `Error` with a span. The protocol is the top comment of `front/emit.smith`.
- Rules: no recursion (a work stack and a value stack); a handler pops operands and pushes one value; a node that needs a child first pushes a `then` continuation; pass 1 declares, pass 2 emits bodies; allocas go in the entry block.
- `Target` and `Options` pick the triple and layout. Symbols are the module path joined with `::`; exported (or `pub`) is external, the rest internal.
- Handlers dispatch with `if` chains, never `case .name:` ([body-files](body-files.md)); read payload fields into locals before any call.
- No arena around LLVM: LLVM allocates with `malloc`, which smith routes to the allocator in force, so a test must not hold an arena while it emits or runs the JIT.
- Tests run the result in LLJIT (`emit_jit.smith`) and compare golden IR (`tests/front/emit/`).

**Files**: `emit.smith` (passes, the walk, routing), `emit_state.smith` (shared types, `Emitter`, hooks; its own file to break an import cycle), then one file per area, no two tickets sharing one.
- Slices `[]T` and `str` are `{ data, count }` (#1623): a string literal is a constant `str`, `s[i]` checks the index against the count in every build, `s[a:b]` and `countof(s)` read the pair, and `for x in s` walks it.
- Multi-dimensional arrays (#1710): `[5,3]T` is `[5][3]T`, `[,3]T` a slice of `[3]T`, and `[,]T`/`[,,]T` (up to 8) a block `{ ptr, [N x usize] }`, one count per run-time dimension, row-major, strides derived. `x[i, j]` is `x[i][j]`; a row of a block is a block of one dimension fewer (a slice for the last two) sharing the elements, checked per dimension; a fixed `[N][M]T` converts to a block; `countof(x)` is the first count.
- `emit_vector` (`vec(T, N)`), `emit_arith` (integer expressions), `emit_control`, `emit_call`, `emit_memory`, `emit_debug`, `emit_option`, `emit_iter`, `emit_match`, `emit_value`, `emit_tuple`, `emit_lambda`, `emit_jit` (test helper).
- Three hooks in `Emitter`, set by `emit::module`, let a handler reach `emit.smith` without importing it: `state::type_of` (a type expression), `state::type_of_node` (the checker's type of a node) and `state::convert`.
- `state::convert(e, value, to, span, site)` pushes `value` as a `to`: as is, widened by the checker's rules, or wrapped for an option; else the site's error (`initializing X with Y`).
- `type_of_node` with no `Options.checked` is an Error, "this needs the checker's types".

**The wave-3 table** (#1582 of #1428): one module per new area, each handler with its final signature; the ticket that fills a stub edits that file and never `emit.smith`.

| File | Emits today | Left | Issue |
| --- | --- | --- | --- |
| `emit_option` | `?T` and `Option(T)` (`{ i8 tag, T payload }`, tag 0 is `.none`), `Result(T, E)` (`{ i8 tag, T, E }`, tag 0 is ok), `.some(x)`, `.none`, `if let`, `wrap`, `is_some`, `payload` | `?*T`, `match` over `?T`, `if let` as a value, `try`, `==` of options | #1584 (filled) |
| `emit_iter` | `for x in xs` over a fixed array, `refs(xs)`, `range(n)`, `range(a, b)`, `range(a, b, step)` with a positive literal step | `enumerate`, `zip`, tuple pattern, slice, `str`, block, stream, array value, other steps: `not emitted yet` | #1586 (filled) |
| `emit_match` | enumerations (an integer `Ty` of the underlying type, enumerators from 0 or as written; `emit_memory` and `emit_call` find one for `as` and parameters), `.red`, `match` over an enumeration (one `switch`, `unreachable` default) or `?T`, as statement and value | a payload enumeration, `match` over `Result`, `if let` over an enumeration | #1585 |
| `emit_value` | an `if` with an `else` and a block used as a value: one alloca slot (the hint's type, else the join of the branches' checker types), branches convert and store, `endif` loads it; a branch that ended stores nothing; no branch reaching the end gives `unreachable` and `undef` | `for`, `while`, `if let` as a value, a `match` with no type written | #1618 (filled) |
| `emit_tuple` | tuple types `(A, B)`, `(T,)`, `()` as an LLVM struct, `(a, b)`, `t.0` and `t.name` as a value and a place, named tuples `(x = 1)`, `(q, r) := f()`; the lexer reads `t.0` and `t.0.1` as an index | `==` between tuples, tuples of slices or `fn` values, `for (i, x)` | #1619 (filled) |
| `emit_lambda` | `fn(A) -> R` and `fn_once` types (`{ ptr code, ptr context }`), lambdas as internal functions, calls of `fn` values (#1620) | a stored lambda with captures, a function named as a value | next batch |
| `emit_stream` | `stream::Stream(T)` (one `ptr`), a generator as a ramp and a body on the stream's own stack, `yield`, `stream::next(&s)` (`?T`), `stream::close(&s)` (#1546; see [stream](stream.md)) | `for` over a stream, a generator with debug info | #1546 |
| floats, slices, `str`, array literals | no route yet: `emit.smith` answers `not emitted yet: this kind of expression` | everything | none yet |

- Routing in `emit.smith`: `type_of` sends `?T`, a tuple and a `fn` type to their file and a one-segment name to `match::type_enum` before records.
- A `variant` goes to `emit_option` when the hint or the checker's type is an option, else to `emit_match`; a `stmt_expr` goes to `match::value_of` for a `match`, else `emit_value`.
- A member named by a digit is a tuple member; `declare_all` sends an `enum` to `enum_decl`; a function's last line that is a `match`, block or `if`/`else` with values is returned as `value_of`.
- The state of the table comes from the files; a later docs pass updates it. Tests: `tests/front_emit_wave3_test.smith`, `tests/front_emit_option_test.smith`, `tests/front_emit_for_test.smith`.

**Arithmetic** (`emit_arith.smith`): literals, names, locals, `+ - * / %`, shifts, comparisons, assignment.
- Signed `+ - *` and negation use `llvm.*.with.overflow`; division and remainder check zero and `MIN / -1`; a shift checks its width; unsigned wraps.
- Each failing check goes to its own `fail` block that calls a `.smith` report function, then to the function's one `trap` block (K6, #1389, #1542).
- The reports are in `lib/std/contract.smith` (`fail_cast`, `fail_index`, `fail_overflow`, `fail_negate`, `fail_divide`, `fail_shift`): `index 7 is outside 0 to 3`, `7 / 0 divides by zero at f.smith:4:14`.
- `at` is `<file>:<line>:<column>` of the node's `Span.start`, from a line table made whenever `Options.source` is not empty. The branch to `fail` carries `!prof` weights (1 against 2000, #1550).
- `emit_jit` defines the report symbols in the JIT (`define_checks`); an object links `std::contract`.
- Built-in type names (#1576): smith's `i8 .. usize bool char` and today's capitalized names give the same `Ty`; `isize` and `usize` are pointer-wide on the target (64 bits with no data layout).
- `Char` is an unsigned `i8` in the IR with the layout of `U8` (K4, #1345); a character literal is that constant whatever the hint says.

**Control flow** (`emit_control.smith`, #1131): `if`/`else`, `while`, `do while`, `for`, `break`, `continue` (through `Emitter.loops`), `&&` and `||` (short circuit with a phi), `!`, `?:`.
- A condition is a `Bool`: an integer condition is an Error, "a condition is a Bool; write `x != 0`". A function whose end is reached without `return` branches to the `trap` block.
- `defer body` (#1583) waits on `Emitter.defers` with its scope depth and runs on every way out: the end of its block (newest first, skipped when the end is unreachable), `return` (after its value is computed, before `ret`), and `break`/`continue` (the defers deeper than the loop's `scope_depth`, then the jump); a deferred body is emitted once per way out.
- `switch`, `goto` and labels are `not emitted yet`.
- `for x in xs` (`emit_iter`) lowers to the plain indexed loop with no stream object: blocks `forcond`, `forbody`, `forstep`, `endfor` and a hidden counter in the entry block.
- `range(a, b, step)` adds through `llvm.*add.with.overflow` and ends the loop on overflow; a zero, negative or run-time step is `not emitted yet` (lead's decision, 2026-11-02).

**Memory** (`emit_memory.smith`, #1133): pointer, array (`T[N]`, N a literal) and record types (`Decl.record` becomes a named struct `<module path>::<name>`); a union, bit-field or unknown record is an Error.
- Every lvalue is a place (address plus stored type): `&x`, `*p`, `a[i]`, `s.f`, `p->f`; a read is the place plus a load; a base that is no lvalue is spilled.
- An array's index is checked against its count (a constant index in range needs no check); a pointer's is not.
- Array literals (#1624): `[a, b, c]` is an array value, its items converted to the element type; where a slice is expected it is stored in a fresh entry-block slot of the function and the value is `{ slot, count }`, so a literal evaluated again in a loop overwrites the same slot. `[]` is `{ null, 0 }` or `[0]T`, and an error with no type.
- `(Point){.x = 1}` and `{0, 0}` fill a zeroed temporary; `null` takes the expected pointer type; `sizeof`, `alignof` and `countof` read the target data, never the host.
- A file-scope variable is a global with a constant initializer, read and assigned by its bare name (`memory::is_global_name`). `emit::module` calls `memory::release`.
- Checked casts (K2, #1329): `Options.checked` is the checker's `types::Checked` (null: no cast is checked). `cast` asks `types::cast_checked(...).needed`, then converts, converts back and compares.
- `p.x` through a pointer loads it and takes the pointee's field, as `->` does (#1580); `pp.x` on a pointer to a pointer is an Error. `p as usize` is `ptrtoint`, `n as *u8` is `inttoptr`, pointer-wide only.
- Affine values (`@affine`, #1986 of #1972, part of #1631): `front/affine.smith` (`affine::check_body`, called at the end of `types_decl::walk`, after the body is typed) reports a second use of a value of an `@affine` record (`Ty.affine`, set by `declare_nominal`): the same messages as `shim/affine.cpp` (`smith-affine: 'key' is used again; it was already used at line N`, with the note `'key' was first used here`). A path is a name or fields through values (`h.key`, never `p->key`); a use is an affine-typed path read, passed or returned, or `&key`; `x = v` makes `x` fresh; the branches of `if`, `if let`, `match`, `?:`, `&&` and `||` start from the state before them and join, except one that ends in `return`, `break`, `continue` or `goto`; a `defer` body counts where it is written. Loops are read once as straight-line code, `switch` and lambdas are not read (loops, elements and slices: #1987; payload-enum narrowing: #1988). A struct that holds an affine member is not affine itself. `types::Error` has a general `related` span with a `related_message`, printed by `front::compile` as a `note:` line, and a `$line` in the message becomes the line of `related` (`types::fail_related`).
- There are no zero values (#1810 of #1428): a declaration with no value is an Error (checker and emitter); `= undefined` makes the slot with no store, except that with `Options.fill_undefined` (`compile` sets it: objects are made at optimization 0) the slot is filled with the byte 0xAA (`declare_undefined`). The fix-it writes `let mut x: T = undefined` for `T x;` (`= 0` for a static or a file's number, which C zeroes). A read of an `undefined` local (`let mut x: T = undefined`) before any write is an error (`types_undefined::check_body`, #1854): the first later statement of the same block that reads it when none before it wrote it; a statement that both reads and writes it, a write by `&x`, `x[i] = e`, `x.f = e`, passing it to a call, and code the pass does not read (lambdas, `if` as a value) all allow it. At most 32 undefined locals of one block are followed.
- Lint checks of smith's own (`front/lint.smith`, #1976 of #1631), run by `types_decl::check_module` as checker errors with the message text of the clang-tidy checks they replace: `smith-unsafe` (a call of C's `setjmp`, `longjmp`, `sigsetjmp`, `siglongjmp` or `alloca`), `smith-mem` (C's `memcpy`, `memmove`, `memset`, `memcmp`, `malloc`, `calloc`, `realloc`, `free`) and `smith-bounded-loop` (`while true`, a constant `for` or `do ... while` outside `main`). A call is C's when written `c::name`, through a C header's alias or stem, or imported by `from c <header> import name`; `mem::free` and a module's own `free` are not. A function that calls `contract::trusted()` is skipped. `smith-module-library` (`import c <smith/arena.h>`, and a `c::name` that the header's `// smith-module:` and `// smith-names:` lines pair with a module's name, in expressions and in the types of parameters, results, fields and `let`s) reads headers through `lint::set_header_text(fn(header, angled) -> text)`; with none set the check is off, and no driver sets it yet. Not yet: the `# NOLINT(smith-...)` opt-out (the checker holds no comments).
- `vec(T, N)` (#1630): `T` a number type, `N` an integer literal of 1 or more; `vec(f32, 4)(1, 2, 3, 4)` or a list `[1, 2, 3, 4]` where a vector is expected; element-wise `+ - * / %` and the comparisons (a `vec(bool, N)`) between two vectors of one type or a vector and a scalar of its element; `v[i]` and `v[i] = x` (a constant index out of range is an error, a run-time one is checked). Emitted as one `<N x T>`. Not yet: signed integer lanes' arithmetic (the overflow checks), integer division, bitwise operators and unary `-` on a vector, `v += w`, swizzles, writing a lane of a `vec(bool, N)`.
- `fn` values (#1620): a lambda is an internal function `<symbol>::lambda.<n>` taking its context first; a lambda written as a call argument captures locals by reference, one that captures nothing has a null context, `f(a, b)` on a `fn` local is an indirect call. Still not emitted: slices, unions, pointer arithmetic and comparison, a stored lambda with captures, a function named as a value.

**Calls** (`emit_call.smith`, #1132): `f(a, b)` names a function or a prototype (a `Decl.function` with no body) of the module; a path is joined by `::` and a prototype named with `::` is its own symbol. A method call `x.f(a)` (#1796 of #1793) is emitted as the call the checker recorded (`types::method_of`): `f(x, a)` or `f(&x, a)`; `f` of the type's module links as `lib::list::f`, as `m.f` does. A record of another module is emitted as in the modules paragraph above (#1935). Tests: `tests/front_emit_method_test.smith`.
- Arguments run left to right with the parameter as hint; count and types must match (an Error at the argument), after `state::convert`.
- `Void` is an LLVM `void`; a `Void` function's reachable end emits `ret void`, any other function's traps. A bodiless function may be variadic (`...`, extra arguments after C's default promotions) and `extern`; a variadic function with a body is not yet emitted.
- Named arguments (#1579): `name = value` fills the parameter of that name (the checker's `argument_slot`), evaluated in written order; unknown or doubled names are Errors.
- A call whose callee is a record builds it, `Point(x = 1, y = 2)`: a field left out takes its default, written as one more named argument (`insertvalue` per argument), and a parameter left out takes its default the same way; a positional, unknown or doubled field is an Error.

**Functions in smith's own syntax** (`emit.smith`, #1577): `pub` is external linkage; `@inline`, `@noinline`, `@cold` are `alwaysinline`, `noinline`, `cold`; `@c_name("x")` makes `x` the symbol, for a function, a prototype (also one of an imported module) and an `extern` global: how `std::autodiff`'s `__enzyme_autodiff` and `enzyme_out` are declared (#1975, tests: `tests/front_emit_autodiff_test.smith`). Not yet: a reference to a global of an imported module (`autodiff.out`).
- A function named `main` has the symbol `main`. The last line of a body, an expression statement with a non-`Void` result, is returned as `return <expression>`.
- `let _ = f()` evaluates and drops the value; a local `const X = v` is an ordinary slot stored once. Tests: `tests/front_emit_fn_test.smith`.

**Debug info** (`emit_debug.smith`, B5, #1135): with `Options.debug_info`, a DIBuilder, a C11 compile unit (producer `smith`), a `DISubprogram` per function (name as the source writes it, the full path, `geometry.box.area`; linkage name the symbol, `geometry::box::area`; #1979), a `DILocalVariable` per parameter and local, and a `DILocation` on every instruction.
- A name or literal keeps the location of the statement or operator around it; a continuation gets the location of the node that pushed it (`debug::then_location`).
- The line table is `Emitter.line_starts`; there is no file-level state, so two emits can run at once. `debug::release` runs on every exit. Test: `tests/front_emit_debug_test.smith`.

## Objects and the driver

### front::emit_object (`front/emit_object.smith`, #1134)

`emit_object::write(module, ObjectOptions, path)` writes an LLVM module to an object file and gives `path` back, or an `emit::Error`.
- The target is the caller's (`options.target.triple`, `cpu`, `features`; empty means the triple's defaults); nothing reads the host. It registers every target the linked LLVM has and makes a PIC machine.
- `optimization` 0..3 sets the code generator's level; 1..3 first runs LLVM's `default<O1..O3>` pipeline on the module in place (#1581); 0 runs no pass; checks stay.
- A module whose data layout is not the machine's is refused ("emitted for another target"). Test: `tests/front_emit_object_test.smith` links `add.o` with a `main.c` through `smith cc` and runs it.

### front::compile and front::roots (`front/compile.smith`, `front/roots.smith`, #1221 J2)

`smith cc --front-end=new` reads each `.smith` file of the command line, and `parser`, `types_decl::check_module`, `emit::module` and `emit_object::write` make its object.
- The other arguments go to clang as before with each `.smith` file replaced by its object (`-c` leaves only objects).
- `compile::file(path, output, triple, roots)` does one file and gives a `Report` (ok, diagnostics, nanoseconds per stage); `compile::run` (C name `front_compile_run`) is what `runtime/front_flag.c` calls.
- A diagnostic is `file:line:column: error: message`; a failing stage stops the next, so a type error writes no object.
- `smith build frontend-coverage` (#1730, #1922) reads every `error:` line of a failed file, not only the first: the parser reports one error and stops, the checker reports every error it finds, emit one. Its summary counts the failed files per stage and the diagnostics per cause (names and numbers replaced), so how much is left after the first cause of each file is fixed can be read from it.
- The parser's warnings of a file in smith's own syntax print as `file:line:column: warning: message` before any error and do not stop the compile.
- The target is the driver's (`--target=` or LLVM's default triple); the checker's pointer width follows the triple. Casts are checked (`Options.checked`).
- Modules are named by path (`front::roots`; owner, 2026-10-31): `src/game/replay.smith` is `game::replay`, `lib/std/ascii.smith` is `std::ascii`. Roots are `src` and `lib`; roots from `build.smith` are later.
- A `module` line that disagrees with the path is an error naming both; two files with one name are an error naming both; a file under no root takes its `module` line.
- `build.c` compiles `front/*.smith` (all but `emit_jit` and `cimport_cache`) into smith, with `shim/cimport.cpp` and `shim/cimport_deps.cpp` linked beside the other shims, with `ascii`, `format` and `clock` as plain compile steps. The units runner (`SMITH_CORE`) has no front end.
- `smith prove ... -- --front-end=new` (#1974): a `.smith` source goes through `compile::file` to bitcode, not clang. `file` writes the module as emitted when the output is named `*.bc` (`shim/prove.cpp`, `compile_with_new_front_end`, in a child process), and the prover reads it as it reads clang's. A `.c` file is unchanged. Gaps: `lib/std/contract.smith` is in today's syntax (`[[gnu::always_inline]]`) and does not compile under the new front end yet, and `c::smith_requires` from `import <smith/contract.h>` is not found (an inline function of a header), so a contract cannot be written yet; the test (`tests/prove_smith_test.smith`) proves only that a function reaches the prover by its name.
- Gaps: the temporary objects of a link (`<TMPDIR>/<name>.front.o`) are not removed yet; the header cache and `import <header.h>;` are not called by `compile`.
- Tests: `tests/front_join_test.smith` (in process and through `$SMITH_SELF`, goldens `tests/front/join/`) and `tests/front_roots_test.smith`.

## C import (`front/cimport.smith`, `front/cimport_cache.smith`)

Declarations read from C headers become `front::ast` nodes with `from_c` set; the design is [c-header-import](c-header-import.md).
- `cimport::read` parses a header through clang's C++ libraries at the edge file `shim/cimport.cpp`; what it cannot express is listed with a reason in `not_imported`.
- Each declaration the import builds (function, struct, typedef, enum, constant) carries `Decl.place` (file, line and column: clang's presumed location, the header as written after `#line`) and `Decl.doc` (the comment above it with its `//`, `///` and `/** */` markers taken off, empty when there is none); a struct field and a named enum's item carry the same on `FieldDecl` and `Enumerator`. A comment counts only when it ends on the line above the declaration or on its line (clang gives an enumerator the comment of the one before it). A macro constant's comment is read from the lines above its `#define`. A declaration with no location (a builtin) has none, never an invented one. This is the data hover (#1892), go-to-definition (#1893) and completion (#1894) read; a comment after the declaration on the same line is not kept. The cache stores both and its format version went up for them.
- `cimport_cache` caches the result and is not linked into smith (it needs `std::process`, which `smith prove` would then prove); tests: `tests/front_cimport_test.smith`, `tests/front_cimport_cache_test.smith`. `compile` calls `cimport::read` directly for now: the cache's dependency pass (`shim/cimport_deps.cpp`) crashes in the smith binary (a null vtable in clang's `EndOfMainFile` callback), so it is not used there yet.
- `c::` names (#1762): `import <h>;` or `import "h";` makes `compile` read the header (flags from `$SMITH_FRONT_CIMPORT_FLAGS`, which `smith cc --front-end=new` sets to the resource directory and the SDK) and the checker (`types::Options.headers`, `types_decl::check_module_headers`) declares each declaration as `c::name`, a type of the header naming another by `c::` too. A declaration the checker cannot type is left out, so its use says `` `c::x` is not declared by any C header this file imports``. The enumerators of an enumeration are constants `c::red` of the enumeration's type. The emitter calls a `c::` function through the checker's type of it (`emit_call::c_function`; variadic calls pass their extra arguments after C's default promotions), emits a constant by its initializer, makes a record the first time a type names it (fields the emitter has no type for leave it opaque), and a string literal given to `const char *` is the address of its bytes. After `import c <h>` the names are written `c.name` (#1961): `c.f(x)` and `c.CONST` are a member of a bare `c` that no local or module binds, read as `c::name` by `types::c_member` and by the emitter's `c_callee` and `c_constant`; a local named `c` hides it. Not yet: a typedef or enum no declaration uses (cimport reads the types the declarations name), a union, a bit-field, a function pointer, a struct that holds another by value, and the namespace spelling `import <stdio.h> as stdio` (#1678).

## The language server (`front/lsp_server.smith`, #1887)

`smith lsp --smith` is a language server for `.smith` written in `.smith` on this front end (the plan: #1887, with #1377's zero patches). Plain `smith lsp` stays clangd; an editor registers the server per file type, so there is no proxy.
- JSON-RPC 2.0 over stdio with `Content-Length` framing; `lsp_server::handle(body)` is one message in, the framed replies out, which the tests drive with no pipe (`tests/front_lsp_server_test.smith`); `run` (C: `front_lsp_run`) is the loop, one arena reset per message.
- Slice 1 serves `initialize` (full sync), `shutdown`, `exit`, `didOpen`, `didChange` and `didClose`; each open or change answers with `textDocument/publishDiagnostics` from `compile::diagnose(path, source, roots)`: the parser's errors (and, in smith's own syntax, its warnings as severity 2), or the checker's errors when it parsed. Any other request gets -32601.
- Columns are bytes in the line (LSP counts UTF-16 units). Hover (#1892, `front/lsp_hover.smith`): `textDocument/hover` returns the type of the innermost checked expression under the position (`ast::expr_at`, `compile::hover_at`) as markdown with its range; the server keeps the open documents' text by uri (fixed slots) for it. Definition and symbols (#1893, `front/lsp_definition.smith`, `front/lsp_symbols.smith`): `textDocument/definition` finds the declaration of the name under the position in the file, through `q.name` / `q::name` imports (a module, or a C header at the place clang records, #1941) and `from ... import` names (`compile::definition_at`; locals and parameters are not resolved yet), and `textDocument/documentSymbol` lists the file's named declarations flat (`compile::symbols`). The next slice (#1894 completion) add methods to the table in `lsp_server::handle`; #1895 makes plain `smith lsp` clangd without patches. Semantic tokens (#2008, `front/lsp_semantic.smith`): `textDocument/semanticTokens/full` and `/range`. The lexer gives keywords, literals and comments (found in the text between tokens), and `ast::symbols` lists what the file declares (functions, types, parameters, locals, fields, variants, modules) with the bytes where each is in scope; a name takes the narrowest scope that covers it, a name after `.` or `::` is a field, variant or method, and a name that nothing declares is a function when called, a type when written with a capital or built in, else a variable. The legend (standard type names, modifiers `declaration`, `readonly`, `defaultLibrary`) is in `initialize`. A file that fails to parse still gets the lexer's tokens, and a lexer error leaves the tokens before it, so highlighting does not flicker while typing. The tokens come from the parsed tree and not the checked one: a name's type is not needed to colour it, and the checker is not run on every keystroke. tree-sitter stays for what editors do without a server (#1977).

## The formatter (`front/format_space.smith`, `front/format.smith`, #1886)

`smith fmt` on smith's own front end, one style and no clang-format (the plan: #1886, with #1377's zero patches). Wired into `smith fmt` (#1890): a `.smith` file that lexes and lays out as the new syntax goes to `format::format` (C entries `front_format_accepts` and `front_format_run`, `runtime/front_flag.c`); any other file stays clang-format's until #1631. `.clang-format-ignore` is honoured.
- `format_space::space_between(prev, next, context)` is the space inside a line, a pure function of two token kinds (#1888).
- `format::format(source)` lexes with `tokenize_new`, adds the layout tokens and writes the file back: 4 spaces a level from `indent`/`dedent`, one statement a line, the space by `space_between`, `#` and `##` comments kept as written (taken from the gap between two tokens, trailing or on their own line, a comment before a dedent is written at the new level), at most one blank line in a row, none at the start or end, one final newline. A line that goes on inside a bracket or after a binary operator keeps its break and is indented two levels deeper than its statement. Lines over 100 columns are left as written. It is idempotent; an error of the lexer or the layout is its result.
- Tests: `tests/front_format_test.smith` reads the pairs `tests/front/format/<name>.smith` and `.expected` and checks that formatting twice changes nothing over `tests/front/e2e/`.

## Tests

The front end has its own runner (`tests/front_units/build.c`, `front-project`), because its tests link LLVM's C API ([build-tests](build-tests.md)).
- Unit tests: `tests/front_*_test.smith`, one or more per module (`front_lex_test`, `front_parse_*_test`, `front_types_*_test`, `front_emit_*_test`, `front_fix_*_test`).
- Golden files under `tests/front/<area>/` hold dumps (ast, tokens, IR, checker types, parser cases); rewrite them with the runner's `--update` or `FRONT_UPDATE=1` for the corpus.
- The e2e corpus `tests/front/e2e/` is run by `tests/front_e2e_test.smith` through `compile::file`; each case is a module in smith's own syntax.
- To add an e2e case: add `<name>.smith` with `pub fn check() -> i32` returning 0, which is compiled, linked with a `main.c` and run; or add `<name>.error` beside it with the exact diagnostics (scratch folder written `<dir>`).
- The folder is in `.clang-format-ignore` (`smith fmt` would rewrite the new forms). No test file lists cases: the folder is read.
- Run all: `smith build test-front`; one test: `smith test --test <name>` (a glob such as `'front_types_expr_test::*golden*'` works for a group).

### front::types_attr (`front/types_attr.smith`, #1628)
- An attribute may sit on its own line before a declaration, a struct field or an enumerator (`parse_decl`); one followed by a blank line, a comment or the end of the block is the error "an attribute marks the declaration on the next line". `@attribute` marks a struct as an attribute type.
- `types_attr::check_module` checks every `@name` written so: built-ins (`inline noinline cold pure affine c_name attribute effects`) pass, any other unqualified name must be an `@attribute` struct of the module and its arguments are a construction of it, known at compile time, and the arguments are matched as a construction matches them, positional first and then named (`types_expr::argument_field`, #1813): `@rename("x")` is `@rename(to = "x")`. `@x()` is a parser warning whose fix drops the `()` (`warn_empty_call`). `@mod::name` is `not yet typed` (one module is checked at a time). Tests: `tests/front_attr_test.smith`.

### front::effects_table (`front/effects_table.smith`, #1965, piece 1 of #1939)

- The 13 effect bits and the libc leaf table of `shim/effects_table.h` as data: `describe(set)`, `leaf(name, &set)` (bisection over a sorted table; false for a name the table lacks, which the checker reads as `unknown`). The header stays the source until the plugin is deleted (#1632); `tests/front_effects_table_test.smith` reads it as text and fails when a name or its bits differ. Inference is the IR pass below; ceilings come in #2019.
- `@when(cond)` (#1801, `front/when.smith`) on a top-level declaration or a struct field: `cond` is `target.os == .macos` / `.linux` (or `!=`), `true`, `false`, `!`, `&&`, `||`. `types_decl::check_module_headers` (and `check_module_with`) checks a module that has one twice, once with the declarations live on macOS and once on Linux whatever system builds; an error found on one only says `on macos:` / `on linux:`, and a use of a name that is gone there says `` `f` is not declared on linux: it is declared only when <cond> ``. `@when` on an import, an enumerator, a parameter or anything but a declaration is an error pointing at `if constexpr`; a statement takes no attribute, so a local is a parse error. The generated C header writes each condition as `#if` (`defined(__APPLE__)`, `defined(__linux__)`; a field's `#if` is inside the struct). Tests: `tests/front_when_test.smith`, `tests/front_header_test.smith`, `tests/front_fix_test.smith`.

### front::effects_pass and front::effects_cache (`front/effects_pass.smith`, `front/effects_cache.smith`, #2018, piece 1 of #1939)

- Effect inference is one pass over the unoptimized LLVM IR of the whole build (#1939), the same for smith, C, C++ and Objective-C; `knowledge/effect-inference.md` has the design. `effects_pass::make()` gives a `Graph`; `add_module(&g, ir, &cache)` adds the functions a module defines (internal linkage is named `<module>:<name>`); `solve(&g)` resolves each called name that nothing defines to a leaf (`front::effects_table::leaf`; an `llvm.*` intrinsic is pure but `llvm.trap` and `llvm.debugtrap` are `fails`; any other is `unknown`) and takes the union of the calls' sets to a fixed point. A function adds `globals` itself by a store, `atomicrmw` or `cmpxchg` through a global variable (a function-local static is one in IR), and `unknown` by inline assembly or a call through a pointer (pointers are #2020). `set_of(&g, name)` is the set; `cause_of(&g, index, bit)` is the call and source line that first added a bit (the chain of #2019); `find` gives the index.
- `effects_cache::Cache` keeps each function's summary (its own bits and the direct calls, by name and line) by a hash of the IR the pass reads (opcode and line of every instruction, callee and global names), so a function with the same hash is not scanned again. Printing the function to hash it cost most of the pass, so it is not done. Measured in the test build (-O0, ASan) on the IR of `front/emit.smith`, `front/types_expr.smith` and `front/compile.smith` (176 to 382 functions, 1.4 to 3.5 MB of IR text): adding a module 25 to 72 ms, solving 8 to 30 ms. Wiring it into `smith lsp` follows with the ceiling errors (#2019), which is what the editor shows.
- Tests: `tests/front_effects_pass_test.smith` (the sets of the chain functions from IR text, the chain of causes with its line, the cache).

### Return types and function names (`front/parse_type.smith`, `front/parse_decl.smith`, #1959)
- A function's return type may be any type the type grammar reads, including a call of a type function (`Option(T)`) and one through a module in smith's own syntax (`-> arena.slice_of(T, countof(dims))`: `parse_type::qualified_type_function` joins `.name(` to the path and reads the arguments as an applied type). A `.` after a type that is not followed by `name(` still ends the type.
- `type` is a keyword where a type is expected, and a function may be named `type` (`pub fn type(p: *parser) -> *node:`). Tests: `tests/front_parse_type_call_test.smith`.
