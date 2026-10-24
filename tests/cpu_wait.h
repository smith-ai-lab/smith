// A test client's wait for a server, counted in the server's CPU time, never in wall time: a loaded machine slows the
// server and must not fail the test. The wall clock is only a backstop for a hang (a server blocked, spending none).
// The CPU time is the one process's, every thread's (user plus system); a child the server forks is not counted.
#ifndef SMITH_TESTS_CPU_WAIT_H
#define SMITH_TESTS_CPU_WAIT_H

#include <libproc.h>
#include <mach/mach_time.h>
#include <poll.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

constexpr int cpu_wait_slice_milliseconds = 50;
constexpr int cpu_wait_hang_milliseconds = 600000; // the wall backstop: ten minutes

constexpr int cpu_wait_ready = 0;
constexpr int cpu_wait_spent = 1;
constexpr int cpu_wait_hung = 2;
constexpr int cpu_wait_slices_max = cpu_wait_hang_milliseconds / cpu_wait_slice_milliseconds + 1;

static uint64_t cpu_wait_cpu_ns(pid_t pid) {
    struct rusage_info_v2 usage = {};
    if (proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t *)&usage) != 0) return 0;
    mach_timebase_info_data_t base = {};
    mach_timebase_info(&base);
    return (usage.ri_user_time + usage.ri_system_time) * base.numer / base.denom;
}

static uint64_t cpu_wait_wall_ms(void) {
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000000;
}

// Waits until `fd` is readable (cpu_wait_ready), `pid` has spent `cpu_ms` of CPU time since the call (cpu_wait_spent),
// or ten minutes of wall time have passed (cpu_wait_hung). With `extend`, a server still spending CPU time is waited
// for past `cpu_ms` of wall time instead: that is a settle, "it has been idle for cpu_ms", whose idle is the server's.
[[maybe_unused]] static int cpu_wait_for(int fd, pid_t pid, int cpu_ms) {
    uint64_t began = cpu_wait_wall_ms();
    uint64_t cpu_began = cpu_wait_cpu_ns(pid);
    for (int slice = 0; slice < cpu_wait_slices_max; slice++) {
        struct pollfd poller = {.fd = fd, .events = POLLIN};
        if (poll(&poller, 1, cpu_wait_slice_milliseconds) > 0) return cpu_wait_ready;
        if ((cpu_wait_cpu_ns(pid) - cpu_began) / 1000000 >= (uint64_t)cpu_ms) return cpu_wait_spent;
        if (cpu_wait_wall_ms() - began >= (uint64_t)cpu_wait_hang_milliseconds) return cpu_wait_hung;
    }
    return cpu_wait_hung;
}

// A settle: ready when a message came, spent when the server has been idle for `quiet_ms` of wall time in a row (any
// CPU time it spends restarts the count), hung after ten minutes.
[[maybe_unused]] static int cpu_wait_quiet(int fd, pid_t pid, int quiet_ms) {
    uint64_t began = cpu_wait_wall_ms();
    uint64_t idle_began = began;
    uint64_t cpu_last = cpu_wait_cpu_ns(pid);
    for (int slice = 0; slice < cpu_wait_slices_max; slice++) {
        struct pollfd poller = {.fd = fd, .events = POLLIN};
        if (poll(&poller, 1, cpu_wait_slice_milliseconds) > 0) return cpu_wait_ready;
        uint64_t cpu_now = cpu_wait_cpu_ns(pid);
        uint64_t now = cpu_wait_wall_ms();
        if (cpu_now > cpu_last) idle_began = now;
        cpu_last = cpu_now;
        if (now - idle_began >= (uint64_t)quiet_ms) return cpu_wait_spent;
        if (now - began >= (uint64_t)cpu_wait_hang_milliseconds) return cpu_wait_hung;
    }
    return cpu_wait_hung;
}

#endif
