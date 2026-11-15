# Smith

Smith is a programming language. This repository holds its definition and its compiler.

- Read `knowledge/index.md` before any work. The `knowledge/` folder says what Smith is.
- `knowledge/language.md` is complete. Anything not in it is not in the language. Do not add a feature because it seems useful.
- When a change alters the language, the grammar, or the compiler's design, change the files in `knowledge/` in the same pull request. Follow the `update-knowledge` skill.
- Every change follows the `ship-change` skill: a branch, conventional commits, a pull request, and a rebase merge once the checks pass.
- If `knowledge/` does not answer a question the work depends on, ask in a comment on the issue. Do not invent an answer.
- Skills live in `.agents/skills/`. They say how to do things. `knowledge/` says what is true.
- The compiler in `bootstrap/` is written in Rust with no dependencies. It is retired once Smith compiles itself.
