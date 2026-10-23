---
type: design
title: Generic functions are lowered, not templates
description: constexpr type parameters and auto parameters make one weak hidden instance per list of types, chosen and written by the lowering before the parser.
tags: [generics, lowering]
---
# Generic functions are lowered, not templates

**Generic functions are lowered, not templates** ([the language](smith-language.md), issue #15; skill smith-lang-generics).
Every compile-time parameter is marked `constexpr`, a type parameter included: `export T *push(Arena *a, constexpr type T)`,
called as `arena::push(&a, Point)`; a call passes every parameter the function declares, so a type no argument carries
stays a parameter (`math::max(u32)`) and leaving it out is an error (issue #61). An `auto` parameter takes the type of
its argument (`export void check_equal(auto a, typeof(a) b)`), `typeof(a)` names it in later parameters, the body and
the return type (`export typeof(x) sqrt(auto x)`), a later argument converts to it by C's rules but a conversion that
changes the value is an error at the call (judged by value for a literal or a constant of the file, else by type; no
warning flag involved), and a module the bootstrap lowers calls no `auto` function (no compiler to type the argument).
The lowering records the
generic's tokens (no clang patch), writes one weak, hidden instance per canonical list of types at the end of the file,
and chooses the branch of each `if constexpr` for it, so the parser only ever sees ordinary functions; an error in an
instance is reported at the generic's source with a note at the call. An instance's code is a copy of its generic's source
file under the same name, so the debugger shows `pick::larger<i32>` at the generic's own lines. Value parameters and
functions that return `type` are the next extension.

A function of a module whose parameter list has `constexpr type T` or `auto x` parameters is written once and compiled for each
type it is called with (issue #15). `constexpr` in a parameter list means compile-time: the parameter is in no
generated function. `type` is a kind, as `u32` is, so `constexpr type T` is a compile-time constant that is a type. A
bare `type T` is an error that says to write `constexpr type T`.

```c
module arena;

export T *push(Arena *a, constexpr type T) {        // the types may be used anywhere in the signature and body
    usize aligned = (a->used + alignof(T) - 1) & ~(alignof(T) - 1);
    ...
}

Point *p = arena::push(&a, Point);                  // the type is the argument in T's place
```

- **What a generic is.** A function with a body at file scope in a module, exported or not, `inline` or not (not
  `static` or `extern`: `export` is what makes it public). Up to 4 `constexpr type` parameters and 32 parameters (a formatter takes its pieces as `auto... args`). It has
  no symbol and cannot be named without being called (`&arena::push` is an error); C and C++ see a placeholder
  (`[[clang::unavailable]] void arena_push(void);`), and the module's header carries the record of the generic in a
  `// smith-generic:` comment line, which an importer reads (so a header on the include path, as `smith build` writes,
  or the module's source read in memory, both work).
- **A call** passes every parameter the function declares: a `constexpr type` parameter takes the type where it stands
  (`arena::push(&a, Point)`, `list::make_two(arena, i32)`), and leaving it out is the error ``"`push` is called without
  its type `T`; pass the type: arena::push(&a, T)"``. A call is read whole, so it may be an argument of another call,
  16 deep at most. It becomes `__extension__ ({ extern R f(P) __asm__("label"); f(args); })` with the instance's name
  for `f`.
- **`auto` parameters.** `export void check_equal(auto a, typeof(a) b)` is a generic with no `constexpr type`: the type
  of `a` is the type of its argument, and each distinct canonical type (or list of them, for several `auto`
  parameters, up to 4 with the `constexpr type`s) is one instance. The compiler types the argument: the lowering gives
  its tokens to the parser itself, as `__typeof__(<tokens>)` read through Sema's `ParseTypeFromString` (the parser's
  own entry for a type written in a string) in a child scope of the one the parser is in, so any expression works
  (a cast of an index, `((u8 *)block)[n - 1]`, a member of an anonymous union, an operator with a side effect, a
  statement expression, a call to another generic), a local declared by an earlier statement counts, and nothing is
  evaluated or marked used. Arrays are pointers and qualifiers are dropped, as `auto` drops them. Diagnostics are off
  while it reads; an argument the compiler does not accept is an error that carries the compiler's own message and
  says to give it a type with a cast or a variable. The same read says whether the compiler folds the argument to a
  number, and which: a later argument is judged by that value and by its own type, never by the lowering's guess
  (all ones in a `u32` constant is a `u32`'s). Inside a generic's body, an argument that is a parameter of that generic
  (`value_of(a)`, `check_compare(op, value_of(b))`) has the instance's type, so generics
  call generics with their parameters.
- **`typeof(p)`** names the type of an earlier `auto` parameter `p` (or of the instance's own `T`) in later parameters,
  in the body and in the return type, which is written before the name as in C: `export typeof(x) sqrt(auto x);`. It
  is one token of the record, replaced by the instance's type, so `if constexpr (typeof(x) == f32)` and
  `sizeof(typeof(x))` work as they do for `T`. `typeof` of anything else is C's.
- **Function parameters** (issue #69). `constexpr auto f` takes a function known at compile time: the argument is a
  function designator (a function's name, `module::name`, `c::name`), and each distinct function is its own instance, so
  in the instance `f` is that function's name: a call through it is direct, `(void *)f` and `&f` are its address. The
  parameter is in no generated function, like `constexpr type T`, and counts toward the 4 compile-time parameters. The
  compiler says what the argument is (the host's `SmithExprType` says whether the expression is a function before it
  decays to a pointer): a function pointer variable, `&f`, a number or an expression is an error at the call that names
  the parameter (``the argument `pointer` of `twice` is not a function: the parameter `f` is `constexpr auto`, ...``).
  Whether `f` takes the other arguments is the body's call, checked by clang at the instance
  (`(void)sizeof(f(x, rest...))` writes it explicitly). An instance is identified by the function's spelling (`inc` and
  `main_inc` are the same function written two ways, and so two instances) and named `twice<apply::triple, int>` in the
  debugger. Its symbol carries the file too, because a `static` function of two files may share a name.
- **Argument lists** (issue #70). `auto... rest` is the last parameter and takes the arguments that remain, each of its
  own type; each distinct list of types (with the other slots') is one instance. In the instance `rest` is the tuple
  of the arguments, written as the C that it lowers to: the parameters `rest_0, rest_1, ...` (marked
  `[[maybe_unused]]`, since they may only be counted), `rest...` is `rest_0, rest_1` wherever a call's arguments go (an
  empty `rest` takes a comma with it), `rest[i]` is `rest_i` with `i` a compile-time constant (the evaluator of
  `if constexpr`: a number, `countof(rest)`, a `constexpr` of the file; a runtime index or one out of range is an
  error naming `rest`), and `countof(rest)` is its size, a number the compiler and `if constexpr` conditions both read
  (C2y's `_Countof`). `rest` used any other way (`g(rest)`) is an error that says to spread it with `rest...`: passing
  the tuple whole or returning it would need a struct of the pack in the instance's signature, which waits for a
  concrete use (issue #101). `rest...` in the arguments of a
  call to another generic is expanded before that call is read, so `forward(f, x, rest...)` types each element by its
  own type; a call of the generic needs at least the parameters before `rest`. No value-changing conversion check
  applies (no `typeof` target). The debugger names the instance with every type, `gradient<loss, Params *, Batch *>`.
- **`typeof` of an expression in the result type.** The result type is written before the parameters, where C does not
  know them, so an `auto` parameter's name inside a `typeof(...)` there stands for a value of its type, `(*(T *)0)`
  (never evaluated): `export typeof(*x) gradient(constexpr auto f, auto x, auto... rest)` returns what `*x` is. In the
  parameters and the body `typeof(*x)` is plain C.
- **Conversions.** A later argument whose parameter is `typeof(a) b` converts to a's type by C's rules, with one
  addition: a conversion that changes the value is an error at the call, whatever warnings the project has on. A
  literal and an expression of literals, `constexpr` integers (the file's or a C header's, an unsigned one as the
  unsigned number it is) and enumerators (`1 << 4`, `button_a | button_b`, `c::Style_Count - 1`) are
  judged by their value (`check_equal(my_u8, 300)`, `check_near(my_f32, 0.1)`, `-1` for an unsigned type, `16777217`
  for an `f32` are errors; `0.5` and `0.1f` are fine); any other argument is judged by its type, which must have a
  range inside the target's (`u8` to `u32`, `i32` to `f64` and `u16` to `f32` are fine; `u32` to `u8`, `i32` to `u32`,
  `i32` to `f32` and `f64` to `f32` are not). Bools, enums and pointers convert by C's rules, and an argument whose type
  the compiler cannot say is not judged. The message names both fixes: ``the argument `big` of `check_equal` is u32 and
  converts to u8, the type of its first argument `small`, which may change its value; cast it, `(u8)big`, or the
  first argument, `(u32)small` ``.
- **Without a compiler.** The header pass writes declarations only and skips a call in a body whose type it cannot
  tell (a module's test can call `test::check_equal(a, 1)`); the raw lowering (`--smith-lower`, the bootstrap, the
  stage0) has no compiler to type an `auto` argument, so a bootstrapped module calls no `auto` function (it calls
  the builtins, or a function with `constexpr type` parameters and the types written).
- **An instance** is one function for each distinct list of types, compared by the parser's canonical spelling (`u32`
  and `uint32_t` are one instance). It is written at the end of the file as a weak, hidden definition with a symbol of
  its own, `_pick::larger(int)`, so the linker folds the copies other files made; an `inline` generic is
  `always_inline` instead. Its C name is `pick_larger__i32`. A generic can call generics (`list::make(a, T)` in a
  body: `T` is the instance's type), 32 deep and 4096 instances at most.
- **`if constexpr`** in a generic's body is chosen for each instance; the branch not taken is never given to the parser,
  so it may name what `T` has not (`value.x`). The condition is a constant expression of integers (`true`, `false`,
  the operators of C), `sizeof(T)`, `alignof(T)`, `T == U` and `T != U` (canonical spellings; `T` may be `typeof(x)` of an `auto` parameter, and a `constexpr` integer of the file is a number), and the questions
  `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum` and `is_record`. A type is written as C
  writes one: `T`, a sized type, a typedef, `struct S`, qualifiers, `*`. Anywhere else a generic's constraint is a
  `static_assert` in a final `else`. `if constexpr` outside a generic is "The target, and `if constexpr` outside generics".
- **Errors** point at the generic's source, with a note where the instance was asked for, chained when one generic
  asked for another: `num.smith:5:14: error: invalid operands to binary expression ('Point' and 'Point')`, then
  `main.smith:13:20: note: in instance num::add(Point), requested from here`. An error in a generic's head, a call that
  cannot be written (wrong number of arguments, a type that cannot be inferred, a statement that ends inside the
  parentheses, the end of the file, too deep) is one error: the generic or the call is dropped for a name whose error is
  not shown, so nothing follows it.
- **The editor and the debugger.** Hover on the name in a call shows the generic as its module wrote it and the
  instance (`generic function pick::larger`, the head, `instance pick::larger(i32)`), and go-to-definition goes to the
  generic's name. An instance is `pick::larger<i32>` in `smith dbg` and `smith dap`, and its code is the generic's own
  file, line and column, so a breakpoint, a listing and the arguments are those of the source.
- **Reflection: `fields(T)`** (issue #83, skill smith-lang-reflection). A struct's fields are one more compile-time
  list, read from the parser, never written by hand. `runtime/lang_fields.c` (included by `lang_constexpr.c`) is the
  whole of it. When an instance is written, the replay (and `smith_lang_pack_expand_range`, for the arguments of a call)
  asks `smith_lang_fields_use` at each token: `countof(fields(T))` becomes the number, `fields(T)[i].name` a string
  literal, `.[fields(T)[i]]` and `->[...]` the member name after the operator, `T.[...]` a member of `((T *)0)`; the
  index is read by the evaluator of `if constexpr`, so it may be the loop variable. The condition's `typeof(x->[F])` is
  the field's type spelling, asked of the host (`SmithHost.type_field`: status, count, the `index`th named field with its
  type and whether it is a bit-field; `hook.cpp` `typeField`), and `is_array(...)` is one more type question. T is a
  type parameter (`fields(T)`) or the type of a parameter (`fields(typeof(x))`, through `smith_lang_param_type_of`).
  A union, an anonymous member, a type that is no struct and an index out of range are errors; unnamed bit-fields
  (padding) are not fields. `typeof` of a bit-field is an error in C, so it is written as the field's declared integer
  type, and the host types an argument that is a bit-field as the integer it promotes to. An argument of a call to a
  generic that holds `fields` or an expression of an instance's parameter (`*p`, `a.m`) is typed like an argument that
  holds a call: the parameters are written as `(*(T *)0)` for the compiler to type (a local of the body, a runtime loop
  variable, cannot be named there). The grammar (`tree-sitter/grammar.js`) reads `x.[e]` and `x->[e]`; clang-format
  needs nothing. `[..]` fields are #82's.
- **`for constexpr`** (issue #71, skill smith-lang-rest). Smith is C plus compile time. The lowering keeps, for the
  instance being written, the loop variables of the `for constexpr` it is inside, and writes plain C.
  - *Built lists are gone* (issue #101). `auto name = {a, b};` and `name = {name..., x};` made a list of variables
    `name_<binding>_<index>` whose shape the lowering tracked (about 1000 lines of the replay and of the evaluator).
    Tuples (skill smith-lang-tuples) hold values of different types, so the owner dropped them: in a generic's body the
    two statements are an error that says built lists are gone and points to a struct or a tuple type written out
    (`smith_lang_list_gone` in `runtime/lang_generic.c`). Only the argument list `auto... rest` is left of the lists. A
    tuple type written in a generic, and `t[0]` or `countof(t)` on a tuple inside a generic's body, are not built (they
    keep their clear errors) until a use needs them.
  - *`for constexpr (init; condition; step) body`* is the token `for constexpr` in the record (`Y` in the header's
    `// smith-generic:` line) and, when the instance is written, a frame on the replay stack: the loop variable is a
    number (every use of it is written as the number, `rest[i]` is the element), the condition and step are read by the
    evaluator of `if constexpr` (numbers, `countof`, the variable, the operators of C; step `i++`, `i--`, `i += n`,
    `i -= n`, `i = n`), and each turn is the body pushed above the loop frame, so the loop is not a recursion of C. A turn
    count of 1024 is the limit. Outside a generic `for constexpr` is an error. There is no range-based `for`: C's `for`
    with `countof` is what loops over sequences, and a sequence of the language (a tuple, an array, later a slice) would be
    the only thing a range form worked on. A body that is one statement is written in braces of its own, so its locals do
    not clash from turn to turn.
  - *Types.* `typeof(rest[i])` in an `if constexpr` condition is the instance's own type for that argument, so
    `is_integer(typeof(rest[i]))` and `typeof(rest[i]) == char *` choose per argument (a type-checked formatter is a
    `for constexpr` over `auto... args` with a `static_assert(false)` in the final `else`).
  - *Names.* A variable of the body spelled like an argument (`rest_0`) is an error, found where the body is
    read. The debugger shows the arguments as variables of that name at the generic's own lines.
  - *The editor's grammar* reads `for constexpr (...)` and `{rest..., x}`;
    clang-format reads `for constexpr` (patch smith-lang-for-constexpr-format).
- **Private names in an exported generic's body** (issue #75). The body may name the module's private functions, variables, types, constants and generics: the header's instance section declares what the instance needs, and the module's compile gives the functions and variables a symbol (design: "The instance section" of [smith-module-headers](smith-module-headers.md)). The lowering keeps the names each generic's body writes, chooses at the end of the file what the exported generics reach (through private generics too) and writes only that. The importer still cannot write them.
- **A call as the argument of a call, in an instance's body** (`twice(f, twice(f, x))`). The arguments of such a call are written to the pool first, with the instance's parameters as values (`(*(int *)0)`), so that the compiler types them where the instance is not declared yet; then the call is written from the arguments as they were. Frames of the replay do it in turn (`TypedStart`, `TypedFinish`), so it is no recursion of C.
- **A module's own exports** may be written qualified in its generics and elsewhere in the module (`tools::dup` in
  `smith::tools`): the module's last name is a binding of itself that an import of that name replaces.
- **Not done:** generic types and functions that return `type`, compile-time value parameters (`constexpr u32 tile`),
  `auto *p` and `const auto x` (an `auto` parameter is `auto name`),
  overloading, specialization, constraints other than `if constexpr` and `static_assert`,
  pointers to a generic, and a type local to a function as `T`.

## Types are values

**Types are values** (issue #99, ticket 4 of #95; `runtime/lang_type.c`, skill smith-lang-type-values). `type` is a compile-time type: `constexpr type t = i32;` is a value, a struct may hold one (`constexpr type of;`, valid because `constexpr` on a member is invalid C, so the C rule allows it), an array may, and an ordinary function may take and return one (`bool small(type t)`), which then runs only in the compiler. The engine is clang's constant evaluator on plain C, so a type is a **handle**, a 64-bit number: the index into the file's table of canonical spellings (`smith_lang_ty_table`, 4096 types, an error beyond it), the kinds the type is asked (integer, float, signed, pointer, bool, enum, record, array), log2 of its alignment, an incomplete flag and its size (`bits 0-15, 16-23, 24-28, 29, 32-63`). Two handles are equal exactly when the compiler's canonical spellings are (`u32 == uint32_t`), and `sizeof(t)`, `alignof(t)` and `is_integer(t)` are shifts of the handle written in the C clang reads, so they are exact for the target (the handle was made from `type_query`) and need no callback from the evaluator and no table declared before the function that reads it. The handle is wider than the 32 bits the ticket's first sketch had because a table of sizes would have to exist before the first function that uses it, and the types a file uses are met in order. In the text clang sees, a `type` is `unsigned long long` and a type written where a value is expected is the handle as `123ULL`.

`smith_lang_ty_token` is called from `smith_lang_process` for each token of a file whose host set `maybe_type_value` (`hasTypeValue`: `type` before a name or `*`, outside a generic's `constexpr type T` and outside comments). It follows the file's shape (parentheses, braces, a declaration's specifiers) and recognises four **regions** where a type name is a value: the initializer of a declaration whose type is or holds a `type`, the arguments of a call to a function that takes a type, the `return` of a function that returns one, and either side of `==` and `!=`. Inside `sizeof(` and the like a type is a type, and `(i32)x` is a cast. A type value is turned back into the type it names where a type is written: `t x;` at the start of a declaration, `(t)y`, and an argument of a generic call (`pool::width(t)`), by asking the compiler for the variable's value (`value_query`, a chain of names, `.field` and `[n]`) and reading the index. A generic's `if constexpr (small(T))` writes `T` as its handle when it is a whole argument of a call. A variable of a `type` or of a struct that holds one, declared without `constexpr`, is an error that names the field. The fast paths of the hook and of the module's declarations do not run in such a file (every token is offered, and a sized name is read with its neighbours).

Not built, by design of the stages: a type value inside a generic's body or as its parameter, functions that return a type where a type is written (ticket 7, `List(f32)`), `fields(T)` as an array of `{name, type}` (ticket 5), `typeof(x)` as a leaf. A `constexpr type` initialized from an array element or a member is known to the compiler only after the next statement has begun (the parser has not attached the initializer yet when the lowering reads the next token), so the first use is one statement later.

