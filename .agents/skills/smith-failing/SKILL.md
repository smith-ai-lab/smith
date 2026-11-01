---
name: smith-failing
description: "Using std::failing (import std::failing; failing::make, failing::Failing): an allocator for tests that refuses some allocations by a draw from a random::Key, so a test reaches every allocation-failure path and a failure replays from its seed. Load when code imports std::failing or includes <smith/failing.h>, or on testing what a function does when memory is refused."
---

# Refusing on purpose: std::failing

`failing::make(parent, key)` (C: `smith_failing_make`, in `<smith/failing.h>`; `parent` null is the system's) is a `failing::Failing` (`SmithFailing`), an allocator of [smith-mem](../smith-mem/SKILL.md)'s interface that wraps `parent` and refuses one allocation in `f.one_in` (8 at first; 1 every one, 0 none) by a draw from `key`. The same key and the same calls refuse the same allocations, so a failure replays from its seed. It is a test tool built on `random`, not part of the allocator interface: `mem` depends on nothing above it.

```smith
module scratch_test;
import std::failing;
import std::mem;
import std::random;
import std::test;

export void test_every_allocation_refused() {
    failing::Failing f = failing::make(nullptr, random::key_make(1));
    f.one_in = 1;                                           // refuse each one
    mem::Allocator *before = mem::set_allocator(&f.base);
    defer mem::set_allocator(before);
    test::check_condition(mem::try_alloc_raw(64, 16) == nullptr); // try_alloc answers .none
    test::check_equal(f.refused, 1);
}
```

- A refusal is a null from the allocator: `mem::try_alloc` and `try_alloc_raw` answer `.none` or null; `mem::alloc` stops with a checked failure naming the size (with tasks, #324, it fails the task). Free passes to the parent.
- `f.calls` and `f.refused` count the allocations asked and refused; set the allocator with `mem::set_allocator(&f.base)` and restore it with `defer`; the value must not move while set.
- A test gets one from the runner: `void test_x(random::Key key, failing::Failing *failing)` is a property test whose allocator refuses by the key's draws, over the checked allocator, and `--seed` replays it ([smith-test](../smith-test/SKILL.md)). Details: knowledge/checked-allocator.md.
- It was `mem::failing` and `smith_mem_failing` before issue #500.
