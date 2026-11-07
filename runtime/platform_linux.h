// Generated split of smith.c (#2685): what runtime/platform_linux.c shares with the other files.
#pragma once

#include "core.h"

constexpr size_t smith_platform_id_bytes = 16;

bool smith_platform_file_id(const char *path, char *out);

void smith_platform_self_id(char *out, size_t size);

bool smith_platform_clone(const char *from, const char *to);

bool smith_platform_clone_file(const char *from, const char *to);

[[maybe_unused]] unsigned long long smith_platform_monotonic_ns(void);

struct timespec smith_platform_mtime(const struct stat *info);

[[maybe_unused]] int smith_platform_process_stopped(long pid);

size_t smith_platform_decompress(unsigned char *out, size_t capacity, const unsigned char *in, size_t size);

