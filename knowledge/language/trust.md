---
type: specification
title: What is trusted
description: The rule that Smith trusts nothing but a small kernel that checks proofs step by step, the four rules that keep a proof from being a lie, and the list of what is still taken on trust.
status: planned
---

# What is trusted

Smith trusts nothing but a small kernel. Every fact a program relies on is a proof that the kernel has checked, and a program has no way to assert a fact without one. The aim is a language as safe as a proof assistant and as fast as C: a proof costs nothing when the program runs, as it is erased, and what it buys is a check that need not be made.

```
fn first(xs: List[u8]) -> u8
  requires xs.size > 0
{
  xs[0]
}
```

`xs[0]` reads with no test of the index, as the `requires` is a proof that the index is inside. Whatever finds a proof may be as clever as it likes, and may be wrong: [`arith`](arith.md), a [function over code](code.md), a library of facts. What it finds is checked, so a wrong proof is refused and never believed.

## What a kernel is

The kernel is the one part of the compiler whose mistakes would let a false statement through. So it is small, it does one thing, and it is the same in every build: it takes a statement and a proof, as terms of the [core term](code.md#what-code-is), and says whether the proof proves the statement. Everything else is outside it.

## What the kernel does by itself

The kernel checks the steps it is shown and takes none of its own. It does not open a function to see what it does: a proof that needs `append`'s body says where to unfold `append`, and the kernel confirms that the step is what the definition says.

It does one thing unasked: it runs an expression that has nothing unknown in it, with the evaluator the compiler uses for [top-level values](structs.md).

| Statement | How the kernel accepts it |
| --- | --- |
| `append([1], [2]).size == 2` | It runs both sides, as every value is known |
| `append(a, b).size == a.size + b.size` | Only by a proof that writes each step, as `a` and `b` are not known |

So the time a proof takes to check grows with its length and with what it asks to be run, both of which are in the source. The steps are long, and a person does not write them: a [function over code](code.md) writes them once, and it is outside the kernel, so its mistakes are refused and not believed.

## Four rules that keep a proof honest

Each closes a way a proof could be had for nothing.

- **A type holds itself only where it is held.** A type may contain itself as a field, a case, or an element, as [Types that contain themselves](recursive-types.md) describes. It may not contain itself as a parameter of a function it holds. `enum T { wrap: (T) -> T }` is refused: with it, a computation that never ends can be written with no recursion for the check of [termination](termination.md) to see, and a computation that never ends proves anything.
- **What gives a proof carries no effect and ends.** A proof is erased, so the code that gives it is never run. A function that returns a proof and carries `diverge` would prove its statement by never returning. So anything of a proof's type carries no effect at all, and a program with a [`hole`](hole.md) left in it is checked but not built.
- **A statement names only what cannot change.** A proof of `x < 10` says nothing once `x` is given another value. So a statement, as the type of a proof, names no `var`, as the size of an array names none. What `arith` knows of a `var` from one line to the next is its own matter, and is forgotten at each assignment.
- **A proof is used and never looked into.** A proof is passed, held, and given where one is owed. No code picks a value by which proof it was given, as erasing the proof would then change what the program does.

## What is still taken on trust

These are promises that nothing checks today. Each is a debt, and the list is here so that it is seen.

| Trusted | Where it is said | What would end it |
| --- | --- | --- |
| `arith`, which decides what it decides with no proof to show | [What arith proves](arith.md) | `arith` gives a proof that the kernel checks |
| The compiler, from the core term to the executable | [The compiler](../compiler.md) | Not planned. A compiler written in the language it proves things in is where it would start |
| The laws of an `Ord` that a program writes: that `compare` is `equal` when `==` is true, and that the order agrees with itself. `Eq` has no law to trust, as a program does not write it, and `Hash` has none, as any function gives the same answer for the same value | [Comparisons](comparisons.md) | An `impl Ord` gives the proofs with it |
| That a `backward` equals the derivative | [Differentiable](differentiable.md) | Not planned for floats |
| The contract of a foreign function | [Foreign functions](foreign.md) | Nothing: it is where the program meets code Smith did not check, and the `foreign` effect marks it |

No name is added to this list without the owner's agreement, and a fact a library needs is proven in Smith, not put here.

## The forms

| Rule | Refuses |
| --- | --- |
| A type holds itself only where it is held | `enum T { wrap: (T) -> T }` |
| What gives a proof carries no effect and ends | A proof from a function with `diverge`, and a build with a `hole` in it |
| A statement names only what cannot change | A proof about a `var` |
| A proof is used and never looked into | A `match` that picks a value by a proof |

Next: [Array buffers](buffers.md)
