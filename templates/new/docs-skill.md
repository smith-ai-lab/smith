---
name: @KEBAB@-docs
description: "Maintaining @NAME@'s documentation: what belongs in knowledge (the what) and skills (the how), adding or changing a knowledge concept or a skill, keeping the index and links current, and running smith docs. Load when a change touches docs, or code that a concept describes."
---

# Maintain knowledge and skills

Each fact has one home:

| Home | Holds | Example |
| --- | --- | --- |
| [knowledge/](../../../knowledge/index.md) | what @NAME@ is: conventions, architecture, contracts, design | the sum is asserted |
| `.agents/skills/` | how to do a task, step by step | how to add a module |
| the project's issues | plans and the roadmap: what to build next, its scope and acceptance | add a command line |

When a fact seems to belong in two homes, it lives in one and the other links to it. Skills link knowledge; knowledge never links skills. A line stays only if an agent needs it and the code can't say it quickly: no history or rationale, and plans go in an issue. smith gives the project no AGENTS.md; its skills (the smith plugin's and this one) carry what an agent needs.

## Knowledge is an Open Knowledge Format bundle

The format is [OKF](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md).

- One concept per file: `knowledge/<concept>.md`, in kebab-case. `index.md` and `log.md` are reserved names.
- Frontmatter:
  - `type`, required: `convention`, `architecture`, `contract` or `design`
  - `title`, and `description` as one sentence
  - `tags`
  - `status: draft` while the concept is agreed but not built
- The body states rules in short declarative sentences, with tables for mappings. A draft concept may end with one "Today" line saying what exists now.
- Concepts link each other with relative links (`[the program](program.md)`). The sentence around a link says how the two relate.
- `index.md` has `okf_version: "0.2"` in its frontmatter and lists every concept, with its `description` copied word for word, as `- [Title](name.md): description`, one entry to a line, the entries of each section sorted by file name (`smith docs` names the line of an entry that is out of order or listed twice, which a bad merge causes).
- The types a concept may have, and the fields a type must have, are declared in `index.md`'s frontmatter: `types: convention, architecture, contract, design`, and `requires: <type>:<field>[=<value>,...]; ...` for a field a type must have (values separated by commas, requirements by `;`).

### Add or change a concept

1. Check `index.md` for an existing concept that should hold the fact.
2. Write or edit the file, frontmatter included.
3. Update `index.md` in the same commit.
4. Link to it from the concepts and skills that depend on it.
5. When code changes what a concept says, update the concept in the same commit.
6. When code lands that a draft describes, make the concept match the code and remove `status: draft`.

## Skills follow the Agent Skills format

- One folder per skill: `.agents/skills/<name>/SKILL.md`. The name is kebab-case and matches the folder.
- Frontmatter has `name` and `description`. The description says when to load the skill; it's all an agent sees before opening it.
- The body is numbered procedures and checklists. Link the knowledge a step depends on (`../../../knowledge/<concept>.md`) instead of restating it.
- `.claude/skills` is a symlink to `../.agents/skills`, because Claude Code finds a project's skills only there. Add skills only under `.agents/skills`.
- smith's own skills come from the smith plugin, which `.claude/settings.json` enables. They are not copied here.

## Before committing

1. `smith docs` passes. It is part of `smith check`, and it prints each problem as `path: message`:
   - every concept has a kebab-case file name and frontmatter with `type`, `title` and `description`, and a type the project allows;
   - `knowledge/index.md` lists exactly the concepts on disk, with matching descriptions;
   - every skill's `name` is its folder, and it has a `description`;
   - `.claude/skills` links to `../.agents/skills`;
   - every relative link in knowledge and skills resolves, and knowledge links no skill;
   - no document has a merge-conflict marker.
2. Docs-only commits are `docs(knowledge): …` or `docs(skills): …`.
