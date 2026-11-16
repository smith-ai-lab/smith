---
name: lead-issue
description: Take roadmap issues from open to merged by starting a worker agent on each, reviewing its pull request, and merging. Use when acting as the lead for implementation work, not when writing the code yourself.
---

# Lead an issue

The lead does not write the code. It starts a worker on an issue, stays out of the way until a pull request is ready, reviews it, and merges it. The lead runs on Fable, a more capable model than any worker, so that its review is the stronger read.

The owner and the lead decide the design of Smith together, in conversation, and both agree before anything is written into `knowledge/` and before an issue is created. Everything else is the lead's to do: writing the issues they agreed, starting workers, reviewing pull requests, and merging them. The owner does not do those, and is not asked to.

## Steps

1. Take every open sub-issue of the roadmap whose "After" issues are all closed, in roadmap order. Read each, and read its `model:` label.

2. Start one worker for each: a cloud agent on this repository, on the newest model in the family the label names, at the default effort. Give it this brief and nothing more, with your own session's id in it.

   ```
   Do issue #<n> in <owner>/<repo>. Follow the ship-change skill.
   Stop when the pull request exists and every check passes, or when
   you are blocked and have asked in a comment on the issue. When you
   stop, send a message to session <lead session id> that says which,
   with the pull request's number.
   ```

   The issue is the brief. If the worker would need more than this, the issue is missing something: fix the issue.

3. Wait for the worker's message. Do not message it or check on it while it works. A message can fail to arrive, so also watch GitHub in the background for the two ways a worker can stop: a pull request for the issue whose checks have finished, or a comment on the issue.

4. If it stopped with a question, answer in a comment on the issue. Answer from `knowledge/`. If it does not answer the question, it is a design question: ask the owner, agree on the answer, write it into `knowledge/` with the `update-knowledge` skill, and then answer. Then tell the worker the answer is on the issue.

5. Review the pull request against the issue.

   - Every line of "Must be true" has a test that would fail if the line were false, and the tests follow the `write-tests` skill.
   - Nothing from "Out of scope" was built.
   - `knowledge/` changed if what Smith is changed.
   - Every check passes and every commit is meaningful.
   - Check out the branch, run the checks, and run the issue's example yourself. Read the whole diff.
   - Start from what the pull request says it could not verify, and verify it.

6. Post what you found as comments on the pull request, each on the line it is about where there is one. Each comment says what is wrong and what must be true instead. It does not say how to fix it. Post everything in one round. Then send the worker one message:

   ```
   Address the review comments on pull request #<number>. Stop when
   every check passes.
   ```

7. Repeat steps 5 and 6 until nothing is left. Then add the pull request to the merge queue, as the `ship-change` skill describes.

8. Tell the owner what merged, in a line or two, and anything the run showed that should change an issue or a skill.

## When a worker falls short

Work through these in order, and stop at the first that applies.

1. The issue was unclear or wrong. Fix the issue.
2. The worker lacked something it needed to read. Add it to `knowledge/` or to "Read first".
3. The worker had what it needed and tried. Raise the effort one level. On a `model:sonnet` issue do this once: beyond that, Sonnet costs about what Opus does, so move up instead.
4. It still fails. Move the label up a model and start a new worker. If the label is already `model:opus`, split the issue into smaller ones.

## Rules

- One worker per issue. Issues run at the same time only when neither is listed under the other's "After".
- When several issues that could run together would each have to move the same code into a shared place, start one, and start the others when it has merged. Otherwise each does that work alone and all but the first must redo it.
- Merge pull requests in roadmap order when several are ready. A worker whose branch falls behind `main` rebases it, as `ship-change` says.
- The lead owns the issues. It writes and corrects them with the `write-issue` skill. A worker is assigned an issue and never edits one.
- `knowledge/` is changed by whoever makes the change. A worker updates it in its own pull request when its work alters what Smith is. The lead updates it when a decision is made outside a worker's pull request, such as an answer to a worker's question.
- Never push to the worker's branch. Review in comments, and let the worker change the code.
- A pull request that touches `knowledge/language/` or `knowledge/grammar.md` changes the language. Merge it only when it writes down what the owner and the lead already agreed. A change to the language that nobody agreed is a design question for the owner, not something to merge or to settle in review.
- Never merge with a failing or pending check.
