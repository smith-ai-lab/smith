---
type: specification
title: Mutable locals and if
description: Locals that change, declared with var, and if as an expression.
status: partial
built: "`var` and `=` on text, and an error for assigning to a name that is not a `var`"
---

# Mutable locals and `if`

`var` defines a local that can change, and it may state its type, as in `var n: i32 = 0`. `=` gives a `var` a new value, and `+=`, `-=`, `*=`, and `/=` accumulate into it. A name that is not a `var` never changes, and assigning to one is a compile error. `if` is an expression, and the words `not`, `and`, and `or` combine conditions.

```
fn relu(x: f32) -> f32 {
  if x < 0.0 { 0.0 } else { x }
}

fn grow(start: f32, rate: f32) -> f32 {
  var total = start
  total = total * (1.0 + rate)
  total += 1.0
  total
}
```

So `x = e` defines `x` when the function has no such name, and assigns when `x` is a `var`.

Next: [Structs and tuples](structs.md)
