---
name: smith-dev-tickets
description: "Writing smith's GitHub tickets: the body as the only truth, the sections a body has, the one label. Load before filing, rewriting or closing a ticket."
---

# smith-dev-tickets

The reader has no context: not the conversation, not the owner's words. The body is the whole brief, and the only truth: a decision or a result is an edit to the body, never a comment.

A ticket is one piece of work for one worker in one pull request, a sub-issue of #1631 (queue-heal adds it last when you forget; move it to its rank). Label it `difficulty:easy` only when the change is spelled out line by line; otherwise no label.

## Sections
1. **Why**: the problem and what it costs.
2. **Design**: every decision stated, with the owner's words quoted where they matter.
3. **Files**: where the change goes and what it must not touch.
4. **Prediction**: the number this moves and by how much. After the merge: the measured value and, on a miss, why.
5. **Done when**: the commands that show it and what they print.
