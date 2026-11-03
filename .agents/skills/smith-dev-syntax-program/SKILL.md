---
name: smith-dev-syntax-program
description: "A whole program in the new syntax (#1428): imports, `#` comments, a constant, a struct with pub fields, a payload enum, match, a list from `list.init(Shape)` with method calls, parentheses construction, a for expression and a fused sum, all in indentation blocks. Load for a first look at the new syntax, or a template for a new example or test."
---

# A whole program

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

```smith
import c <stdio.h>
import std.list
from std.math import sum

const TAU = 6.283185307179586

## A circle around the origin.
pub struct Circle:
    pub radius: f64 = 1

pub enum Shape:
    circle: Circle
    square: f64
    empty

fn area(s: Shape) -> f64:
    match s:
        .circle(c): TAU / 2 * c.radius * c.radius
        .square(side): side * side
        .empty: 0

pub fn main() -> i32:
    let mut shapes = list.init(Shape)       # a type is an ordinary argument
    shapes.push(.circle(Circle(radius = 2)))
    shapes.push(.circle(Circle()))          # every field has a default
    shapes.push(.square(2))
    let total = sum(for s in shapes.items(): s.area())   # area is in this module: the method form is the one way
    stdio.printf("%.2f\n", total)
    0
```

`shapes.items()` stands for whatever slice or stream `std.list` gives; the std library's names are not part of #1428.

## HTML with trailing blocks

```smith
let page = html:
    head:
        title: "hello"
    body:
        h1(class = "my class"): "my heading"
        for todo in todos:
            li(class = "done"): text(todo.title)
```

A call whose last parameter is a `fn()` takes an indented block after `:`; with no other arguments the parentheses go too. One whose last parameter takes arguments is written `sort(people) fn(a, b): a.age < b.age` ([smith-dev-syntax-functions](../smith-dev-syntax-functions/SKILL.md)).
