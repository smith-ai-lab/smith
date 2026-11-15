---
type: specification
title: Bindings
description: How x = e defines an immutable name, once per function.
---

# Bindings

`x = e` defines a new immutable name. A name is defined once per function, so there is no shadowing.

```
fn hypot(a: f32, b: f32) -> f32 {
  aa = a * a
  bb = b * b
  sqrt(aa + bb)
}
```

A binding may state its type, written as a parameter's is: `half: f32 = 0.5`. Most bindings do not need to, because the type of the value is already known.

Next: [Mutable locals and `if`](mutable-locals.md)
