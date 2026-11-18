---
name: write-issue
description: Write a GitHub issue that an implementing agent can finish from the issue and the repository alone. Use when planning work, splitting a task, or adding to the roadmap.
---

# Write an issue

An issue is read by an agent that starts cold, with only the repository and the issue text. Write as a manager would: say what must be true when the work is done, and leave how to the implementer.

## Where issues live

The owner and the lead agree on an issue before it is created, unless it only builds what `knowledge/` already decides. The lead then writes it. A closed issue is removed from the roadmap.

One parent issue is the roadmap. Each task is a sub-issue of it, and the order of the sub-issues is the order of the work. An implementer takes the first open one.

## Before writing

- If the task depends on a large or contested decision that will outlive it, such as a boundary between stages, write that decision into `knowledge/` first, with the `update-knowledge` skill. The issue links to it. An issue is closed and forgotten, and `knowledge/` is not.
- A small, settled decision, such as the name of a flag, can be stated in the issue. Add a line to "Must be true" saying that `knowledge/` describes it, so the pull request records it.
- An issue is a vertical slice: one feature of the language, working end to end from source text to a running executable. Do not write an issue for a whole stage, such as the full parser.
- One issue is one pull request. If the work will not fit in one session, split it into several issues.

## Format

```
<Title: the outcome, as an instruction>

Goal
One or two sentences on what exists when this is done.

Must be true
- One or two concrete examples: a program or command and its exact result.
- Properties, each starting with "For every".

Touches
The files or folders of bootstrap/src and knowledge/ the work is expected to change, one to a line.

Out of scope
What not to build.

Ask first
Choices the implementer must not make alone. Leave this out when there are none.

After
Issues that must be merged before this one starts, because it builds on them, changes the same code, or would move the same code into a shared place. Leave this out when there are none.

Read first
The sections of knowledge/ that apply, as links.
```

The first three parts are the goal, the finished result, and where it lands. "Ask first" says when to stop. "Touches" and "After" tell the lead what can run at the same time, as the `lead-issue` skill says. "Read first" points at the sources, so nothing is pasted into the issue that the repository already holds.

## Rules

- "Must be true" is both the goal and the test plan. Every line in it becomes a test, and a property becomes a property test.
- Make each property exact enough that the test is nearly dictated. "For every string literal s with no braces and no escapes, the program that prints s outputs exactly s and a newline" can be checked. "Printing works" cannot.
- Prefer a property to a list of examples. Keep one or two examples, to pin down the meaning.
- Do not write steps, and do not assign an approach. Describing how is writing the code in prose.
- Do not add lines such as "think carefully" or "be thorough". They change nothing.
- Name what is out of scope, so the implementer does not build the next issue too.
- Ask for nothing that exists only because of what Smith used to be. Smith has no users and no code to keep working, so when something is removed, the compiler forgets it: no special error, no hint towards the new form, no test that it is gone.
- Keep the whole issue to one screen.

## Sizing it

An issue is small. Many small issues cost less than a few large ones, and each is easier to review. If an issue feels large, split it before anyone starts.

Most issues carry no label. Label an issue `trivial` only when both of these hold.

- The issue nearly dictates the change, as marking pages, renaming, or correcting a sentence does.
- A mistake in it would fail a check, and could not pass unseen. A script that moves files or talks to another service is never trivial, however short.

A label names the size of the task and never a model. The `lead-issue` skill says which model each size runs on.

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
