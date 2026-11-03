---
name: smith-dev-syntax-program
description: "A whole program in the new syntax (#1428): imports, a constant, a struct with pub fields, a payload enum, match, a list built with method calls, a for expression and a fused sum. Load for a first look at the new syntax, or a template for a new example or test."
---

# A whole program

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

```smith
stdio := import c <stdio.h>
list := import std.list

const TAU := 6.283185307179586

pub struct Circle {
    pub radius: f64
}

pub enum Shape {
    circle: Circle
    square: f64
    empty
}

fn area(s: Shape) -> f64 {
    match s {
        .circle(c) => TAU / 2 * c.radius * c.radius
        .square(side) => side * side
        .empty => 0
    }
}

pub fn main() {
    mut shapes: list.List(Shape)
    shapes.push(.circle(Circle(radius = 1)))
    shapes.push(.square(2))
    total := sum(for s in shapes.items() { s.area() })   // area is in this module: the method form is the one way
    stdio.printf("%.2f\n", total)
}
```

`shapes.items()` stands for whatever slice or stream `std.list` gives; the std library's names are not part of #1428.
