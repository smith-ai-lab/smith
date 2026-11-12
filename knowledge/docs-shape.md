---
type: convention
title: smith docs holds the shape of skills and concepts
description: smith docs fails a project with more than 20 skills, a skill whose description is over 300 characters or names no trigger, a concept over 4,000 bytes, or a plugin that links out of its folder; the old files are on ratchets that only go down.
tags: [docs, skills, plugin, check]
---
# smith docs holds the shape of skills and concepts

A shape that is only written down drifts: 161 skills grew, about 80% of their text "what", while the rule said knowledge is the what and skills are the how. So `smith docs` (runtime/docs.c) fails on it, one line per problem that says the rule and the reason (#3411).

| Rule | Why |
|---|---|
| A skill has a `description` of at most 300 characters that names a trigger: `Load when`, `Load before`, `Load on` or `Load for`. | An agent picks a skill from the names and descriptions in its listing alone. The listing has a budget of about 1% of the context window (about 20,500 characters here, about 8,000 on a 200k window); over it, descriptions are dropped and the skill is never matched. |
| The repo set (`.agents/skills/`) and the plugin set (`plugin/skills/`) together hold at most 20 skills. | The same listing: a session lists them all. |
| A concept under `plugin/knowledge/` is at most 4,000 bytes; so is a concept of `knowledge/`, except the ones on the ratchet, which may only warn when they grow. | An agent reads a concept whole. A 64 KB concept is about 15,000 tokens for one lookup; a small one is one fact in one home, found by its name in the index. |
| `plugin/knowledge/index.md` lists each concept there with its description word for word, as `knowledge/index.md` does. | The index is how a concept is found. |
| A file under `plugin/` links nothing outside `plugin/`, and a file under `plugin/knowledge/` links no skill. | An installed plugin is a copy of its folder only: a link out of it points at nothing on a user's machine. |

The check also prints the number of skills when it is over 20. `plugin/` need not exist: the plugin rules apply once it does.

## The ratchets

While the old files are converted, three lists in `runtime/docs_ratchet.h` let them through; each only shrinks, and a worker who fixes an entry deletes its line.

- `smith_docs_skills_ratchet`: the most skills allowed now (it ends at 20, and the ratchet is then deleted). `--max-skills N` replaces it.
- `smith_docs_description_ratchet`: the skills of `.agents/skills/` whose description broke the first rule on the day it began. A new skill is never on it, and none of `plugin/skills/` is.
- `smith_docs_size_ratchet`: the concepts of `knowledge/` over 4,000 bytes, with their size that day. One that grows prints a warning line with its two sizes and does not fail, so other pull requests keep working while the list is converted; a concept not on the list fails at the cap; the list only loses entries (62 today). A concept that shrinks stays listed until its line is deleted.

## The docs gate

`check-docs-gate` (build.c) fails a branch that has a `feat` commit unless the branch changed something under `knowledge/` or `plugin/knowledge/`. A change to a skill alone no longer counts: a new verb or feature always has a "what".
