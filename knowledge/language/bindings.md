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

Next: [Mutable locals and `if`](mutable-locals.md)
