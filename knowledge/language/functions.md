---
type: specification
title: Functions
description: Typed parameters, a return type, a block whose last expression is its value, named arguments, and defaults.
status: partial
built: "typed parameters and return types over `str`, `bool`, and the types a program defines, blocks, named arguments, defaults, a final lambda parameter after them, and `//` comments"
---

# Functions

A function has typed parameters, a return type, and a block for a body. The last expression of a block is its value. A function with nothing to return leaves the return type out. Arguments are evaluated from left to right. A comment starts with `//` on its own line.

```
// The area of a rectangle.
fn area(w: f32, h: f32) -> f32 { w * h }

fn double(x: f32) -> f32 { x * 2.0 }
```

A call is `area(2.0, 3.0)`, and arguments may be named, as in `area(w: 2.0, h: 3.0)`. Arguments by position come first, in the order of the parameters. Once one argument is named, every argument after it is named too, as in `area(2.0, h: 3.0)`.

A parameter may have a default, written after its type. A call that leaves the argument out gets the default.

```
fn scale(x: f32, by: f32 = 2.0) -> f32 { x * by }
```

`scale(3.0)` is `6.0`, and `scale(3.0, 10.0)` and `scale(3.0, by: 10.0)` are both `30.0`.

- Parameters with defaults come after those without. The one exception is a final lambda parameter, described under [Lambdas, `with`, and `try`](lambdas.md).
- Naming an argument is how a call sets a later default and leaves an earlier one alone.
- A default is a fixed value. It is an expression that uses no parameter, and it is worked out when the program is built, so it never depends on when or where the call happens.
- The compiler fills in a missing argument where the call is written. The function always receives every argument, so a default costs nothing at run time.

Next: [Bindings](bindings.md)
