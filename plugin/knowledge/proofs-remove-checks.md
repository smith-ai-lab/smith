---
type: concept
title: A proof removes the checks it proved
description: A run of smith prove records the lines of the checks a proved function owns, and an optimized build leaves those checks out while the proof still holds; debug builds keep every check.
tags: [prove, contracts, checks]
---
# A proof removes the checks it proved

Checks run in every build; only a proof removes one. A run of `smith prove` writes, for each `.smith` source, the lines whose slice index and cut checks, arithmetic checks (signed overflow of `+ - * -x`, `/` and `%` by zero or of the minimum by -1, a bad `<<` or `>>`, a float to int out of range), and `contract.assert`, `ensures` and `invariant` calls (never `requires`) a proved function owns, to `.smith/proved/<source>.pts`.

## Example

```text
$ SMITH_PROVED_REPORT=1 smith cc -O2 -c x.smith
```

counts the checks removed and kept.

## Rules

- **The file's first line is `smith-proved <key>`**, the key a SHA-256 of the source's text and the prover's version, then one line number each. The key also covers every other file the proof read a function or global from (`dep <path>` lines: a callee whose `requires` and `ensures` it assumed, an inlined helper, a generic). Changing a callee's contract puts the caller's checks back at the next release build; re-run `smith prove` to remove them again.
- **Only optimized builds remove** (`smith build release`, `fast`). A proved contract call is written as `contract.proved`, which does nothing. Debug (-O0) keeps every check as a safety net against a prover bug, but reads the file too, so a function's `fails` verdict is the same in every build.
- **A check stays** in a helper inlined into the proved function, in a generic's instance, and in a function that failed or was not proved. `--function` writes nothing. A `requires` is never left out, since a caller outside the proof may break it.
- The file is an input of the compile, so a new proof rebuilds only the sources whose set changed. `smith build` never runs the prover.
- `smith prove -v` says how many sources it wrote and how many arithmetic checks were proved and how many remain.

## Interactions

- To pay less at run time, prove more ([contracts](contracts.md), [proving](proving.md)).

## Replaces from C

`-DNDEBUG`, which removes every assert whether or not it was proved.
