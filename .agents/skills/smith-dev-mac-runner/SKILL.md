---
name: smith-dev-mac-runner
description: Load when you set up or supervise the runner loop on the owner's Mac: runners on the owner's Anthropic API credits (key from the login Keychain through apiKeyHelper on the command line), the SMITH_RUNNER_LOOP bash loop, the mailbox #1406, and the Mac's rules (no pkill, no smith build install, own worktree).
---

# smith-dev-mac-runner

Why: the Mac runs on the owner's Anthropic API credits, so it keeps working when the subscription's daily limit pauses the cloud runners. The owner's desktop session stays on the subscription. Keep that session's own token use small: the work happens in `claude -p` processes. The Mac runner counts as one of the N in the pool. The runner's rules are in [smith-dev-runner](../smith-dev-runner/SKILL.md); the pool's in [smith-dev-factory](../smith-dev-factory/SKILL.md). It takes `mac` tickets first, then the normal backlog.

## The API key
- It is in the owner's login Keychain: service `anthropic-api-smith`, account `$USER`. The owner stores it once, in their own terminal, which prompts for the value so it never reaches a transcript:
  `security add-generic-password -a "$USER" -s anthropic-api-smith -w`
- Pass the helper to runner processes on the command line only, so no settings file exists and nothing needs ignoring. The desktop session is unaffected:
  `claude -p ... --settings '{"apiKeyHelper":"security find-generic-password -a $USER -s anthropic-api-smith -w"}'`
  `--settings` takes a file or a JSON string; confirm with `claude --help`.
- Never run `security find-generic-password -w` without `>/dev/null`, and never `echo $ANTHROPIC_API_KEY`, where the output lands in a transcript. Never put the key in a comment, a log, a commit or a file.
- **The init line proves nothing:** `apiKeySource: apiKeyHelper` shows even when `claude` sends the desktop app's claude.ai login, which bills the subscription (the Mac loops did for 6 hours). The app puts its login in the environment of its children; the loop unsets every `CLAUDE*` and `ANTHROPIC*` variable first. Verify before a loop starts: `claude -p "say ok" --model <the easy model from providers/anthropic.md> --settings '...' 2>&1 | grep -c 'takes precedence over your claude.ai login'` must print 1 (0 means the login is in use), then check on the Anthropic console that the API key was billed.
- If a Keychain prompt asks to allow access, the item was not stored through `security`: ask the owner. Never work around it. To rotate the key, the owner runs the same command with `-U`.

## The runner loop
A plain bash loop costs no tokens. It starts a runner, waits for it, and starts the next. A crash costs one ticket, not the chain. `SMITH_RUNNER_LOOP=1` tells the runner to skip its "start a successor" step.

```bash
for v in $(env | cut -d= -f1 | grep -E '^(CLAUDE|ANTHROPIC)'); do unset "$v"; done   # else the app's login is sent, not the key
R=smith-ai-lab/smith
SETTINGS='{"apiKeyHelper":"security find-generic-password -a $USER -s anthropic-api-smith -w"}'
MODEL=<the medium model in providers/anthropic.md>; LOOP=<this worktree's name, such as mac-runner-2>
L="$HOME/smith-runner-logs"; mkdir -p "$L"
while :; do
  top=$(gh api repos/$R/issues/73 --jq '.body|split("\n")[0]' 2>/dev/null)
  if [[ $top == *"pool: running"* ]]; then
    git fetch -q origin main && [ -z "$(git status --porcelain -uno)" ] && git switch -q --detach origin/main && git clean -fdq || { sleep 600; continue; }
    f="$L/$(date +%Y%m%dT%H%M%S)-$LOOP"; start=$(date +%s)
    SMITH_RUNNER_LOOP=1 claude -p "You are a smith runner on the owner's Mac. Load the skill smith-dev-runner and follow it. Take mac tickets first." \
      --model "$MODEL" --permission-mode auto --settings "$SETTINGS" --output-format json > "$f.json" 2> "$f.err"
    jq -r --arg l "$LOOP" '[(now|todate),$l,.duration_ms/1000,.total_cost_usd,.num_turns,.is_error]|@tsv' "$f.json" >> "$L/cost.tsv" 2>/dev/null
    (( $(date +%s) - start < 120 )) && sleep 600   # no ticket, no key or a crash: don't spin
  else
    sleep 300
  fi
done
```
- **Model:** the runner knows its ticket only after it claims it, so the loop starts it on the medium model from [providers/anthropic.md](../smith-dev-factory/providers/anthropic.md). The runner then hands the work to subagents on the model its ticket's difficulty maps to. Each loop runs in its own worktree of main and starts every run from a clean tree at the latest `origin/main` (it waits instead if tracked changes are left, and the runner never reuses the last runner's branch); #73's N counts the Mac's loops. `cost.tsv` holds a line per run (time, loop, seconds, `total_cost_usd`, turns, error): `column -t ~/smith-runner-logs/cost.tsv`.
- **Paused:** a run under 2 minutes (no ticket, no key, a crash) waits 10 minutes, so it never spins. When #73 says `pool: paused`, it sleeps 5 minutes and checks again. A running runner finishes its ticket first.

## The mailbox #1406
For rare cases only (a Keychain prompt, a question): comments on #1406 starting `@mac:` or `@lead:`. A zero-token watcher wakes you on a new `@mac:` comment; restart it after each wake and answer with an `@lead:` comment:

```bash
c() { gh api 'repos/smith-ai-lab/smith/issues/1406/comments?per_page=100' --jq "$1" 2>/dev/null; }
seen=$(c length)
while :; do sleep 60; n=$(c length) || continue; [ -z "$n" ] && continue
  [ "$(c "[.[$seen:][]|select(.body|startswith(\"@mac:\"))]|length")" -gt 0 ] && exit 0; seen=$n; done
```

## Rules on the Mac
- **Never `pkill` or `killall`** (a reparented pid may be another test's process; kill only a pid you started), and **never `smith build install` or `smith build llvm`**. CI's `install` job puts each release in `~/.local/bin`; a hand install breaks the owner's tools.
- **Your own worktree only.** Build and test a smith under test there, never in the `~/Documents` test beds in place: they are the owner's projects, and two builds in one `.smith/` collide.
- **Never push to main.** Every change is a pull request through the merge queue; `ci` must pass. Every PR ends with `## Friction` (bullets ending `(~N min)`). No model names in commits or PRs. No secrets in a comment, a log or a commit.
- **Worktree hygiene:** remove only worktrees that are clean and whose branches are merged. List any with uncommitted or unmerged work, and keep them.
