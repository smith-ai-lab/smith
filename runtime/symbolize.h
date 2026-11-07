// Generated split of smith.c (#2685): what runtime/symbolize.c shares with the other files.
#pragma once

#include "core.h"

extern char smith_sym_directory[PATH_MAX];

bool smith_sym_machinery(const char *function);

bool smith_sym_system_path(const char *path);

const char *smith_sym_short(const char *path);

bool smith_sym_source(const char *file, uint32_t line, char *text, size_t capacity);

int smith_symbolize_main(int argc, char **argv);

int smith_llvm_symbolizer_main(int argc, char **argv);

