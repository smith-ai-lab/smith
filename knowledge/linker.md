---
type: architecture
title: The linker is lld, in smith's process
description: The driver finds a hard link named ld that runs lld's Mach-O linker inside smith, and the system's other tools stay the system's.
tags: [linker, lld]
---
# The linker is lld, in smith's process

**The linker is lld, in smith's process.** The driver looks for `ld` in its own program's folder before `PATH`, so
that folder's `bin/ld` is a third hard link to smith; run as `ld`, smith calls `lld::macho::link` (`shim/lld.cpp`)
on the arguments the driver built, so nothing is added to the user's command and no system `ld` runs. lld signs
the output ad hoc on Apple Silicon, as `ld` does. The driver starts it as a process of its own (it has no hook to
link in its own process, as it has for `-cc1`): one more start of smith, which costs less than ld's own start (a hello-world link takes 33 ms against ld's 48). `SMITH_LINKER=/usr/bin/ld`
makes smith run that linker instead (a way out, and for comparison), and then the driver is left as Apple's `ld`
wants it. With lld the driver is told the linker's version (`-mlinker-version=1115.7.3`, which the user's own
flag overrides), so that it does not send messages to classes through `objc_msgSendClass$` stubs, which only
Apple's current `ld` writes; lld writes `objc_msgSend$` stubs per selector.

**The system's other tools stay the system's**, found on `PATH`: `dsymutil` (Apple's; its output is the same size
as LLVM's in `compare`), `libtool`, `xcrun`.
