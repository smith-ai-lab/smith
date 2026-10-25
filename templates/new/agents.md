# @NAME@

One home per fact. This file holds the rules and decisions, one line each. [knowledge/](knowledge/index.md) holds what
@NAME@ is, and the skills in [.agents/skills/](.agents/skills/@KEBAB@-docs/SKILL.md) hold how to do a task. Plans go in
the project's issues. A line stays only if an agent needs it and the code can't say it quickly.

Start every change with the [@KEBAB@-docs](.agents/skills/@KEBAB@-docs/SKILL.md) skill, and run `smith check` before
you commit: it checks the formatting, the lint and these documents (`smith docs`).

- **There is no build file and no config file.** smith builds by convention: `main.smith` or `main.c` at the root is the
  program, named for the folder; what it imports or includes is compiled with it; a `*_test.smith` (`*_test.c`) beside the module it tests is its tests; integration tests, if any, go in `tests/`.
  `smith build` is the debug profile (sanitizers on), `smith build release` and `smith build fast` optimize; every profile keeps the contracts and assertions
  (only `smith prove` removes a check). `smith fmt` and `smith lint` use smith's own style and checks.
- **Effects come through parameters from `main`.** `main` makes the io, `io::Io io = io::make(argc, argv)` in `.smith`
  (`SmithIo io = smith_io_make(argc, argv)` in C), and a function that touches the world takes the part it needs
  (`io::Files *`, `io::Console *`, `io::Clock *`...; `SmithFiles *` in C), so its signature says what it can do and a
  test passes a fake (`io::make_test`; `smith_io_make_test` in C). `smith lint` reports a raw call (`printf`, `fopen`)
  with the capability to take instead; the smith-io skill has the rules.
- **The verbs do the work:** `smith run`, `smith test`, `smith fmt`, `smith lint`, `smith docs`, `smith prove` and `smith
  check`. The skills of the smith plugin, which `.claude/settings.json` enables, say how.
- **To change a default, write it down first.** `smith fmt --dump-config > .clang-format` and `smith lint --dump-config >
  .clang-tidy` make the style and the checks this project's own. When the build needs more than the conventions give (a
  package, a second program, other flags), the project needs a build file: `smith build --dump-config` prints the one
  that does what the conventions do (`build.smith` or `build.c`, by the project's language), to edit; load the
  smith-build skill.
