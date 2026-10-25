---
name: smith-docs-check
description: "The checks of smith docs with their messages (path: message), concept types and required fields declared in knowledge/index.md, and the plugin.json rule. Load when smith docs or smith check reports a frontmatter, index, skill name, broken link, 'links the skill', '.claude/skills', plugin.json or merge-conflict problem."
---

# The check

`smith docs [<root>]` checks the project of `<root>`, or, when none is named, of the nearest folder at or above the current one that has an `AGENTS.md`, looking no higher than the repository's root (a build.c in `repo/app/` checks the AGENTS.md and `knowledge/` of `repo/`; with none found it is the current folder). `smith check` uses the same rule. It prints each problem on stderr as `path: message`, in the order below, then `smith docs: N concepts, M problems` on stdout, and exits 1 if there was a problem. A folder with no `knowledge/` or no skills has nothing to check there.

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
| a skill has more than 60 lines | `.agents/skills/a/SKILL.md: 61 lines; a skill stays under 60: split it or shorten it` |
| `.agents/skills` exists and `.claude/skills` is not a symlink to it | `.claude/skills: missing; it must be a symlink to ../.agents/skills`, `is not a symlink`, `links to X, not ../.agents/skills` |
| a relative link in AGENTS.md, knowledge or a skill does not resolve (links in code, `#anchors` and URLs are ignored) | `AGENTS.md: broken link knowledge/gone.md` |
| a document of `knowledge/` links into a skill | `knowledge/x.md: links the skill ../.agents/skills/a/SKILL.md; knowledge never links skills` |
| a project with `.claude-plugin/plugin.json` lists a dev skill (named `<plugin>-dev` or `<plugin>-dev-*`), a skill that is not on disk, or leaves out a skill that is not a dev skill | `.claude-plugin/plugin.json: lists the dev skill p-dev-x, which must not ship`, `lists gone, which is not a skill on disk`, `does not list the skill b` |
| a merge-conflict marker | `AGENTS.md:12: merge-conflict marker` |

## Concept types and required fields

The default types are `convention`, `architecture`, `contract` and `design`. A project with others declares them in `knowledge/index.md`'s frontmatter, which is where the check reads them (as `.clang-format` holds the style):

```text
---
okf_version: "0.2"
types: contract, convention, dataset, model
requires: dataset:license_class=commercial-ok,research-only,unknown; model:owner
---
```

- `types:` lists the allowed types, separated by commas. With it, the default types no longer apply, so list them all.
- `requires:` makes a field a must for a type, separated by `;`: `<type>:<field>`, or `<type>:<field>=a,b,c` to limit its values.
- On the command line, `--type <name>` (once for each allowed type) and `--require <type>:<field>[=a,b,c]` do the same and replace what the index declares. `smith docs` with no arguments is the check of that project with the index's declaration; `--depfile <path> --target <path>` writes the files it read in the form of a compiler's `-MF`.

Also in this family: [smith-docs](../smith-docs/SKILL.md).
