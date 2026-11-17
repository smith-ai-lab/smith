---
type: convention
title: Open Knowledge Format
description: The format the knowledge folder follows, and the conventions this repository adds to it.
---

# Open Knowledge Format

The Open Knowledge Format (OKF) is an open specification from Google Cloud for packaging knowledge as a folder of Markdown files that people and agents can both read. It needs no tool, runtime, or build step. The specification lives in the `GoogleCloudPlatform/open-knowledge-format` repository.

## What the format requires

- A bundle is a folder of UTF-8 Markdown files. Ours is `knowledge/`.
- Each file describes one concept and starts with YAML frontmatter. The only required field is `type`. `title` and `description` are optional.
- Ordinary Markdown links between files are the edges of the graph. A link is directed and carries no type.
- `index.md` is reserved. It lists what a folder contains, so a reader can find the right file without opening all of them. It has no frontmatter.
- `log.md` is also reserved, for a running history. We do not use it, because git is the history.
- A reader must accept types it does not know and keep fields it does not know.

## What this repository adds

- Every file has `type`, `title`, and `description`. The description is one or two sentences that tell a reader whether to open the file.
- We use two types. `specification` is for files that define Smith. `convention` is for files about how the knowledge is kept.
- A file is named for its subject, as a plain noun: `grammar.md`, `compiler.md`, `language/effects.md`.
- Headings carry no numbers. Sections are referred to by name, with a link to the heading, so that adding a section never forces others to be renumbered.
- A fact is stated once, in the file it belongs to. Other files link to it.
- A file says only what is true now, in as few words as will teach it. It holds no history, no record of a decision, and no note of what something replaced. Git is the history.
- [language/](language/index.md) is complete. If a feature is not described there, it is not in Smith.
- Every file in `language/` has `status`, which says how much of the page the bootstrap compiler builds today: `built` for all of it, `planned` for none, and `partial` for some. A `partial` page also has `built`, a line naming what works. The pull request that builds a feature changes these in the same change. The body of a page describes the language and never says what is built.
- The language folder is a tour, and it is how people and agents learn Smith. Its index lists the files in reading order, each file uses only what the files before it introduced, and each ends with a link to the next.
- A file in `language/` teaches one concept, which its title names without an "and". Two concepts are two files, even when the second is short, and a concept built from another comes after it in the tour.
- A file goes from the simplest to the hardest. It opens with one sentence that says what the concept is, and then the smallest complete example of it in its plainest form. Each further idea follows in order of difficulty, each with its own example, and a shorter way to write something comes after the plain way it shortens. Rules and exceptions come after the example they are about, and what few programs need comes last.
- [library/](library/index.md) holds one file for each module of the standard library, named for the module.
- Work to be done is not knowledge. Plans and open problems are GitHub issues.

How to change these files is in the `update-knowledge` skill.
