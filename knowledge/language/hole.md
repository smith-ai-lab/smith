---
type: specification
title: hole
description: hole stands in for code or a proof not yet written, and the compiler reports the type and the goal it needs.
status: planned
---

# hole

`hole(name)` stands in for code or a proof that is not yet written. The compiler reports what the place needs, so the author knows what to write.

```
fn clamp(x: u8, lo: u8, hi: u8) -> r: u8
  requires lo <= hi
  ensures lo <= r and r <= hi
{
  hole(body)
}
```

The compiler reports for `body` the type `u8`, the effects the function declares, and the goal `lo <= r and r <= hi`, which is what the code that fills it must prove. [`smith hole`](../compiler.md) prints the same report.

## In place of a proof

A hole is also written where a proof is needed, and the report names the statement that is wanted.

```
fn positive(raw: Nat) -> Pos {
  Pos(value: raw, above: hole(inside))
}
```

The report for `inside` is the statement `raw > 0`, which a [`check`](check.md) would give.

## A hole is not built

A program with a `hole` in it is checked, and it is not built or run. A hole where a proof is owed would otherwise be a fact taken with no proof, which [nothing in Smith is](trust.md).

## The forms

| Written | Stands in for |
| --- | --- |
| `hole(name)` where an expression is wanted | Code of the type, and effects, the place needs |
| `hole(name)` where a proof is wanted | A proof of the statement the place needs |

Next: [What is trusted](trust.md)
