---
name: lead-issue
description: Take roadmap issues from open to merged by starting a worker agent on each, reviewing its pull request, and merging. Use when acting as the lead for implementation work, not when writing the code yourself.
---

# Lead an issue

The lead does not write the code. It starts a worker on an issue, stays out of the way until a pull request is ready, reviews it, and merges it. The lead runs on a more capable model than any worker, so that its review is the stronger read.

The owner and the lead decide the design of Smith together, in conversation, and both agree before anything is written into `knowledge/`. An issue that only builds what `knowledge/` already decides needs no further agreement: the lead writes it and starts a worker, so that building goes on while the design is discussed. Any other issue is agreed first. Everything else is the lead's to do: writing the issues they agreed, starting workers, reviewing pull requests, and merging them. The owner does not do those, and is not asked to.

## Starting as a new lead

A lead's session ends and another begins, so nothing the work depends on lives only in a conversation. Read these before anything else.

- `knowledge/index.md` and the files it leads to say what Smith is. Each page of `knowledge/language/` says in its `status` how much of it the compiler builds.
- The roadmap is the issue titled "Roadmap". Its open sub-issues are the work, in order.
- The issue titled "Open design questions" lists what the owner and the lead have not yet agreed, with the lead's recommendation for each. Keep it current: add a question when one appears, and remove it when its answer is in `knowledge/`.
- The skills in `.agents/skills/` say how the work is done.

## Steps

1. Take the first open sub-issue of the roadmap whose "After" issues are all closed. Read it, and see whether it is labelled `trivial`.

2. Start one worker for it: a cloud agent on this repository, on the model [the table below](#models) gives for its size, at the default effort. Give it this brief and nothing more, with your own session's id in it.

   ```
   Do issue #<n> in <owner>/<repo>. Follow the ship-change skill.
   Stop when the pull request exists and every check passes, or when
   you are blocked and have asked in a comment on the issue. When you
   stop, send a message to session <lead session id> that says which,
   with the pull request's number.
   ```

   The issue is the brief. If the worker would need more than this, the issue is missing something: fix the issue.

3. Wait for the worker's message, and for nothing else. Do not message it or check on it while it works, do not subscribe to its pull request, and do not schedule a check. Each of those wakes the lead, and a wake costs the whole conversation again. If the owner writes and no message has come, look at GitHub once then.

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

7. Repeat steps 5 and 6 until nothing is left. Then add the pull request to the merge queue, as the `ship-change` skill describes. Ask for the merge only when every check has passed, and do not watch it. The queue merges it.

8. Start the next worker. Before that, confirm the pull request merged, remove its closed issue from the roadmap's sub-issues, so that the roadmap lists only what is left, and archive the worker's session, so that finished sessions do not pile up. Tell the owner what merged, in a line or two, and anything the run showed that should change an issue or a skill.

## Models

An issue's label names the size of the task, and this table names the model for each size. It is the one place a model is named. Someone working with another provider adds a column for it and changes nothing else.

| Label | Anthropic |
| --- | --- |
| `trivial` | The newest Haiku |
| None | The newest Sonnet |

## When a worker falls short

Work through these in order, and stop at the first that applies.

1. The issue was unclear or wrong. Fix the issue.
2. The worker lacked something it needed to read. Add it to `knowledge/` or to "Read first".
3. The issue was too large. Split it into smaller issues, and start a new worker on the first.

## Rules

- One worker runs at a time, unless the owner asks for more. Then issues run together only when neither is listed under the other's "After", and never when each would have to move the same code into a shared place.
- A second review round means the issue left something out. Before starting the next worker, put what it left out into `write-issue` or the next issue, so that one round is the usual number.
- The lead owns the issues. It writes and corrects them with the `write-issue` skill. A worker is assigned an issue and never edits one.
- `knowledge/` is changed by whoever makes the change. A worker updates it in its own pull request when its work alters what Smith is. The lead updates it when a decision is made outside a worker's pull request, such as an answer to a worker's question.
- Never push to the worker's branch. Review in comments, and let the worker change the code.
- A pull request that touches `knowledge/language/` or `knowledge/grammar.md` changes the language. Merge it only when it writes down what the owner and the lead already agreed. A change to the language that nobody agreed is a design question for the owner, not something to merge or to settle in review.
- Never merge with a failing or pending check.
