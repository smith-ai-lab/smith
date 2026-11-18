---
type: specification
title: Structs
description: Structs with named fields and defaults, reading a field, assigning to a field of a var, and a definition at the top level of a file.
status: partial
built:
  - "structs with fields and defaults, reading a field, and assigning to a field of a `var` with `=`"
---

# Structs

A struct is a value that holds several values, each under a name. It is declared with `struct`, and its fields are written one per line, each as `name: Type`.

```
struct Rect {
  w: i32
  h: i32
}
```

## Making one and reading a field

A struct is made by calling its name, with every field given by name. `.` reads a field.

```
fn wider(a: Rect, b: Rect) -> bool { a.w > b.w }

fn main() ! console, alloc {
  r = Rect(w: 3, h: 4)
  print("{r.w} {r.h}")
}
```

This prints `3 4`. A struct is a value like any other: it can be bound, passed to a function, and returned from one.

The fields are given by name, so the order they are given in is not the order they are declared in. `Rect(h: 4, w: 3)` is the same value as `Rect(w: 3, h: 4)`.

A call that leaves a field out, names one the struct does not have, gives a field a value of another type, or gives fields without names is refused.

```
r = Rect(w: 3)
```

This gives "`Rect` needs a value for `h`".

```
r = Rect(w: 3, h: 4, d: 1)
```

This gives "`Rect` has no field named `d`".

```
r = Rect(w: 3, h: "four")
```

This gives "this gives text, where a `i32` is needed".

```
r = Rect(3, 4)
```

This gives "the fields of `Rect` are given by name", with the fix "write `w: ` before it". So there is one way to build a struct, the one with names.

Reading a field the struct does not have is refused the same way: `r.depth` gives "`Rect` has no field named `depth`".

## One field to a line

A struct's fields are written one to a line. `struct Rect { w: i32, h: i32 }` is refused: "expected a line break or `}`, found `,`".

## Changing a field of a `var`

A field of a [`var`](var.md) can be assigned with `=`.

```
fn widen(r: Rect, to: i32) -> Rect {
  var wide = r
  wide.w = to
  wide
}

fn main() ! console, alloc {
  r = Rect(w: 3, h: 4)
  w = widen(r, 9)
  print("{r.w} {w.w} {w.h}")
}
```

This prints `3 9 4`. Nothing else can name the value a `var` holds, so the change is seen through that `var` alone: it now holds a copy of the struct with the field replaced, and `widen` leaves `r` as it was. A field of a field is assigned the same way, as `o.inner.v = 4` is for a `var` named `o`.

The other forms of assignment, `+=`, `-=`, `*=`, and `/=`, work on a field as they do on a `var`. With `wide.w += 2`, the field `w` of `wide` becomes the field plus 2.

Assigning to a field of a name that is not a `var` is refused.

```
fn main() ! console, alloc {
  r = Rect(w: 3, h: 4)
  r.w = 5
}
```

This gives "`r` is not a `var`, so it cannot be assigned to", with the fix "declare it with `var`: write `var r` where it is defined". A value of the wrong type is refused as it is for a call: `r.w = "x"` on a `var r` gives "this gives text, where a `i32` is needed". Assigning to a field the struct does not have gives "`Rect` has no field named `depth`".

## Defaults

A field may have a default, written after its type with `=`, as a [parameter's](functions.md) is. A default is a fixed value, and any field may have one, wherever it stands. Construction may then leave the field out.

```
struct Border {
  width: i32 = 1
  inset: i32 = 0
}

fn main() ! console, alloc {
  thin = Border()
  wide = Border(width: 4)
  print("{thin.width} {thin.inset} {wide.width} {wide.inset}")
}
```

This prints `1 0 4 0`. A field that comes after one with a default may have none, because fields are always given by name.

```
struct Margin {
  width: i32 = 1
  inset: i32
}

fn main() ! console, alloc {
  a = Margin(inset: 2)
  b = Margin(width: 4, inset: 3)
  print("{a.width} {a.inset} {b.width} {b.inset}")
}
```

This prints `1 2 4 3`. A field with no default still has to be given: `Margin()` gives "`Margin` needs a value for `inset`".

## At the top level of a file

A definition that is not a function may sit at the top level of a file, outside every function. Here `unit` is a `Rect` that any function of the file can read.

```
unit = Rect(w: 1, h: 1)
```

## The forms

| Written | Is |
| --- | --- |
| `struct S { a: T }` | A struct with a field `a` of type `T`, one field to a line |
| `struct S { a: T = e }` | The same, where construction may leave `a` out and it is `e` |
| `S(a: e)` | A `S` whose field `a` is `e`, every field named |
| `s.a` | The field `a` of `s` |
| `s.a = e` | Replaces the field `a` of the `var` `s` |

Next: [Tuples](tuples.md)
