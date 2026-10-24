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
warning flag involved), and a module that goes through the raw lowering (the header pass) calls no `auto` function (no compiler to type the argument).
The lowering records the
generic's tokens (no clang patch), writes one weak, hidden instance per canonical list of types at the end of the file,
and chooses the branch of each `if constexpr` for it, so the parser only ever sees ordinary functions; an error in an
instance is reported at the generic's source with a note at the call. An instance's code is a copy of its generic's source
file under the same name, so the debugger shows `pick::larger<i32>` at the generic's own lines. Value parameters and
a result type computed by an ordinary function over types and numbers (issue #102) is "A computed result type" below.

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
  `static` or `extern`: `export` is what makes it public). Up to 32 parameters, any of them `constexpr type`, `constexpr auto` or `auto` (a formatter takes its pieces as `auto... args`). It has
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
  parameters, up to 32 with the `constexpr type`s) is one instance. The compiler types the argument: the lowering gives
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
  parameter is in no generated function, like `constexpr type T`, and counts toward the 32 parameters. The
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
  tell (a module's test can call `test::check_equal(a, 1)`); the raw lowering (`--smith-lower`, the header pass)
  has no compiler to type an `auto` argument, so a module that goes through it calls no `auto` function (it calls
  the builtins, or a function with `constexpr type` parameters and the types written).
- **An instance** is one function for each distinct list of types, compared by the parser's canonical spelling (`u32`
  and `uint32_t` are one instance). It is written at the end of the file as a weak, hidden definition with a symbol of
  its own, `_pick::larger(int)`, so the linker folds the copies other files made; an `inline` generic is
  `always_inline` instead. Its C name is `pick_larger__i32`. A generic can call generics (`list::make(a, T)` in a
  body: `T` is the instance's type), 32 deep and 4096 instances at most.
- **`if constexpr`** in a generic's body is chosen for each instance; the branch not taken is never given to the parser,
  so it may name what `T` has not (`value.x`). The condition is a constant expression of integers (`true`, `false`,
  the operators of C), `sizeof(T)`, `alignof(T)`, `T == U` and `T != U` (canonical spellings; `T` may be `typeof(x)` of an `auto` parameter, and a `constexpr` integer of the file is a number), and the questions
  `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum`, `is_record`, `is_array` (a fixed C array) and `is_slice` (a slice struct of runtime/lang_slice.c, any dimensions). A type is written as C
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
  type parameter (`fields(T)`) or the type of a parameter (`fields(typeof(x))`, through `smith_lang_param_type_of`; any other expression of the parameters, `typeof(*p)`, is staged by `smith_lang_fields_stage_expression`).
  A union, an anonymous member, a type that is no struct and an index out of range are errors; unnamed bit-fields
  (padding) are not fields. `typeof` of a bit-field is an error in C, so it is written as the field's declared integer
  type, and the host types an argument that is a bit-field as the integer it promotes to. An argument of a call to a
  generic that holds `fields` or an expression of an instance's parameter (`*p`, `a.m`) is typed like an argument that
  holds a call: the parameters are written as `(*(T *)0)` for the compiler to type (a local of the body, a runtime loop
  variable, cannot be named there). The grammar (`tree-sitter/grammar.js`) reads `x.[e]` and `x->[e]`; clang-format
  needs nothing. `[:]` fields are #82's.
  **`fields(T)` is an array of `Field`** (issue #100, ticket 5 of #95). `Field` is `struct smith_field { char name[:];
  unsigned long long type; }` (a type value is a handle, `runtime/lang_type.c`), `typedef`ed `Field` and defined with the
  `static inline Field smith_field_make(char *name, size_t length, unsigned long long kind)` that builds one, raw (the
  tokens `smith_lang_field_words*` in `runtime/lang_fields.c`) once per file: after the last `import` line of a module
  whose text has `fields(` or `Field` (`host->maybe_field`, which also makes `maybe_type_value` true so that every token
  is offered) or that imports a header with a generic that reflects (`=fields p@` in its lines, `lang->field_import`),
  else where the first instance that needs it is written. `fields(T)[i]` that is no `.name` and no `.[F]` becomes
  `smith_field_make("m", 1, handle)`, a call the compiler evaluates (the r6 evaluator runs C functions and a
  `constexpr` pointer may point into a literal); `fields(T)` alone becomes `smith_fields_<instance>_<k>`, a
  `static constexpr Field[N]` declared before the instance. The conditions the compiler decides while an instance is
  written (`if constexpr`, `.[F]` of anything but `fields(T)[i]`) cannot name a local of the instance, whose body the
  parser has not read: `smith_lang_fields_alias` remembers `constexpr Field f = E;` (or `constexpr auto`) with `E`
  already written out as an expression (the `for constexpr` variables as numbers, `fields(T)[i]` as the call of the
  maker), and `smith_lang_cx_text` writes the name as that expression in the text it asks `value_query` about. `.[E]`
  asks `value_query` for `(E).name.data` (the host reads a pointer to a whole string literal as its characters, the one
  pointer `value_query` accepts) and for `(E).type`, a handle `smith_lang_ty_spelling_of` turns back into the field's
  type; a tuple's members `_0` are named "0". The type of `f.type` in `is_float(f.type)` is asked the same way
  (`smith_lang_fields_valued_type`). The definition goes out through `gen_put` with the generics dump off (a test reads
  the instances only); it cannot be queued as source, because a function body in the queue is read as the lowering's
  sparse body and loses the `}` that ends it. Limits: the functions a condition calls must be defined in the file that
  instantiates the generic or be exported functions of an import ([body-files](body-files.md), #107) (the instance is compiled there), `typeof(x->[f])` of a bit-field kept in a variable is C's
  error, a file that defines its own `Field` and also uses `fields(` is a redefinition, `countof` of a member is lowered in an
  instance (below).
- **What an instance's body may ask about names it declares** (issue #123, `runtime/lang_generic.c`, `lang_fields.c`). The compiler has read nothing of an instance when its body is written, so every question about a name of the body is answered from the generic's own tokens. (1) A generic has up to 32 parameters (`smith_lang_type_params_max`, as many as `smith_lang_call_params_max`). (2) A local or a parameter written as a type and a name (`i32 total = ...;`, `typeof(proto) *p`) is an argument of a generic call: `smith_lang_decl_type` finds its declaration (the nearest one before the use, the parameters of the head after it; a type with `auto`, `static`, `fields(` or a bracket is not read, so that case keeps its old error) and the call's arguments are typed with the name written as a value of the type, `(*(i32 *)0)`, as an `auto` parameter's name always was (`smith_lang_typing_declared`). A call routes its arguments through the typed path (`smith_lang_expression_of_param`) when one holds such a name in an expression, or alone when the callee has an `auto` or `auto...` parameter. `rest[k]` inside an expression is typed by `smith_lang_typing_entry` the same way. A `typeof(*x)` that is a type argument stages its `x` as a value, `(*(T *)0)`. (3) `typeof(x.[f][0])` in a condition is the element of the field's type (`smith_lang_fields_typeof_parts`, `smith_lang_fields_element`: a pointer or array loses a level, a slice's struct gives what its `data` points to). (4) `countof(E)` with a member access in E (`x.[f]`, `p->xs`, `p->rows[1]`) in an instance's body is typed like a call's argument and then written (`smith_lang_countof_begin`, a frame like the call's: a number for an array, `((E).count[0])` for a slice). Corpus: `generic-many-parameters`, `generic-argument-names`, `generic-field-element`, `generic-countof-member`. Limits: a local whose type is `auto` is not read. (Issue #124: a slice of pointers keeps its star, because `smith_lang_ty_stem_spelling` wrote none for a stem ending in `_ptr`; and `fields(typeof(*p))`, `fields(typeof(p->f))` and `typeof(*p)` in a condition stage the expression with each parameter it names as `(*(T *)0)`, `smith_lang_fields_stage_expression`. Corpus: `generic-fields-star`, `generic-fields-pointee`.) (Issue #125: `x.[f][i]`, `x->[f][i][j]`, `x.[f][a:b]` as values: `smith_lang_slice_replay_member` (runtime/lang_slice.c, tried first by `smith_lang_slice_replay`) reads the object `ident(.ident|->ident)*`, resolves the field with `smith_lang_fields_group`, and for a slice member (`struct smith_array_...` in the field's type) writes the checked element or cut as a slice parameter's; an array or pointer member stays C's own subscript. A slice of more dimensions needs every index, a partial row is an error. Corpus: `generic-fields-index`, `generic-fields-index-error`.)
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
- **Not done:** generic types and structs built by a function that returns `type`, compile-time value parameters (`constexpr u32 tile`),
  `auto *p` and `const auto x` (an `auto` parameter is `auto name`),
  overloading, specialization, constraints other than `if constexpr` and `static_assert`,
  pointers to a generic, and a type local to a function as `T`.

## Types are values

**Types are values** (issue #99, ticket 4 of #95; `runtime/lang_type.c`, skill smith-lang-type-values). `type` is a compile-time type: `constexpr type t = i32;` is a value, a struct may hold one (`constexpr type of;`, valid because `constexpr` on a member is invalid C, so the C rule allows it), an array may, and an ordinary function may take and return one (`bool small(type t)`), which then runs only in the compiler. The engine is clang's constant evaluator on plain C, so a type is a **handle**, a 64-bit number: the index into the file's table of canonical spellings (`smith_lang_ty_table`, 4096 types, an error beyond it), a slice depth, the kinds the type is asked (integer, float, signed, pointer, bool, enum, record, array), log2 of its alignment, an incomplete flag and its size (`bits 0-11, 12-15, 16-23, 24-28, 29, 32-63`). Two handles are equal exactly when the compiler's canonical spellings are (`u32 == uint32_t`), and `sizeof(t)`, `alignof(t)` and `is_integer(t)` are shifts of the handle written in the C clang reads, so they are exact for the target (the handle was made from `type_query`) and need no callback from the evaluator and no table declared before the function that reads it. The handle is wider than the 32 bits the ticket's first sketch had because a table of sizes would have to exist before the first function that uses it, and the types a file uses are met in order. In the text clang sees, a `type` is `unsigned long long` and a type written where a value is expected is the handle as `123ULL`. `type` is not reserved (issue #114, the C rule): the keyword only where a declaration begins and C means nothing else; a variable, parameter or member named `type` of another type (`int type;`, `c::Kind type`) is C's identifier, remembered for its scope (`SmithLangTy_Plain`), and while one is in scope `type * n` is the product, not a pointer declaration.

`smith_lang_ty_token` is called from `smith_lang_process` for each token of a file whose host set `maybe_type_value` (`hasTypeValue`: `type` before a name or `*`, outside a generic's `constexpr type T` and outside comments). It follows the file's shape (parentheses, braces, a declaration's specifiers) and recognises four **regions** where a type name is a value: the initializer of a declaration whose type is or holds a `type`, the arguments of a call to a function that takes a type, the `return` of a function that returns one, and either side of `==` and `!=`. Inside `sizeof(` and the like a type is a type, and `(i32)x` is a cast. A type value is turned back into the type it names where a type is written: `t x;` at the start of a declaration, `(t)y`, and an argument of a generic call (`pool::width(t)`), by asking the compiler for the variable's value (`value_query`, a chain of names, `.field` and `[n]`) and reading the index. A generic's `if constexpr (small(T))` writes `T` as its handle when it is a whole argument of a call. A variable of a `type` or of a struct that holds one, declared without `constexpr`, is an error that names the field. The fast paths of the hook and of the module's declarations do not run in such a file (every token is offered, and a sized name is read with its neighbours).

**A slice type from a type value** (issue #107): `t[:]` is the slice of `t`, written as an expression over the handle (`smith_lang_ty_slice_op`: the depth grows by the number of `[:]`, and the kinds, alignment and size are those of the struct `{ T *data; usize count[depth]; }`, exact for the target, `smith_lang_ty_slice_make` is the same arithmetic), so the evaluator needs no callback and an ordinary function builds the type (`for (...) t = t[:];`). A slice is not in the table: its handle is its element's index with the depth, so `slice_of(f32, 1)` equals, bit for bit, `typeof(xs)` of `f32 xs[:]` (the handle of a type whose canonical spelling is a `smith_array_` tag is read back from the tag, `smith_lang_ty_from_tag`) and `f32[:][2]` is `f32[2]` at depth 1. Where a handle is written as a type (`t x;`, a computed result type) a slice becomes its struct, defined or bare as `T xs[:]` does (`smith_lang_ty_type_tokens`). `f32[2]` and `typeof(x)` are type values; a local `type` needs no `constexpr` in a function that runs in the compiler (`SmithLangTyFrame.compile_time`).

**Exported functions over `type`** (issue #107) are records of their module's header: the header pass records a function with a bare `type` result or parameter like a generic (`SmithLangGeneric.plain`, `// smith-generic: ... f` for an exported one, `g` for a private one an exported one calls; the `// smith-types:` line names them), and writes no declaration for C; the module's own compile drops the `export` and writes it as a private function (`[[maybe_unused]]`). An importer that reads the line (`smith_lang_import_tuples`) tells the host (`type_values`: every token is offered from then on), and after its last `import` line (the host counts them, `import_lines`; no token after the line has been read) queues each record's tokens marked `replay` (`smith_lang_types_define`): `smith_lang_process` lowers only their type values and writes them, `[[maybe_unused]] static` in front, with their places in a buffer of their own (`types_place`, so no check of the file's code reads them). A call through `m::f` in a generic's body or condition is written by its C name (`smith_lang_callee_name`). Limits: a function is at most `smith_lang_queue_max` tokens, its body has no `if constexpr`, `for constexpr` or call of a generic, the host must have found the import's header, and an ordinary function over numbers is not carried (a later step of #107's decision). A private function over `type` is carried when an exported generic calls it in its body or names it in a computed result type (`smith_lang_generic_head_uses`, issue #143: the head is written with the C name, so the call is found by `SmithLangGeneric.lowered`). A `constexpr type` initialized from an array element or a member is known to the compiler only after the next statement has begun (the parser has not attached the initializer yet when the lowering reads the next token), so the first use is one statement later.

## A slice passed to a generic (issue #119)

A slice's struct is defined where the slice is written, in a function's block, so an instance (written at the end of the file, at file scope) could not name it ("will not be visible outside of this function"). The instance writes the definition itself, and so does the call, in the block before its `extern` declaration: `struct smith_array_f32_n { float *data; size_t count[1]; };`, read from the compiler's own struct when it can (`SmithLangInstance.slices`, `smith_lang_instance_slices`) and else from the tag (`smith_lang_slice_tag_definition`: a row of a slice of rows is defined only inside its expression). C23 makes every such definition one type, so a definition at file scope, in a block and in a parameter agree, and a generated header guards each tag (`SMITH_ARRAY_...`, as for tuples), which is why the struct was not moved to one file-scope place (nothing can write before the function that first uses it). In the body of an instance the replay writes `countof(xs)`, `xs[i]` and `xs[a:b]` of a parameter as the lowering does (`smith_lang_slice_replay`, frames of words and the generic's own tokens, so an index is lowered in turn); when the compiler is asked only the type of an argument (a call inside a call) they are the pointer's element and the slice itself, since the compiler has not read the instance's parameters at the end of the file. Parameters only; more than one dimension is an error that says to take the row first. A slice type held in a `constexpr type` and passed as a type argument (`is_type(plain, plain_type)`, issue #129) is written as `struct tag` alone, never with the struct's definition (the instance and the call define it, once when an `auto` argument has the same type): the definition's `u8` tokens reached clang unlowered and crashed it. A call in an instance's body converts a slice parameter as an ordinary call does (issue #135, `smith_lang_slice_replay_call`, tried by `smith_lang_slice_replay`): when an argument is the bare name of a slice parameter of the instance and the callee (a name the compiler has declared, asked its type as `smith_lang_slice_call` does) wants the same slice with `const` added or `restrict` added or dropped, the argument is written through the struct of the wanted tag (the code of `smith_lang_slice_requalify`, as replay frames); taking `const` away is #126's error, with the generic's place. Corpus: `generic-slice-convert`, `generic-slice-convert-error`. Not done: an argument that is not a bare parameter (a local, `f(g(xs))`), an assignment, an initializer and a `return` in a generic's body get no conversions, and a local `u8 v[:] = xs;` in a generic's body is unsupported altogether (the replay writes a slice local's declaration and `countof(v)` as C, so it is no conversion gap but a missing lowering of locals there).

## A computed result type

A generic's result type may be a call to an ordinary function that returns `type` (issue #102), for example `export slice_of(T, countof(rest)) push_slice(Arena *a, constexpr type T, auto... rest)` with `static type slice_of(type T, usize k)`. One body is recorded; the type is settled for each instance, as `typeof(a)` is. The lowering detects it in `smith_lang_generic_ahead` (a `)` of the result's call followed by a name and `(`; `decl.result_call` makes the parser take the name after it as the declared one), the header keeps the written tokens (the record is unchanged: `smith_lang_result_call` finds the call again from the tokens), and `smith_lang_gen_signature` writes the computed type's tokens instead (`runtime/lang_constexpr.c`, "A result type computed from the parameters"): the arguments are the instance's types as handles (#99) and `countof(rest)` as a number, the compiler runs the call (`value_query`, #97), and the handle's spelling is the result, kept in the instance. A value that is no handle, or a call the compiler cannot evaluate, is an error at the call that asked for the instance. The ordinary function is evaluated where the instance is written, so it is the file's own or an exported function over `type` of an import, which the importer defines (#107); `slice_of` of `smith::arena` is one. A slice result is written with its struct (`smith_lang_gen_put_slice_result`), and `struct smith_array_T_n` in the body names the instance's result struct. Only an existing type or a slice of one can result: no other struct is built.
