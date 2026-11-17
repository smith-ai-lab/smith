---
name: update-knowledge
description: Add to or change the files in knowledge/, which define the Smith language, its grammar, and the compiler's design, and which are never loaded automatically. Use whenever your change adds or alters a language feature, syntax, a command, or how the compiler works, whenever a decision about Smith is made, and whenever knowledge/ turns out to be silent or wrong about something the work depends on.
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
- Write only what is true now. When a feature is removed or a rule changes, delete or rewrite the text so that a reader sees the present state and nothing else. Do not leave a note of what it used to be, or of why it was decided.
- Keep files short. Before adding a sentence, look for one to remove.
- One concept to a file in `knowledge/language/`, taught from the simplest to the hardest, as `knowledge/okf.md` says. When what you add is a concept of its own, it is a file of its own. When it is a harder part of a concept, it goes after the easier parts, with an example of its own.
- Do not number headings, and do not refer to a section by position. Link to it by name.
- Examples use only features introduced above them in the same file, or in a file earlier in the tour that `knowledge/language/index.md` lists.
- A change to `knowledge/language/` or `knowledge/grammar.md` changes the language. Only the lead merges it, so open the pull request and stop.
- A function added to the standard library is written into `knowledge/library/`, in the file for its module, by whoever adds it. A new effect is a design question for the owner and the lead, because it defines a permission.
- When your change makes the compiler build something a page of `knowledge/language/` describes, update that page's `status` and `built`, as `knowledge/okf.md` describes them.
- Plans, open questions, and work to be done go in GitHub issues, not in `knowledge/`.
