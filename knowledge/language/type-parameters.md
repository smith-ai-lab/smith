---
type: specification
title: Type parameters
description: A name for a type that a function or a type leaves open, written in brackets after its name, how a call or a binding settles it, and a default for one.
status: partial
built: "type parameters"
---

# Type parameters

A type parameter is a name for a type that a function or a type leaves open. It is written in brackets after the name, and it stands for one type wherever it is used.

## A function with a type parameter

`same` returns what it was given, whatever its type.

```
fn same[T](x: T) -> T { x }
```

`T` is the type of `x` and the type of the result. A call settles it from its arguments: `same("x")` is a `str`, and `same(3)` is a number, as [Literals](literals.md) says of a number.

A function may have several type parameters, separated by commas. Each is settled by its own argument.

```
fn pick[A, B](a: A, b: B) -> A { a }
```

`pick("x", 1)` is `"x"`, and its type is a `str`.

Every use of one type parameter stands for the same type. `T` in this function is both `a` and `b`, so they must have one type.

```
fn either[T](a: T, b: T) -> T { a }
```

`either("x", 1)` is refused: "this is a number, where text is needed".

## A type with a type parameter

A struct or an enum takes type parameters after its name, and uses them in the types of its fields and cases.

```
struct Pair[A, B] {
  first: A
  second: B
}

enum Maybe[T] {
  some: T
  none
}
```

The type that is written, such as `Pair[str, i64]` and `Maybe[str]`, names each parameter in order.

A function over such a type names its parameters in the brackets after the function's name, and uses them in the type.

```
fn first_of[A, B](p: Pair[A, B]) -> A { p.first }
```

The type of a value that is built must be known. A binding states it, as [Bindings](bindings.md) says of any type.

```
p: Pair[str, i64] = Pair(first: "a", second: 2)
o: Maybe[str] = .some("y")
```

`first_of(p)` is `"a"`. Without the stated type, `p = Pair(first: "a", second: 2)` is refused: "the type `B` of `Pair` is not known here", as the `2` is a number that nothing gives a type.

## What a function may do with a `T`

A function that knows nothing of `T` cannot do what only some types allow, such as asking whether two values are equal.

```
fn same_as[T](a: T, b: T) -> bool { a == b }
```

This is refused: "there is no implementation of `Eq` for `T`". What a function needs of its `T` is said in its signature, as [Bounds and `requires`](bounds.md) says.

## A default for a type parameter

A type parameter may have a default, written with `=` as a function's parameter writes one. A use that leaves the parameter out gets the default.

```
struct Pair[A, B = A] {
  first: A
  second: B
}
```

So `Pair[str]` means `Pair[str, str]`. A default may name a parameter before it, and parameters with defaults come after those without.

```
struct Bad[A = str, B] {
  first: A
  second: B
}
```

This is refused, as `B` has no default and comes after `A`, which has one.

## The forms

| Written | Is |
| --- | --- |
| `fn f[T](x: T) -> T` | A function over any type `T`, settled by the call |
| `fn f[A, B](a: A, b: B)` | Two type parameters, each settled by its argument |
| `struct S[T] { x: T }` | A struct that holds a `T` |
| `enum E[T]` with a case `a: T` | An enum with a case that holds a `T` |
| `S[str]`, `S[A, B]` | The type of a struct or an enum, with each parameter named |
| `struct S[A, B = A]` | A type whose `B` is `A` unless it is written |

Next: [Traits](traits.md)
