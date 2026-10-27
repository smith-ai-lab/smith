// What smith's own C shares before anything else: the assertion every build keeps, the exit statuses, the way a
// program stops with a message, and the byte copy. Included by smith.c and by scripts/header_tool.c (the standalone
// writer of the .smith libraries' headers), which include runtime/lang*.c and so need the same.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// On in every build. A failure is a bug: report where and stop.
#define SMITH_ASSERT(condition) \
    do { \
        if (!(condition)) smith_assert_fail(#condition, __FILE__, __LINE__); \
    } while (0)

// Exit statuses are values for exit() and main's return, not alternatives of one type, so they are not an enum.
constexpr int smith_exit_usage = 2;
constexpr int smith_exit_missing = 127;

[[noreturn]] static void smith_assert_fail(const char *condition, const char *file, int line) {
    fprintf(stderr, "smith: assertion failed: %s (%s:%d)\n", condition, file, line);
    fflush(stderr);
    abort();
}

[[noreturn]] static void smith_die(int status, const char *message, const char *detail) {
    if (detail != nullptr) {
        fprintf(stderr, "smith: %s: %s\n", message, detail);
    } else {
        fprintf(stderr, "smith: %s\n", message);
    }
    exit(status);
}

// Copies `size` bytes and no terminator: a piece of text put inside a longer one. memcpy says the same, but a char
// copy of a string's length reads to clang-tidy (bugprone-not-null-terminated-result) as a string left unterminated.
static void smith_copy_bytes(void *to, const void *from, size_t size) {
    memcpy(to, from, size);
}
