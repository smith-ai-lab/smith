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
- A file is named for its subject, as a plain noun: `language.md`, `grammar.md`, `compiler.md`.
- Headings carry no numbers. Sections are referred to by name, with a link to the heading, so that adding a section never forces others to be renumbered.
- A fact is stated once, in the file it belongs to. Other files link to it.
- [language.md](language.md) is complete. If a feature is not described there, it is not in Smith.
- Work to be done is not knowledge. Plans and open problems are GitHub issues.

How to change these files is in the `update-knowledge` skill.
