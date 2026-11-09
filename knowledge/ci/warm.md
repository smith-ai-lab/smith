---
type: design
title: Testing a main-only step before it merges
description: How warm writes are tested as a dry run before a change merges.
tags: [ci, process]
---
# Testing a main-only step before it merges

**Testing a main-only step before it merges** (#2769). `warm` writes (the caches, the `step-cache` release, the body of #2751) and a push to `main` is the only place that ran it, so a broken step was found after merge (#2752's timing step, fixed in #2761 after about 90 minutes). Now `changes` compares the job with the base's (`yq '.jobs.warm'` of both `ci.yml`, plus a diff of `.github/actions/setup-smith`) and outputs `warm`/`warm_os`: a pull request that changes either runs `warm` as a **dry run** on Linux only (a push to `main` still runs both OSes for real; a pull request that leaves `warm` alone does not run it, and a queue entry never runs the dry run). The job sets `DRY_RUN=1` for any event but a push; each write step is skipped on it (`Save the step cache`, the artifact upload, `Save the lint and proof caches`) or ends early after printing what it would have written to the step summary (the release upload lists the assets and their sizes, the timing step prints the whole body of #2751). So a new write step goes behind `DRY_RUN` and prints its payload. The job keeps its `contents`, `actions` and `issues` permissions (a job's `permissions` cannot depend on the event; a fork's pull request gets a read-only token anyway), and the dry run writes nothing with them. It is not a required check: read its summary. It costs one Linux `warm` (about the same as a push's, dominated by the cold timing build) on a pull request that touches the job.
