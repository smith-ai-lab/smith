// Generated split of smith.c (#2685): what runtime/make_translate.c shares with the other files.
#pragma once

#include "core.h"

bool smith_make_format_files(FILE *in, FILE *out);

int smith_make_translate(const char *path, FILE *out, FILE *errors);
