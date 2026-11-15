---
name: write-issue
description: Write a GitHub issue that an implementing agent can finish from the issue and the repository alone. Use when planning work, splitting a task, or adding to the roadmap.
---

# Write an issue

An issue is read by an agent that starts cold, with only the repository and the issue text. Write as a manager would: say what must be true when the work is done, and leave how to the implementer.

## Where issues live

One parent issue is the roadmap. Each task is a sub-issue of it, and the order of the sub-issues is the order of the work. An implementer takes the first open one.

## Before writing

- If the task depends on a decision that will outlive it, such as a file layout or a boundary between stages, write that decision into `knowledge/` first, with the `update-knowledge` skill. The issue links to it. An issue is closed and forgotten, and `knowledge/` is not.
- One issue is one pull request. If the work will not fit in one session, split it into several issues.

## Format

```
<Title: the outcome, as an instruction>

Goal
One or two sentences on what exists when this is done.

Must be true
- One or two concrete examples: a program or command and its exact result.
- Properties, each starting with "For every".

Out of scope
What not to build.

Read first
The sections of knowledge/ that apply, as links.
```

## Rules

- "Must be true" is both the goal and the test plan. Every line in it becomes a test, and a property becomes a property test.
- Make each property exact enough that the test is nearly dictated. "For every string literal s, the program that prints s outputs exactly s and a newline" can be checked. "Printing works" cannot.
- Prefer a property to a list of examples. Keep one or two examples, to pin down the meaning.
- Do not write steps. Describing how is writing the code in prose.
- Name what is out of scope, so the implementer does not build the next issue too.
- Keep the whole issue to one screen.

## Creating it

Create the issue, then attach it to the roadmap as a sub-issue. The sub-issue call takes the new issue's `id`, which is not its number, and it must be sent as a number.

```
gh api repos/{owner}/{repo}/issues -f title="<title>" -F body=@issue.md --jq '.id'
echo '{"sub_issue_id": <id>}' | gh api repos/{owner}/{repo}/issues/<roadmap>/sub_issues -X POST --input -
```

To move it within the roadmap, name the sub-issue it should follow.

```
echo '{"sub_issue_id": <id>, "after_id": <other id>}' | gh api repos/{owner}/{repo}/issues/<roadmap>/sub_issues/priority -X PATCH --input -
```
