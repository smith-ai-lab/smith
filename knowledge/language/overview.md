---
type: specification
title: Overview
description: What Smith is for, the rules that hold throughout the language, and the four principles its design follows.
status: partial
built: "blocks in braces for every body, statements separated by line breaks, and a name defined once per function"
---

# Overview

Smith is a language written mainly by models, for three kinds of program: linear algebra with automatic differentiation on CPU and GPU, concurrent servers, and native applications. Its compiler is to be written in Smith itself.

Models write most Smith code, and people direct it and read it. So every choice in the design is judged by what a model gets right without having to remember anything. There is one way to write each thing. Nothing a signature could state is left to context. And the compiler answers questions and proposes the missing piece, whether that is an effect, a bound, or a contract.

What Smith is after is the fastest and the safest program that can be had, on every kind of hardware. So when a few more written words buy a check when the program is built, Smith asks for the words. A model writes them cheaply, and nothing is then left to be found out while the program runs.

This folder builds the language up one concept at a time. Each file uses only what earlier files introduced, and the examples grow into an MLP and then a transformer. The syntax is defined in [grammar.md](../grammar.md), and how it is compiled is in [compiler.md](../compiler.md). Nothing here has been implemented or measured.

Smith's types are dependent: a type may mention a value, as `[n]f32` mentions `n`, and a statement about values is itself a type whose values are its proofs. A proof is written in the source, checked when the program is built, and gone when it runs, along with anything else that only a type or a proof uses. Numbers are machine integers, and the proof is that a value fits. So what other languages test while the program runs, an index in range or a sum that does not overflow, Smith settles beforehand and pays nothing for.

A few rules hold throughout. Every body is a block in braces, statements are separated by newlines, and each program has one canonical formatting. A function's signature states everything a caller needs. Values are never shared by reference. Proofs are written in the source and only checked by the compiler.

The design follows four principles:

- Fewest features. Nothing speculative is included, and a feature is added only when real code needs it. Nothing is kept for the sake of what came before.
- The syntax is fixed and its meaning is extensible through traits, as with operators, literals, `loop`, and `try`.
- Effects are permissions. What a function may do is visible in its signature.
- No dependencies. Everything is written in Smith, with no other language in a Smith program.

Next: [A first program](first-program.md)
