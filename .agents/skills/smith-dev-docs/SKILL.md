---
name: smith-dev-docs
description: "Where a fact goes in smith's repository and how to change AGENTS.md, knowledge/ and the skills, including the dev skills that never ship. Load before editing AGENTS.md, a concept or a skill."
---

# smith-dev-docs

The format is the plugin's [smith-docs](../smith-docs/SKILL.md); this is smith's own use of it.


- `AGENTS.md` is read by every agent every time, so every line must pay for itself: only a rule or decision everyone needs, one line each, with a link to the concept. Anything else (how a feature works, a mechanism, a limit) goes in its concept or skill. No agent adds an AGENTS.md line without the owner. What smith is: `knowledge/` (an OKF bundle, one concept per file, `index.md` listing each with its description word for word). How to do a task: `.agents/skills/<name>/SKILL.md`. Planned work: GitHub issues, nowhere in the repository. `.claude/skills` links to `.agents/skills`.
- Each fact has one home. Skills link knowledge; knowledge never links skills.
- The question for every skill or concept: does someone *using* smith in any project need it, or only someone changing smith? Using: a plugin skill (below), so every project gets it. Changing: a `smith-dev-*` skill or a concept here.
- Skills that teach using smith ship in the plugin straight from `.agents/skills`, are listed in `.claude-plugin/plugin.json`, and stay under about 60 lines; no project carries a copy, so change them here only.
- Skills for changing smith are named `smith-dev` or `smith-dev-*`, are never listed in `plugin.json`, and are loaded by agents working in this repository only. `smith docs` fails when `plugin.json` lists a dev skill or leaves out a shipped one.
- A concept that changes with code changes in the same commit: `docs(knowledge): ...` and `docs(skills): ...` are for commits of documents alone. Small Conventional Commits.
- A new concept: write `knowledge/<name>.md` with `type`, `title`, `description` and `tags`, add its line to `index.md`, link it from the concepts and skills that depend on it, run `smith docs`.
- A new skill that ships: add its folder, list it in `plugin.json` and in the map skill `smith`, and keep its description to the words an agent will see (file names, commands, error text).
- A `feat` commit changes its skill or concept: `smith check` runs `check-docs-gate` (build.c), which fails when a branch has a `feat` commit and none of `.agents/skills/` or `knowledge/` changed since `origin/main` (it passes on `main`, with no `feat` commit, or when `origin/main` cannot be resolved, as in a shallow clone, and says so). No exemptions: a change that needs no docs is a `refactor` or a `fix`. A feature a user reaches for (a language feature, a library, a verb) also gets a short README section with one example in the same PR, which `tests/readme.sh` compiles; internal changes don't.
