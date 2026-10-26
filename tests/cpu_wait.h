// A test client's wait for a server, counted in the server's CPU time, never in wall time: a loaded machine slows the
// server and must not fail the test. The wall clock is only a backstop for a hang (a server blocked, spending none).
// The CPU time is the one process's, every thread's (user plus system); a child the server forks is not counted.
#ifndef SMITH_TESTS_CPU_WAIT_H
#define SMITH_TESTS_CPU_WAIT_H

#ifdef __APPLE__
#include <libproc.h>
#include <mach/mach_time.h>
#else
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif
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

#ifdef __APPLE__
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

// The children of `pid`, at most `max`; returns how many.
[[maybe_unused]] static int cpu_wait_children(pid_t pid, pid_t *out, int max) {
    int bytes = proc_listchildpids(pid, out, max * (int)sizeof(pid_t));
    return bytes > 0 ? bytes / (int)sizeof(pid_t) : 0;
}
#else
// utime and stime, fields 14 and 15 of /proc/<pid>/stat (after the command's closing parenthesis), in clock ticks.
static uint64_t cpu_wait_cpu_ns(pid_t pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    FILE *file = fopen(path, "r");
    if (file == nullptr) return 0;
    char text[1024];
    size_t count = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    text[count] = '\0';
    char *cursor = strrchr(text, ')');
    if (cursor == nullptr) return 0;
    cursor++;
    while (*cursor == ' ')
        cursor++;
    if (*cursor == '\0') return 0;
    cursor++; // the state, one character
    // ppid, pgrp, session, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt, then utime and stime
    constexpr int fields = 12;
    unsigned long long field[fields] = {};
    for (int i = 0; i < fields; i++) {
        char *end = nullptr;
        field[i] = strtoull(cursor, &end, 10);
        if (end == cursor) return 0;
        cursor = end;
    }
    return (uint64_t)(field[10] + field[11]) * 1000000000ULL / (uint64_t)sysconf(_SC_CLK_TCK);
}

static uint64_t cpu_wait_wall_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

// The children of `pid` (of every one of its threads: /proc/<pid>/task/<tid>/children), at most `max`; returns how
// many.
[[maybe_unused]] static int cpu_wait_children(pid_t pid, pid_t *out, int max) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/task", (int)pid);
    DIR *tasks = opendir(path);
    if (tasks == nullptr) return 0;
    int found = 0;
    for (struct dirent *task = readdir(tasks); task != nullptr && found < max; task = readdir(tasks)) {
        if (task->d_name[0] == '.') continue;
        char children[320];
        snprintf(children, sizeof children, "/proc/%d/task/%s/children", (int)pid, task->d_name);
        FILE *file = fopen(children, "r");
        if (file == nullptr) continue;
        char list[1024];
        size_t length = fread(list, 1, sizeof list - 1, file);
        fclose(file);
        list[length] = '\0';
        char *cursor = list;
        while (found < max) { // one number per child, bounded by max
            char *end = nullptr;
            long child = strtol(cursor, &end, 10);
            if (end == cursor) break;
            out[found++] = (pid_t)child;
            cursor = end;
        }
    }
    closedir(tasks);
    return found;
}
#endif

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
