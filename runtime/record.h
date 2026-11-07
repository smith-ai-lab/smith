// Generated split of smith.c (#2685): what runtime/record.c shares with the other files.
#pragma once

#include "core.h"
#include "coverage.h"

// Per-test records (issue #456, knowledge/coverage-selection.md). Included by smith.c after runtime/coverage.c.
//
// Every test that ran under coverage keeps a record in `.smith/records/<test>.rec` (or $SMITH_RECORDS_DIR), keyed by
// content only, so it needs no repository and no commit, and a record that came from elsewhere is as good as one made
// here: a hash that still matches means the same thing. Text, a tab between fields:
//   smith-record 2
//   test <name>
//   result <pass|fail>
//   opened 1                                            the record lists what the test opened (read, readdir lines)
//   fn <file> <name> <lines> <hash> <first line hash>    a function the test executed, found again by its first line
//   outside <file> <hash>                               the file's code outside its functions (globals, macros, types,
//   the
//                                                       lines of a table's braces): for each executed file, and each
//                                                       .c or .h file one of them includes, chains included
//   input <path> <hash>                                  any other file the executed files include, and build.c
//   read <path> <hash>                                   a file of the project the test or its children read
//   readdir <path>/ <hash>                              a folder of the project they listed: the names in it
// A test is stale when its last result failed, a function it executed is not found as it was, an outside hash or an
// input or a read file differs (or is gone). A hash is 64-bit FNV-1a; the guard is against edits, not forgery.
constexpr size_t smith_rec_source_max = 1U << 23;

constexpr size_t smith_rec_records_max = 8192;

extern char smith_rec_source[smith_rec_source_max + 1];

extern bool smith_rec_line_opens[smith_cov_lines_max];

extern bool smith_rec_line_inside[smith_cov_lines_max];

extern size_t smith_rec_line_count;

extern char smith_rec_source_path[PATH_MAX];

// Judged elsewhere (runtime/changed.c, issue #456): functions a change reaches through an `#embed` (file, name), and an
// input whose change is not a reason by itself (build.c, when its diff touches only functions no test runs).
constexpr size_t smith_rec_touched_max = 64;

extern const char *smith_rec_touched_file[smith_rec_touched_max];

extern const char *smith_rec_touched_name[smith_rec_touched_max];

extern size_t smith_rec_touched_count;

extern const char *smith_rec_ignored_input;

bool smith_rec_load_source(const char *relative);

bool smith_rec_write(const char *dir, const char *name, const char *clean, bool pass);

constexpr size_t smith_rec_named_max = 1U << 18;

typedef struct {
    const char *name;
    bool stale;
    const char *why;
    size_t named_first; // its files in smith_rec_named: what its program was built from
    size_t named_count;
    double cpu;  // seconds the test took when it was recorded, 0 when unknown
    bool opened; // the record lists what the test opened (#739): a file it never opened is no input of it
} SmithRecResult;

extern const char *smith_rec_named[smith_rec_named_max];

extern SmithRecResult smith_rec_results[smith_rec_records_max];

extern size_t smith_rec_result_count;

bool smith_rec_mentioned(const char *path);

size_t smith_rec_check_folder(const char *folder);

void smith_rec_reset(void);
