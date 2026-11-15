---
name: ship-change
description: Take a change from a clean main to merged, using a branch, conventional commits, a pull request, and a rebase merge once checks pass. Use for every change to this repository.
---

# Ship a change

Every change reaches `main` through a pull request. Nothing is pushed to `main` directly.

## Steps

1. Start from the newest `main`.

   ```
   git fetch origin main
   git switch -c <type>/<short-name> origin/main
   ```

   The branch name uses the commit type and a few words, such as `feat/lexer` or `docs/loop-section`.

2. Make the change. Keep the pull request to one topic.

3. Run the checks before committing, from `bootstrap/`.

   ```
   cargo fmt --check
   cargo clippy --all-targets -- -D warnings
   cargo test
   ```

4. Commit with a Conventional Commits message.

   ```
   <type>(<scope>): <summary>
   ```

   - `type` is one of `feat`, `fix`, `docs`, `refactor`, `test`, `perf`, `build`, `ci`, `chore`, or `revert`.
   - `scope` is optional and names the part changed, such as `parser` or `knowledge`.
   - `summary` is in the imperative, starts lowercase, and has no full stop.
   - A breaking change adds `!` before the colon.

   Examples: `feat(lexer): tokenize numeric literals`, `docs(knowledge): describe trailing blocks`.

5. Push the branch and open a pull request. Its title follows the same format. Its body says what changed and why, and links the issue it closes.

6. Wait for every check to pass. Each commit lands on `main` as written, so fix a failure by folding the fix into the commit that caused it, then push again.

   ```
   git commit --fixup <commit>
   GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash origin/main
   git push --force-with-lease
   ```

7. If `main` has moved, rebase onto it and push again.

   ```
   git fetch origin main
   git rebase origin/main
   git push --force-with-lease
   ```

8. Merge by rebase once every check is green.

   ```
   gh pr merge --rebase --delete-branch
   ```

## Rules

- Never merge with a failing or pending check.
- Never use a merge commit or a squash merge. History on `main` stays linear.
- A pull request that touches `knowledge/language.md` or `knowledge/grammar.md` waits for the owner's approval, even when checks are green.
- If the change alters what Smith is, update `knowledge/` in the same pull request, following the `update-knowledge` skill.
