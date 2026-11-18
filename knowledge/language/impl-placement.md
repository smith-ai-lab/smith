---
type: specification
title: Where an impl lives
description: An impl is not imported, it is visible everywhere, and it lives in the module that defines the trait or the type.
status: planned
---

# Where an impl lives

An `impl` is not imported. It is visible everywhere, and it lives in the module that defines the trait or the type.

```
trait Area[T] {
  fn area(x: T) -> f32
}

struct Rect {
  w: f32
  h: f32
}

impl Area[Rect] {
  fn area(x: Rect) -> f32 { x.w * x.h }
}
```

Written in one file, the trait `Area` and the type `Rect` are defined in the module of that file, and so is the `impl`. Another module imports `Area` and `Rect` and has `Area[Rect]` in force with no `use` for the `impl`.

```
use shapes (Area, Rect)
```

## An impl in the wrong module

An `impl` in a module that defines neither the trait nor the type is refused. This keeps one implementation per type checkable.

```
use shapes (Area, Rect)

impl Area[Rect] {
  fn area(x: Rect) -> f32 { 0.0 }
}
```

`Area` and `Rect` come from `shapes`, so this `impl` has no place in the module that wrote it.

Next: [Projects](projects.md)
