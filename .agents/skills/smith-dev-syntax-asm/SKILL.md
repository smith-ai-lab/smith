---
name: smith-dev-syntax-asm
description: "New syntax (D7, #2636): the tagged backtick literal, `asm` followed by backticks at module level (assembly for the object, chosen by `@when`), raw text over several lines, `${` reserved, other tags (#2633), the old `__asm__(\"...\");` fix. Load when writing or porting module-level assembly, or a tagged literal, in the new syntax."
---

# Tagged literals and module assembly

```smith
@when(target.arch == .x86_64 and target.os == .linux)
asm`.text
.globl smith_task_switch
smith_task_switch:
  pushq %rbp
  ret
`

@when(target.arch == .x86_64 and target.os == .linux)
extern @c_name("smith_task_switch") fn switch_stacks(save: **void, to: *void)
```

- A name followed by a backtick is one token: the tag and the text up to the next backtick. The text may span lines and nothing in it is escaped (a `\n` is a backslash and an `n`).
- `asm` is the only tag for now. At module level it is assembly for the whole object (LLVM module asm), in order, and `@when` chooses it like any declaration. The symbols it defines are reached through `extern @c_name("...") fn`.
- `${` inside a literal is an error: holes come later. An unclosed backtick is an error. Any other tag (`html`, `css`, `sql`) is an error until #2633.
- Assembly inside a function waits until after the switch-over.
- `smith lint --fix` writes the old `__asm__("a\n" "b\n");` at file scope as this literal, and an `extern` prototype's `__asm__("sym")` label as `@c_name`.
- The tree-sitter grammar has `tagged_literal`; `editors/neovim/queries/smith/injections.scm` highlights the text of an `asm` literal as assembly.
- Test: `tests/front_asm_literal_test.smith`. Rules: [knowledge/front-end.md](../../../knowledge/front-end.md), D7 in [decisions](../../../knowledge/decisions.md).
