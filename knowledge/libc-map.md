---
type: convention
title: The libc map, from every libc function to smith's way
description: Every libc and POSIX function that .smith code calls with c:: on main, grouped by header, with smith's module and function for it or a gap and its issue; the table the check that makes libc a compile error in a module reads its messages from.
tags: [libc, lint, idiom, libraries]
---
# The libc map, from every libc function to smith's way

**Owner (2026-10-31):** "it seems like we already have almost everything that C has. So it seems like we really should just have everything be done with ours and then make calling the standard C library a compile error inside of Smith code with the error message showing you the Smith way of doing things." And: "our standard library could still delegate to the underlying C library. And because it's doing the effects at the C library level, anyone who calls our library or some third party C code, we can swap out the side effects everywhere. But you don't see c:: throughout."

**This file is the table the check reads its messages from** (issue #915, part of #416): one row per libc function, the smith function that replaces it, or **gap** with the issue that adds one. The check reports a call of the left column in a module and prints the right column, as `smith-mem` does for `memcpy` ([mem-typed-functions](mem-typed-functions.md)). A row's equivalent is a module and a function a program imports; the library underneath still calls libc, under `contract::trusted()`, so a handler swaps the effect for every caller, `.smith` or C.

**How the rows were found:** `git grep -o 'c::[a-z_0-9]*(' -- '*.smith'` on main (2026-10-31); the test beds (`~/Documents/checkers`, `~/Documents/virtual-cell/learner`, `~/Documents/game`) were not readable from the lane that wrote this, so a function only they call is added by the first lane that reads them (a row, and an issue when it is a gap). Calls of smith's own C API (`c::smith_*`, `c::Smith*`) are not libc and are not here: they go when the library behind them is a module.

**Where a row says** *gap*, the issue has the proposed API; *gap (design)* is [#924](https://github.com/smith-ai-lab/smith/issues/924), which waits for the lead. *platform* means the call is the inside of a smith library's implementation, in `runtime/` or under `contract::trusted()` in a library module, and never appears in a program.

## stdio.h

| libc | smith |
|---|---|
| `printf`, `puts`, `putchar`, `fputs(s, stdout)`, `vprintf` | `console::print(pieces...)`: typed pieces, no format string ([direct-call-modules](direct-call-modules.md)) |
| `fprintf(stderr, ...)`, `fputs(s, stderr)`, `dprintf`, `perror` | `console::print_error(pieces...)` |
| `snprintf` | `format::to(buffer, capacity, pieces...)` ([format-library](format-library.md)); `format::hex`, `fixed`, `general`, `scientific`, `pad` for the conversions |
| `fopen`, `fclose`, `FILE` | `files::open(path, mode)` returning `Result(File, files::Error)`, `files::close` ([files](files.md)) |
| `fgets`, `getline` | `files::read_line(file, buffer)` returning `Result(Option(Usize), files::Error)`: the line's length without its newline, `none` at the end, `LineTooLong` when it does not fit ([files](files.md)) |
| `fflush`, `setvbuf`, `_IONBF`, `_IOFBF` | not needed: the console writes each call unbuffered |
| `stdout`, `stderr`, `stdin`, `EOF` | `console::write`, `console::write_error`, `console::read` (a count of 0 is the end) and `console::read_line` |

## stdlib.h

| libc | smith |
|---|---|
| `malloc`, `calloc`, `aligned_alloc`, `posix_memalign`, `free` | `mem::alloc(T)`, `mem::alloc_slice(T, n)`, `mem::free(p)` or `arena::push`; `smith-mem` already reports them ([mem-typed-functions](mem-typed-functions.md)) |
| `realloc` | typed `mem::realloc`, with #239 |
| `strtol`, `strtoul`, `strtoull` | `text::parse_i64`, `parse_u64`, `parse_hex` (a base 10 or 16 word, `bool` and the value) |
| `strtod`, `strtof` | `text::parse_f64`, `text::parse_f32` |
| `qsort` | `slice::sort(xs)` for numbers, `slice::sort_by(xs, less)` for the rest |
| `getenv` | `env::get(name)`, an `option` ([direct-call-modules](direct-call-modules.md)) |
| `setenv`, `unsetenv` | gap: [#917](https://github.com/smith-ai-lab/smith/issues/917) |
| `system` | `process::run(argv)` ([process](process.md)): no shell, the argument vector |
| `abs` | `math::abs(x)` |
| `exit`, `_exit`, `abort` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924) |
| `realpath` | gap: [#919](https://github.com/smith-ai-lab/smith/issues/919) |

## string.h

| libc | smith |
|---|---|
| `strlen` | `text::make(string)`, a slice that knows its length |
| `strcmp` | `text::is(word, string)` against a C string, `slice::equal(a, b)` and `slice::compare(a, b)` between slices |
| `strncmp` | `slice::starts_with(xs, prefix)` |
| `strstr` | `slice::find_slice(xs, needle)`, an `Option(Usize)` |
| `strchr`, `memchr` | `slice::find(xs, v)` |
| `strrchr` | `slice::find_last(xs, v)` |
| `strcspn` | `slice::find_if(xs, pred)` |
| `memcpy`, `memmove`, `memset`, `memcmp` | `mem::copy`, `mem::move`, `mem::zero` or `mem::fill`, `mem::equal` (reported by `smith-mem`) |
| `strerror`, `strerror_r` | `files::describe(error)`, `process::describe(error)`, `net::describe(error)`: a constant text for each module's error |
| `strdup` | gap: `text::copy` and `text::terminated`, [#921](https://github.com/smith-ai-lab/smith/issues/921) (blocked: a new export in `std::text` breaks `front/compile.smith`) |

## math.h

| libc | smith |
|---|---|
| `sqrt`, `sqrtf` | `math::sqrt(x)` |
| `fabs` | `math::abs(x)` |
| `isfinite` | `math::is_finite(x)` |
| `SIZE_MAX`, `UINT64_MAX` | `math::max(Usize)`, `math::max(U64)` |
| `pow`, `log`, `logf`, `sin`, `cos`, `ldexp`, `ldexpf`, `fmax` | gap: [#916](https://github.com/smith-ai-lab/smith/issues/916) |

## ctype.h

| libc | smith |
|---|---|
| `isalpha` | `ascii::alpha(c)`; `ascii::digit(c)` and `ascii::space(c)` stand for `isdigit` and `isspace` ([slice-library](slice-library.md)) |

## time.h and the clocks

| libc | smith |
|---|---|
| `clock_gettime(CLOCK_MONOTONIC)`, `mach_absolute_time`, `clock_gettime_nsec_np` | `clock::monotonic()` (platform: the two OS calls behind it) |
| `clock_gettime(CLOCK_REALTIME)` | `clock::now()` |
| `nanosleep`, `usleep`, `sleep` | `clock::sleep(ns)` |
| `clock`, `getrusage`, `proc_pid_rusage`, `clock_getcpuclockid` | gap: [#922](https://github.com/smith-ai-lab/smith/issues/922) |
| `setitimer`, `alarm` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924) |

## assert.h, errno.h, stdarg.h, setjmp.h, signal.h

| libc | smith |
|---|---|
| `assert` | `contract::assert(cond)`; a precondition is `contract::requires` |
| `errno`, `E*` (`ENOENT`, `EINTR`, `EPIPE`, ...), `__error`, `__errno_location` | a call returns `Result(T, Error)` and the module's error enum names the cause (`files::Error`, `process::Error`, `net::Error`); the raw `errno` is read inside the library (platform) |
| `va_list`, `va_start`, `va_end` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924); a `.smith` function takes `auto...` pieces, as `format::to` does |
| `setjmp`, `longjmp`, `sigsetjmp`, `siglongjmp`, `jmp_buf` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924) |
| `signal`, `sigaction`, `sigaltstack`, `raise`, `sigwait`, `sigpending`, `sigemptyset`, `sigaddset`, `sigismember`, `pthread_sigmask`, `SIG*` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924) |

## unistd.h, fcntl.h, sys/stat.h, dirent.h, sys/mman.h

| libc | smith |
|---|---|
| `open`, `read`, `write`, `close`, `O_*` | `files::open`, `files::read`, `files::write`, `files::close` and `files::Mode`; `files::read_file(path)` for a whole file ([files](files.md)) |
| `stat`, `fstat`, `S_ISDIR` | `files::info(path)`, with a field that says folder or file |
| `mkdir` | `files::make_folder(path)` |
| `unlink` | `files::remove(path)` |
| `rename` | `files::move(from, to)` |
| `opendir`, `readdir`, `closedir`, `dirfd`, `DIR`, `dirent` | `files::list(folder)`, a sorted slice of entries |
| `mmap`, `munmap`, `PROT_*`, `MAP_*` | `files::map(path)` and `files::unmap(bytes)` for a file; an anonymous mapping is memory, from `mem::alloc_slice` or an arena |
| `getcwd`, `access`, `readlink`, `symlink`, `lstat`, `fstatat`, `AT_SYMLINK_NOFOLLOW`, `F_OK`, `W_OK` | gap: [#919](https://github.com/smith-ai-lab/smith/issues/919) |
| `lseek`, `pwrite`, `ftruncate`, `fchmod`, `flock`, `SEEK_*`, `LOCK_*` | gap: [#920](https://github.com/smith-ai-lab/smith/issues/920) |
| `isatty` | gap: [#925](https://github.com/smith-ai-lab/smith/issues/925) |
| `STDIN_FILENO`, `STDOUT_FILENO`, `STDERR_FILENO` | `console::` (no descriptor number in a program) |
| `fcntl`, `FD_CLOEXEC`, `O_CLOEXEC`, `O_NONBLOCK` | platform: the files, process and net libraries set them |
| `sysconf(_SC_NPROCESSORS_ONLN)` | `process::cores()` |
| `getpid`, `getppid`, `getuid`, `setpgid`, `getrlimit`, `setrlimit`, `prctl` | gap: [#923](https://github.com/smith-ai-lab/smith/issues/923) |
| `getentropy` | `entropy::seed()`, a `random::Key` |

## Processes (unistd.h, spawn.h, sys/wait.h)

| libc | smith |
|---|---|
| `posix_spawn`, `posix_spawnp`, `posix_spawn_file_actions_*`, `posix_spawnattr_*`, `fork`, `execv`, `pipe`, `dup2` | `process::spawn(argv)`, `process::start(argv)` with pipes, `process::run(argv)` for the output whole ([process](process.md)); the raw calls are the library's inside (platform) |
| `waitpid`, `WIFEXITED`, `WEXITSTATUS`, `WNOHANG` | `process::wait(&child)`, an `Exit` value |
| `kill`, `SIGKILL`, `SIGTERM` | `process::kill(&child, number)` |

## Sockets and waiting (sys/socket.h, netdb.h, poll.h, sys/event.h, sys/epoll.h)

| libc | smith |
|---|---|
| `getaddrinfo`, `freeaddrinfo`, `gai_strerror`, `socket`, `connect`, `listen`, `accept`, `send`, `read` and `write` on a socket, `AF_*`, `SOCK_*`, `addrinfo`, `sockaddr` | `net::connect(host, port)`, `net::connect_tls`, `net::listen`, `net::accept`, `net::read`, `net::write` ([net](net.md)) |
| `socketpair`, `setsockopt`, `getsockopt`, `getsockname`, `getpeername`, `bind`, `inet_pton`, `SO_*`, `MSG_NOSIGNAL` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924); inside `net` (platform) today |
| `poll`, `kqueue`, `kevent`, `epoll_*`, `timerfd_*`, `pidfd_open`, `syscall`, `EV*`, `EPOLL*`, `POLLIN` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924), decided with the removal of `io` (#240) |
| `nw_*`, `dispatch_*` (Network.framework and libdispatch) | platform: `net`'s macOS implementation, never in a program |

## Other

| libc | smith |
|---|---|
| `dlopen`, `dlsym`, `dladdr`, `RTLD_*`, `Dl_info` | gap (design): [#924](https://github.com/smith-ai-lab/smith/issues/924) |
| `alloca` | none, by design: use a fixed array or an arena (an unbounded stack allocation is the hazard) |
| `sched_yield` | none yet: no `.smith` caller outside the slot pool's wait, which is `clock::sleep` |
| `getusershell` | none: only a fixture of the effects check calls it, as a raw effect on purpose |
| `__asan_poison_memory_region`, `__asan_unpoison_memory_region` | platform: the sanitizer's interface, inside `arena` and `mem` under `contract::trusted()` |
| `NULL` | `nullptr` |
| `size_t`, `ssize_t`, `uintmax_t`, `intmax_t`, `uint8_t` to `uint64_t`, `int8_t` to `int64_t`, `uintptr_t`, `time_t`, `pid_t`, `mode_t` | `Usize`, `Isize`, `U64`, `I64`, `U8` to `U64`, `I8` to `I64`, and a library's own type where the type is a handle ([smith-language](smith-language.md)) |

## Changing a row

A library that gains a function changes its row from **gap** to the function in the same commit and closes the gap issue; a function reached for in `.smith` that is not in this file is a row, and an issue when it is a gap. The check reads this file, so a missing row is a function it cannot name a replacement for.
