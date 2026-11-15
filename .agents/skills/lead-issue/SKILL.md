---
name: lead-issue
description: Take one roadmap issue from open to merged by starting a worker agent on it, reviewing its pull request, and merging. Use when acting as the lead for implementation work, not when writing the code yourself.
---

# Lead an issue

The lead does not write the code. It starts one worker on one issue, stays out of the way until a pull request is ready, reviews it, and merges it.

## Steps

1. Take the first open sub-issue of the roadmap. Read it, and read its `model:` label.

2. Start one worker: a cloud agent on this repository, on the model the label names, at the default effort. Give it this brief and nothing more.

   ```
   Do issue #<n> in <owner>/<repo>. Follow the ship-change skill.
   Stop when the pull request exists and every check passes, and
   reply with its number. If you are blocked, ask in a comment on
   the issue and stop.
   ```

   The issue is the brief. If the worker would need more than this, the issue is missing something: fix the issue.

3. Wait for the worker's turn to end. Do not message it or check on it while it works.

4. If it stopped with a question, answer in a comment on the issue. Answer from `knowledge/` or from a decision the owner has made. If neither answers it, ask the owner. Then tell the worker the answer is on the issue.

5. Review the pull request against the issue.

   - Every line of "Must be true" has a test that would fail if the line were false.
   - Nothing from "Out of scope" was built.
   - `knowledge/` changed if what Smith is changed.
   - Every check passes and every commit is meaningful.
   - Check out the branch, run the checks, and run the issue's example yourself. Read the whole diff.

6. Post what you found as comments on the pull request. Each comment says what is wrong and what must be true instead. It does not say how to fix it.

   ```
   gh api repos/{owner}/{repo}/issues/<number>/comments -F body=@comment.md
   gh api repos/{owner}/{repo}/pulls/<number>/comments -F body=@comment.md -f commit_id=<sha> -f path=<file> -F line=<line> -f side=RIGHT
   ```

   The first form is for the pull request as a whole, the second for one line. Post everything in one round. Then send the worker one message:

   ```
   Address the review comments on pull request #<number>. Stop when
   every check passes.
   ```

7. Repeat steps 5 and 6 until nothing is left. Then add the pull request to the merge queue, as the last step of the `ship-change` skill shows.

8. Tell the owner what merged, in a line or two, and anything the run showed that should change an issue or a skill.

## When a worker falls short

Work through these in order, and stop at the first that applies.

1. The issue was unclear or wrong. Fix the issue.
2. The worker lacked something it needed to read. Add it to `knowledge/` or to "Read first".
3. The worker had what it needed and tried. Raise the effort.
4. It still fails. Move the label up a model and start a new worker.

## Rules

- One worker per issue, and one issue at a time.
- Never push to the worker's branch. Review in comments, and let the worker change the code.
- A pull request that touches `knowledge/language.md` or `knowledge/grammar.md` waits for the owner's approval.
- Never merge with a failing or pending check.
