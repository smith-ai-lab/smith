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
  `static` or `extern`: `export` is what makes it public). Up to 4 `constexpr type` parameters and 16 parameters. It has
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
- **Not done:** generic types and functions that return `type`, compile-time value parameters (`constexpr u32 tile`),
  argument lists (`auto... rest`), `auto *p` and `const auto x` (an `auto` parameter is `auto name`),
  overloading, specialization, variadic type lists, constraints other than `if constexpr` and `static_assert`,
  pointers to a generic, and a type local to a function as `T`.
