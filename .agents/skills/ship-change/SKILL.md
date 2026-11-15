---
name: ship-change
description: Take a change from a clean main to a pull request that is ready to merge, or merged, using a branch, conventional commits, a pull request, and a rebase merge once checks pass. Use for every change to this repository.
---

# Ship a change

Every change reaches `main` through a pull request. Nothing is pushed to `main` directly. This skill says what must be true of the change at each point. How you get there is yours to choose.

## Before you start

- You know which issue the change is for. Unless you were given one, take the first open sub-issue of the roadmap issue.
- You have read its "Must be true" list. Every line of it becomes a test.
- You have read `knowledge/index.md` and every file the issue lists under "Read first". `knowledge/` is not loaded for you, so nothing in it is known until you open it.

## The branch and its commits

- The branch starts from the newest `main`. It is named `<type>/<short-name>`, such as `feat/lexer` or `docs/loop-section`, unless your session was given a branch to use.
- The pull request covers one topic.
- Every commit follows Conventional Commits: `<type>(<scope>): <summary>`, with the summary in the imperative, starting lowercase, and with no full stop. `.github/check-commits.sh` holds the exact rule and the list of types.
- Each commit lands on `main` as written, so every commit is meaningful on its own. A fix for a failed check is folded into the commit that caused it, not added on top.
- The branch is rebased onto `main` when `main` moves. `main` is never merged into it.

## The pull request

- The checks in `.github/workflows/ci.yml` pass on your machine before you push, and every check passes on the pull request before you stop.
- The title follows the commit format.
- The body says what changed and why, names anything you could not verify, and ends with `Closes #<issue>` so the roadmap updates when it merges.

## Finishing

- If a lead gave you the issue, stop once the pull request exists and every check passes. End your turn with the pull request's number. The lead reviews it and merges it.
- Otherwise, add the pull request to the merge queue with a rebase merge once every check is green.

## What you could not guess

A cloud session may refuse `gh pr create` and `gh pr merge`. These work in their place.

```
gh api repos/{owner}/{repo}/pulls -f title="<title>" -f head="<branch>" -f base=main -F body=@pr.md --jq '.number'
gh api -X PUT repos/{owner}/{repo}/pulls/<number>/ccr/auto_merge -f merge_method=rebase
```

## Rules

- Never merge with a failing or pending check.
- Never use a merge commit or a squash merge. History on `main` stays linear.
- A pull request that touches `knowledge/language/` or `knowledge/grammar.md` waits for the owner's approval, even when checks are green.
- If the change alters what Smith is, update `knowledge/` in the same pull request, following the `update-knowledge` skill. Whoever makes the change writes it down.
- Do not edit the issue. If it is wrong or unclear, say so in a comment on it.
