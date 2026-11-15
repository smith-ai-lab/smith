---
name: update-knowledge
description: Add to or change the files in knowledge/, which define the Smith language, its grammar, and the compiler's design. Use whenever a change alters what Smith is, or when a fact in knowledge/ is wrong or missing.
---

# Update the knowledge folder

`knowledge/` says what Smith is. Code follows it, never the other way round. The format and its conventions are described in `knowledge/okf.md`. Read that file first.

## Steps

1. Read `knowledge/index.md` and open the file the change belongs to.
2. Make the change in that file, in the section it belongs to. State each fact once and link to it from elsewhere.
3. If the change adds or alters syntax, update `knowledge/grammar.md` in the same pull request, and make sure every code example still follows the grammar.
4. If you add a file, give it frontmatter with `type`, `title`, and `description`, and add a line for it to the `index.md` of its folder. A file in `knowledge/language/` also takes a place in the tour: put its line where it belongs in the order, and update the "Next" link at the end of the file before it and of the new file.
5. Check that every link you added or touched points at a file and heading that exist.
6. Ship it with the `ship-change` skill.

## Rules

- Prefer fewer features. Add one only when real code needs it, and say in the pull request what that code is.
- When a feature is removed, delete its text. Do not leave a note saying it used to exist.
- Do not number headings, and do not refer to a section by position. Link to it by name.
- Examples use only features introduced above them in the same file, or in a file earlier in the tour that `knowledge/language/index.md` lists.
- A change to `knowledge/language/` or `knowledge/grammar.md` changes the language. It needs the owner's approval before it merges, so open the pull request and wait.
- Plans, open questions, and work to be done go in GitHub issues, not in `knowledge/`.
