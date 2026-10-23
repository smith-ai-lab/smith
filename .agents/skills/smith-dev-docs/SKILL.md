---
name: smith-dev-docs
description: "Where a fact goes in smith's repository and how to change AGENTS.md, knowledge/ and the skills, including the dev skills that never ship. Load before editing AGENTS.md, a concept or a skill."
---

# smith-dev-docs

The format is the plugin's [smith-docs](../smith-docs/SKILL.md); this is smith's own use of it.


- Rules and decisions, one line each, with a link to the concept: `AGENTS.md`. What smith is: `knowledge/` (an OKF bundle, one concept per file, `index.md` listing each with its description word for word). How to do a task: `.agents/skills/<name>/SKILL.md`. Planned work: GitHub issues, nowhere in the repository. `.claude/skills` links to `.agents/skills`.
- Each fact has one home. Skills link knowledge; knowledge never links skills.
- Skills that teach using smith ship in the plugin straight from `.agents/skills`, are listed in `.claude-plugin/plugin.json`, and stay under about 60 lines; no project carries a copy, so change them here only.
- Skills for changing smith are named `smith-dev` or `smith-dev-*`, are never listed in `plugin.json`, and are loaded by agents working in this repository only. `smith docs` fails when `plugin.json` lists a dev skill or leaves out a shipped one.
- A concept that changes with code changes in the same commit: `docs(knowledge): ...` and `docs(skills): ...` are for commits of documents alone. Small Conventional Commits.
- A new concept: write `knowledge/<name>.md` with `type`, `title`, `description` and `tags`, add its line to `index.md`, link it from the concepts and skills that depend on it, run `smith docs`.
- A new skill that ships: add its folder, list it in `plugin.json` and in the map skill `smith`, and keep its description to the words an agent will see (file names, commands, error text).
