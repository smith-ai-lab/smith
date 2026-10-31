---
name: smith-clock
description: "Using smith::clock (import smith::clock; clock::now, monotonic, sleep, set_handler): the time and sleeping called directly, and a fake clock where sleep advances time. Load when code imports smith::clock or includes <smith/clock.h>, or on timing, deadlines or sleeping in .smith, or testing code that waits."
---

# Time and sleeping: smith::clock

This skill ships in the smith plugin, from `.agents/skills/smith-clock/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

Durations and times are nanoseconds: `u64` for a duration and the monotonic clock, `i64` for the wall clock. `clock::second`, `clock::millisecond` and `clock::microsecond` are the units.

```smith
module timing;

import smith::clock;

export u64 timed_pause() {
    u64 start = clock::monotonic();
    clock::sleep(250 * clock::millisecond);
    return clock::monotonic() - start; // at least 250 ms
}

export i64 epoch_seconds() {
    return clock::now() / (i64)clock::second;
}
```

- **`monotonic()`** never goes back and means nothing but as a difference. **`now()`** is nanoseconds since the Unix epoch and can jump when the system sets it. **`sleep(ns)`** blocks for `ns` (a signal does not cut it short). None can fail: an OS error here is a bug, and a `sleep` past half of `u64` is a checked failure. The effect is `clock` (`smith-lang-effects`).
- **Test with a fake clock**: `clock::set_handler(&fake.base)` installs a `clock::Handler` (C: `SmithClockHandler`, `smith_handlers_set_clock`) for the thread and returns the previous one; restore it with `defer`. `now(self, which)` returns the nanoseconds of the clock `which` (CLOCK_REALTIME or CLOCK_MONOTONIC); `sleep(self, ns)` is where the fake advances its time and returns at once, so **sleeping on a fake never blocks and never fails** (no EDEADLK). `clock_gettime`, `nanosleep`, `usleep` and `sleep` of any other code on the thread reach it too; `gettimeofday`, `time` and the Mach clocks do not. See `lib/smith/clock_test.smith`.
- **From C**: `<smith/clock.h>` has `smith_clock_monotonic`, `smith_clock_now_epoch`, `smith_clock_sleep_ns` and `smith_clock_set_handler`.
