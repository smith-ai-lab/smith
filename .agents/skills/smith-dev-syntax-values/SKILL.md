---
name: smith-dev-syntax-values
description: "New syntax (#1428): declarations (`a := 3`, `mut a: usize`, `const`), assignment, destructuring, `_ :=`, no shadowing, the types (`i32`, `?T`, `[]T`, `[5]T`, `[,3]T`, tuples, `fn(...)`, `type`, `vec(T, N)`), literals, pointers (`&x`, `p.*`) and `as`. Load when writing values or types in the new syntax."
---

# Values, types and pointers

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Declarations

```smith
a := 3                       // a constant; the type is inferred (i32)
b: f32 = 2.5                 // the type written
mut count: usize             // a variable; no value means zero
mut total := 0.0
count += 1                   // assignment: only to a mut, and a statement, never an expression
total = total + 1.5
(q, r) := divmod(17, 5)      // destructuring
_ := f()                     // an unused result is an error: discard it explicitly
const TAU := 6.28            // must be computed at compile time
IntPair := Pair(i32)         // an alias is a constant of type `type`
```

- A colon after the name declares (`:=` defines); a bare `=` assigns.
- Shadowing is an error: a name already in scope cannot be declared again ("assign with `=`, or choose another name"). A local never hides a module.
- A declaration whose value is known at compile time is a compile-time constant on its own; `const` demands it.
- A bare integer literal is `i32` and a float literal `f64`, unless a type is written or expected.

## Pointers and casts

```smith
mut p := Point(x = 1, y = 2)
q := &p                      // *mut Point, because p is mut; &x of a constant is *T
q.x += 1                     // fields through a pointer: no ->
copy := q.*                  // the whole value
q.* = Point(x = 0, y = 0)
n := copy.x as i32           // a cast; `as` binds tighter than arithmetic
```

- A `*mut T` converts to `*T` on its own. There is no `&mut` (an error naming `&x`).
- Prefix `*p` is an error whose fix is `p.*`; `(T)x` is an error whose fix is `x as T`.

## Not settled yet

Ask the lead before writing these; each is on the open list of #1428: a slice whose elements are writable (`[]mut T`?), sub-slices `xs[a:b]` and `p[a:b]`, `countof`/`sizeof`/`alignof`, `[_]` lengths, mutable globals, function-local `static`, `void *`, overflow and narrowing `as`, the null pointer, `++`.
