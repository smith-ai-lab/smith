---
name: smith-docs
description: "smith docs: checking a project's AGENTS.md, knowledge/ (an OKF bundle of concepts) and .agents/skills/, and the format they follow. Load when a project has a knowledge folder or skills, when smith docs or smith check reports 'path: message' about frontmatter, the index, a skill's name, a broken link, 'links the skill', '.claude/skills' or a merge-conflict marker, when declaring concept types or required fields, or when adding a concept or a skill."
---

# smith docs

This skill ships in the smith plugin, from `.agents/skills/smith-docs/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

Every project wants knowledge (what it is) and skills (how to work on it). `smith docs` checks that they follow one format, and `smith new` starts every project with both.

## The format

Each fact has one home:

| Home | Holds |
| --- | --- |
| `AGENTS.md` | rules and decisions, one line each |
| `knowledge/` | what the project is: an [OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md) bundle, one concept per file |
| `.agents/skills/<name>/SKILL.md` | how to do a task, as [Agent Skills](https://agentskills.io) |
| GitHub issues | plans: what to build next |

Skills link knowledge; knowledge never links skills. No `CLAUDE.md` is needed: Claude Code 2.1.277 and later reads `AGENTS.md` itself.

- A concept is `knowledge/<kebab-case-name>.md` with frontmatter `type`, `title` and `description` (one sentence), and usually `tags` and `status: draft` while it is agreed but not built. `index.md` and `log.md` are reserved names.
- `knowledge/index.md` has `okf_version: "0.2"` in its frontmatter and lists every concept as `- [Title](name.md): description`, with the concept's own description word for word.
- A skill's frontmatter has `name`, the same as its folder, and a `description` that says when to load it.
- `.claude/skills` is a symlink to `../.agents/skills`, because Claude Code finds a project's skills only in `.claude/skills`.

## The check

`smith docs [<root>]` checks the project in the current folder (or `<root>`). It prints each problem on stderr as `path: message`, in the order below, then `smith docs: N concepts, M problems` on stdout, and exits 1 if there was a problem. A folder with no `knowledge/` or no skills has nothing to check there.

| Rule | Message |
| --- | --- |
| a concept's file name is not kebab-case | `knowledge/Bad_Name.md: file name is not kebab-case` |
| a concept has no frontmatter | `knowledge/x.md: no frontmatter` |
| `type`, `title` or `description` is missing | `knowledge/x.md: frontmatter lacks title` |
| the type is not allowed | `knowledge/x.md: type 'poem' is not one of convention, architecture, contract, design` |
| a field the type requires is missing | `knowledge/x.md: type 'dataset' needs license_class: commercial-ok, research-only, unknown` |
| there are concepts and no index | `knowledge/index.md: missing` |
| the index has no `okf_version` | `knowledge/index.md: frontmatter lacks okf_version` |
| the index lists a concept twice, or not at all, or lists a file that is no concept | `knowledge/index.md: x.md is listed twice`, `does not list x.md`, `lists x.md, which is not a concept on disk` |
| an index description differs from the concept's | `knowledge/index.md: description of x.md differs from its own` |
| a skill folder has no `SKILL.md`, no frontmatter, a wrong `name`, no `description` | `.agents/skills/a/SKILL.md: missing`, `no frontmatter`, `name 'b' does not match its folder`, `frontmatter lacks description` |
| `.agents/skills` exists and `.claude/skills` is not a symlink to it | `.claude/skills: missing; it must be a symlink to ../.agents/skills`, `is not a symlink`, `links to X, not ../.agents/skills` |
| a relative link in AGENTS.md, knowledge or a skill does not resolve (links in code, `#anchors` and URLs are ignored) | `AGENTS.md: broken link knowledge/gone.md` |
| a document of `knowledge/` links into a skill | `knowledge/x.md: links the skill ../.agents/skills/a/SKILL.md; knowledge never links skills` |
| a merge-conflict marker | `AGENTS.md:12: merge-conflict marker` |

## Concept types and required fields

The default types are `convention`, `architecture`, `contract` and `design`. A project with others declares them as options, in one place: the `docs` step of its `build.c` (OKF allows no extra keys in `index.md`'s frontmatter, so the bundle cannot hold the declaration).

- `--type <name>`, once for each allowed type. With any `--type`, the default types no longer apply, so list them all.
- `--require <type>:<field>` makes `<field>` a must for that type; `--require <type>:<field>=a,b,c` also limits its values.

```sh
smith docs --type contract --type convention --type dataset --type model \
    --require dataset:license_class=commercial-ok,research-only,unknown
```

## In a build.c

`smith new` writes this step, in the `check` alias, so `smith check` covers the docs. The depfile lists every file the check read or found (the documents, the skills, what each link points at), so the step reruns only when one changes, or a link target appears or goes. `smith docs` with no arguments runs the project's `docs` task (build.c) when there is a build.c, and checks the folder as it is when there is none; with arguments it is always the check, and `--depfile <path> --target <path>` writes the depfile in the form of a compiler's `-MF`.

```c
SmithStep docs = {
    .command = smith_list(b, "smith", "docs", "--depfile", "build/docs.stamp.d", "--target", "build/docs.stamp",
                          "--type", "convention", "--type", "architecture", "--type", "contract", "--type", "design",
                          nullptr),
    .inputs = smith_glob(b, smith_list(b, "AGENTS.md", "knowledge/**/*.md", ".agents/skills/**/*.md", nullptr)),
    .stamp = "build/docs.stamp",
    .depfile = "build/docs.stamp.d",
};
smith_step(b, &docs);
smith_alias(b, "docs", docs.outputs);
```

A project whose build.c has no `docs` task runs the check with `smith docs .` (and the options its types need).

## smith new

`smith new` writes `AGENTS.md`, `knowledge/index.md` and `knowledge/program.md` (a concept describing the program it writes), `.agents/skills/<name>-docs/SKILL.md` (what goes where, adding a concept or a skill, running `smith docs`), the `.claude/skills` link, and the `docs` step above. A project that keeps its own `main.c` or `build.c` does not get `program.md`. The generated skill, `<name>-docs`, is the project's guide to maintaining all of it.

## Add a concept or a skill

1. A concept: check `index.md` for one that should hold the fact; write `knowledge/<name>.md` with its frontmatter; add its line to `index.md` with the same description; link it from the concepts and skills that depend on it.
2. A skill: `.agents/skills/<name>/SKILL.md` with `name` (the folder's) and `description`; link the knowledge it needs with `../../../knowledge/<concept>.md`.
3. `smith docs` (or `smith check`) before committing. Docs-only commits are `docs(knowledge): ...` or `docs(skills): ...`.
