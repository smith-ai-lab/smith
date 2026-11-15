---
name: ship-change
description: Take a change from a clean main to a pull request that is ready to merge, or merged, using a branch, conventional commits, a pull request, and a rebase merge once checks pass. Use for every change to this repository.
---

# Ship a change

Every change reaches `main` through a pull request. Nothing is pushed to `main` directly.

## Steps

1. Know which issue the change is for. Unless you were given one, take the first open sub-issue of the roadmap issue. Read its "Must be true" list: every line of it becomes a test. Then read `knowledge/index.md` and every file the issue lists under "Read first". `knowledge/` is not loaded for you, so nothing in it is known until you open it.

2. Start from the newest `main`.

   ```
   git fetch origin main
   git switch -c <type>/<short-name> origin/main
   ```

   The branch name uses the commit type and a few words, such as `feat/lexer` or `docs/loop-section`.

3. Make the change. Keep the pull request to one topic.

4. Run the checks before committing, from `bootstrap/`.

   ```
   cargo fmt --check
   cargo clippy --all-targets -- -D warnings
   cargo test
   ```

5. Commit with a Conventional Commits message.

   ```
   <type>(<scope>): <summary>
   ```

   - `type` is one of `feat`, `fix`, `docs`, `refactor`, `test`, `perf`, `build`, `ci`, `chore`, or `revert`.
   - `scope` is optional and names the part changed, such as `parser` or `knowledge`.
   - `summary` is in the imperative, starts lowercase, and has no full stop.
   - A breaking change adds `!` before the colon.

   Examples: `feat(lexer): tokenize numeric literals`, `docs(knowledge): describe trailing blocks`.

6. Push the branch and open a pull request. Its title follows the same format. Its body says what changed and why, and ends with `Closes #<issue>` so the roadmap updates when it merges.

   ```
   git push -u origin HEAD
   gh api repos/{owner}/{repo}/pulls -f title="<title>" -f head="<branch>" -f base=main -F body=@pr.md --jq '.number'
   ```

   Use `gh api` as shown. Cloud sessions cannot run `gh pr create`.

7. Wait for every check to pass.

   ```
   gh api repos/{owner}/{repo}/commits/<branch>/check-runs --jq '.check_runs[] | "\(.name) \(.status) \(.conclusion)"'
   ```

   Each commit lands on `main` as written, so fix a failure by folding the fix into the commit that caused it, then push again.

   ```
   git commit --fixup <commit>
   GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash origin/main
   git push --force-with-lease
   ```

8. If `main` has moved, rebase onto it and push again.

   ```
   git fetch origin main
   git rebase origin/main
   git push --force-with-lease
   ```

9. If a lead gave you the issue, stop here. End your turn with the pull request's number. The lead reviews it and merges it.

10. Otherwise, add the pull request to the merge queue once every check is green. The queue rebases it onto `main`.

    ```
    gh pr merge <number> --rebase
    ```

    In a cloud session that command is not available. Use this instead.

    ```
    gh api -X PUT repos/{owner}/{repo}/pulls/<number>/ccr/auto_merge -f merge_method=rebase
    ```

## Rules

- Never merge with a failing or pending check.
- Never use a merge commit or a squash merge. History on `main` stays linear.
- A pull request that touches `knowledge/language/` or `knowledge/grammar.md` waits for the owner's approval, even when checks are green.
- If the change alters what Smith is, update `knowledge/` in the same pull request, following the `update-knowledge` skill. Whoever makes the change writes it down.
- Do not edit the issue. If it is wrong or unclear, say so in a comment on it.
