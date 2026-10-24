---
name: smith-lang-modules-errors
description: "The errors of modules and imports in .smith, each with its fix: 'import cycle', 'cannot find module', 'is private; export it', 'must be in a file whose path ends in', 'imports come before any declaration', 'found `x.h`, which is a C header', 'the header of module lists no export', 'needs a `module` line', 'cannot be imported'. Load when smith cc or smith build rejects a module or an import."
---

# Module errors

```text
$ smith cc -c a.smith b.smith             # a imports b, b imports a
a.smith:2:8: error: import cycle: a -> b -> a
$ smith cc -c m.smith                     # import nowhere;
m.smith:2:8: error: cannot find module `nowhere`: no `nowhere.h` in the include paths, no `nowhere.smith`
        among the input files, and nothing at the path the name implies (`nowhere.smith`)
$ smith cc -c p.smith q.smith             # q calls p::secret(), which p does not export
q.smith:3:34: error: use of undeclared identifier 'p::secret'
q.smith:3:34: note: the header of module `p` lists no export `secret`; if the module has one, the header is stale
```

Each error is at the `.smith` text.

- **`import cycle: a -> b -> a`**, or a module that imports itself: break the loop (move the shared part to a third module). `smith build` reports it before any compile.
- **`cannot find module`**: neither its header nor its source was found, and the message lists what was tried. Name the source on the command (`smith cc main.smith foo.smith`), put it at the path the name implies, or pass it to `smith_object` in build.c (by hand: `smith cc --smith-header`, with `-I .smith/include`). It is the one error: uses of the module's names report nothing more. A module whose own source has an error is ``module `foo` cannot be imported: foo.smith:3:12: ...``: fix that file first.
- **`is private to its module ... export it`**, and an exported declaration that names a private type: add `export` to the type.
- **The `module` line does not match the path**: `module a::b` must be in a file whose path ends in `a/b.smith` and must be the first line. In build.c, `smith_object: src/x.smith declares module a::b, so its path must end in a/b.smith`. `export` in a file with no `module` line, or ``needs a `module` line``: add the line.
- **`imports come before any declaration`**: move the `import` up.
- ``import vector_check; found `vector_check.h`, which is a C header``: a C header of that name is on the include path (beside the file, with `-I .`) and won over the header smith writes (the one with a `// smith-module:` line). Rename one, or import the C header as `import "vector_check.h";`.
- **``the header of module `foo` lists no export `bar` ``**: a typo, a private name, or a header from before the module had `bar` (an old `.smith/include/smith/test.h`). `smith build` writes the headers again; `smith cc x.smith y.smith` needs none.
- A C name used bare (`` `sqrtf` is a C name; write `c::sqrtf` ``): `smith-lang-c-interop`.

Modules: `smith-lang-modules`.
