---
type: specification
title: Mutable locals and if
description: Locals that change with var and :=, and if as an expression.
---

# Mutable locals and `if`

`var` defines a local that can change, and it may state its type, as in `var n: i32 = 0`. `:=` replaces its value, and `+=`, `-=`, `*=`, and `/=` accumulate into it. `if` is an expression, and the words `not`, `and`, and `or` combine conditions.

```
fn relu(x: f32) -> f32 {
  if x < 0.0 { 0.0 } else { x }
}

fn grow(start: f32, rate: f32) -> f32 {
  var total = start
  total := total * (1.0 + rate)
  total += 1.0
  total
}
```

Next: [Structs and tuples](structs.md)
