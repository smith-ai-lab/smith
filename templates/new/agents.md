# @NAME@

One home per fact. This file holds the rules and decisions, one line each. [knowledge/](knowledge/index.md) holds what
@NAME@ is, and the skills in [.agents/skills/](.agents/skills/@KEBAB@-docs/SKILL.md) hold how to do a task. Plans go in
the project's issues. A line stays only if an agent needs it and the code can't say it quickly.

Start every change with the [@KEBAB@-docs](.agents/skills/@KEBAB@-docs/SKILL.md) skill, and run `smith check` before
you commit: it checks the formatting, the lint and these documents (`smith docs`).
