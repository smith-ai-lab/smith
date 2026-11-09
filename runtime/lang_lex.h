// Generated split of smith.c (#2685): what runtime/lang_lex.c shares with the other files.
#pragma once

#include "core.h"

bool smith_raw_take_directive(uint32_t before, const char **text, size_t *length);

bool smith_lang_lines_known(void);

size_t smith_lang_line_of(uint32_t offset);

size_t smith_lang_column_of(uint32_t offset);

// The compile flags the header pass was given beside the file (`smith cc --smith-header f.smith -o f.h -I x -DY`): the
// ones that decide which header a name is found in. Only the question of smith_lang_ask_compiler uses them.
constexpr size_t smith_header_flags_max = 64;

extern const char *smith_header_flags[smith_header_flags_max];

extern size_t smith_header_flag_count;

extern const char *smith_header_source;

// The files a header pass read, for `-MD -MF depfile` (#2937): the step cache serves a header step only for the
// contents of every file the pass read. smith_header_depend adds a file the pass read (the compiler it asked read
// them too); smith_header_depend_unknown says a file may have been read that cannot be named, so no depfile is written
// and the step is never cached.
void smith_header_depend(const char *path);

void smith_header_depend_unknown(void);

// Adds the files a make-style depfile names (what `smith cc -MD -MF` wrote), but not `skip`; false if it cannot be
// read.
bool smith_header_depend_file(const char *depfile, const char *skip);

int smith_lang_main(int count, char **args, bool header);
