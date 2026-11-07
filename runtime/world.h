// Generated split of smith.c (#2685): what runtime/world.c shares with the other files.
#pragma once

#include "core.h"

typedef struct {
    int code; // 0 for success, otherwise an errno value
    char message[192];
} SmithWorldError;

typedef struct {
    SmithWorldError error;
    const uint8_t *data; // size bytes and a zero; null when the read failed
    size_t size;
} SmithWorldRead;

typedef struct {
    SmithWorldError error; // ENOENT if there is nothing at the path
    bool folder;
    size_t size; // bytes of a file; 0 for a folder
} SmithWorldInfo;

typedef enum {
    SmithWorld_Replace, // creates the file or replaces what it held
    SmithWorld_Atomic,  // the bytes go to "<path>.tmp" and are renamed over the path
    SmithWorld_Append   // adds to the end, creating the file if it is not there
} SmithWorldWrite;

SmithWorldInfo smith_world_info(const char *path);

SmithWorldRead smith_world_read(SmithArena *arena, const char *path);

SmithWorldError smith_world_remove(const char *path);

[[maybe_unused]] SmithWorldError smith_world_write(const char *path, const uint8_t *data, size_t size,
                                                          SmithWorldWrite mode);

[[maybe_unused]] SmithWorldError smith_world_make_folder(const char *path);

[[maybe_unused]] uint64_t smith_world_clock(void);

SmithArena smith_world_arena(void);

[[maybe_unused]] ptrdiff_t smith_world_capture(const char *const *argv, char *buffer, size_t capacity);

