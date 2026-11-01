---
name: smith-docs
description: "smith docs and the format it checks: knowledge/ (an OKF bundle of concepts) and .agents/skills/, where each fact lives, and how to add a concept or a skill. Load when a project has a knowledge folder or skills, or when adding a concept or a skill."
---

# smith docs

Every project wants knowledge (what it is) and skills (how to work on it). `smith docs` checks that they follow one format, and `smith new` starts every project with both. The agent harness loads the skills, so a project needs no separate guidance file.

## The format

Each fact has one home:

| Home | Holds |
| --- | --- |
| `knowledge/` | what the project is: an [OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md) bundle, one concept per file |
| `.agents/skills/<name>/SKILL.md` | how to do a task, as [Agent Skills](https://agentskills.io) |
| GitHub issues | plans: what to build next |

Skills link knowledge; knowledge never links skills.

- A concept is `knowledge/<kebab-case-name>.md` with frontmatter `type`, `title` and `description` (one sentence), and usually `tags` and `status: draft` while it is agreed but not built. `index.md` and `log.md` are reserved names.
- `knowledge/index.md` has `okf_version: "0.2"` in its frontmatter and lists every concept as `- [Title](name.md): description`, with the concept's own description word for word. One entry to a line, and the entries of each section (a run of list lines under a heading) sorted by file name, bytewise, so a merge of two branches that both add a line keeps one line each and `smith docs` names the line when it does not.
- A skill's frontmatter has `name`, the same as its folder, and a `description` that says when to load it.
- A project that ships a plugin lists in `.claude-plugin/plugin.json` exactly the skills that teach using it; skills for changing the project are named `<plugin>-dev` or `<plugin>-dev-*`, stay in the repository and are never listed.
- `.claude/skills` is a symlink to `../.agents/skills`, because Claude Code finds a project's skills only in `.claude/skills`.

## In a project

`smith docs` needs no build.c and no task, and `smith check` runs it. A project that has a build.c may not name a task `docs` (a verb).

## smith new

`smith new` writes `knowledge/index.md` and `knowledge/program.md` (a concept describing the program it writes), `.agents/skills/<name>-docs/SKILL.md` (what goes where, adding a concept or a skill, running `smith docs`), the `.claude/skills` link, with the types in its index. A project that keeps its own `main.smith`, `main.c` or build file does not get `program.md`. The generated skill, `<name>-docs`, is the project's guide to maintaining all of it.

## Add a concept or a skill

1. A concept: check `index.md` for one that should hold the fact; write `knowledge/<name>.md` with its frontmatter; add its line to `index.md` in its sorted place, with the same description; link it from the concepts and skills that depend on it.
2. A skill: `.agents/skills/<name>/SKILL.md` with `name` (the folder's) and `description`; link the knowledge it needs with `../../../knowledge/<concept>.md`.
3. `smith docs` (or `smith check`) before committing. Docs-only commits are `docs(knowledge): ...` or `docs(skills): ...`.

Also in this family: [smith-docs-check](../smith-docs-check/SKILL.md).
