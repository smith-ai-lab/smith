---
name: smith-io-fake
description: "The fake io for tests: io::Test, io::make_test, io::test_add_file, io::test_output and scripted servers, so a test touches no real file, console or network. Load when testing code that takes an io capability."
---

# The test io

A test that takes `io::Io *io` (`test_x(io::Io *io)`, smith-test) gets a real io the runner made for it, seeded from the run's seed and with a scratch folder of its own (`test::scratch(io)`, also `$TMPDIR`), removed when the test passes. A test never makes a real io itself (`io::make` is for `main`, and smith-effects reports it anywhere else); to test code that takes a capability on a fake, make one:

```smith
import std::arena;
import std::io;

I32 main() {
    alignas(arena::align_max) static U8 memory[1 << 20];
    io::Io io = io::make_test((io::Test){
        .memory = memory, .capacity = sizeof memory, .seed = 7, .input = "A3 B4\nC5 D6\n"});
    (void)io::test_add_file(&io, "config.txt", "volume=3\n");
    (void)io::test_add_program(&io, "git", "main\n", 0);
    return 0;
}
```

It is the same `io::Io`, so code under test cannot tell it from the real one: files are in memory (writes land there too), the console reads the scripted `.input` then reports the end, output is captured (`io::test_output(&io)`, `io::test_errors(&io)`), the clock moves only when `io::clock_advance(&io.clock, ns)` says so (and `io::clock_sleep` of a non-zero time is `EDEADLK`, since nothing can wake it), the key comes from `.seed`, programs are scripted, and its memory is your buffer (a quarter holds file contents). `io::test_add_server(&io, (io::NetServer){.host, .port, .response, .response_size, ...})` scripts a server of the network: the first connection to `host:port` that no connection has used gets the response (`.chunk` bytes at a time if set), then a clean end or, with `.error`, that errno; `.tls_failure` makes the connect fail with `EAUTH` and that message; a host with no server is `ECONNREFUSED`; `io::test_request(&io, host, port, n)` is what the n-th connection sent. Adding a server to the real io scripts its network only (files, console and clock stay real), which is how code that downloads is tested on a real disk. `io::Test.environment` (`"NAME=value"` entries) and `.cores` give it an environment and a core count; folders exist as soon as a file is under them, or `io::files_make_folder` made them (a write into a folder that was never made still succeeds, where the real io says `ENOENT`). `io::run` on it never waits: it completes what is ready now. Errors have the same codes and messages as the real io.

Write the function once against capabilities, call it with `&io.files` from main and with the fake's in a test, and compare. The scripted console drives a line loop to completion including the end of input, which is the bug a program that spins on EOF has.

Also in this family: [smith-io](../smith-io/SKILL.md), [smith-io-capabilities](../smith-io-capabilities/SKILL.md), [smith-effects](../smith-effects/SKILL.md), [smith-io-errors](../smith-io-errors/SKILL.md), [smith-io-files](../smith-io-files/SKILL.md), [smith-io-processes](../smith-io-processes/SKILL.md), [smith-io-async](../smith-io-async/SKILL.md), [smith-io-net](../smith-io-net/SKILL.md), [smith-io-c](../smith-io-c/SKILL.md).
