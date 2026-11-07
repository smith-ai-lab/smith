// Generated split of smith.c (#2685): what runtime/make.c shares with the other files.
#pragma once

#include "core.h"

constexpr size_t smith_make_memory_size = (size_t)2 << 30; // reserved; touched as used

constexpr size_t smith_make_vars_max = 8192;

constexpr size_t smith_make_slots = 16384; // the variables' hash table, a power of two

constexpr size_t smith_make_holes_max = 4096;

constexpr size_t smith_make_diags_max = 512;

constexpr size_t smith_make_buffer_size = (size_t)32 << 20;

constexpr size_t smith_make_input_max = (size_t)16 << 20;

// In-band marks (bytes no Makefile holds).
constexpr char smith_make_hole_begin = '\x01';

constexpr char smith_make_hole_end = '\x02';

constexpr char smith_make_use_begin = '\x03';

constexpr char smith_make_use_sep = '\x04';

constexpr char smith_make_use_end = '\x05';

typedef struct {
    const char *file;
    uint32_t line;
} SmithMakeWhere;

typedef struct {
    SmithMakeWhere where;
    const char *message;
} SmithMakeDiag;

// A variable. A simple one holds its expansion (`value`, and `marked` with the uses of other variables marked); a
// recursive one holds its text, expanded where it is used. A knob is a variable `make VAR=value` can set: its value is
// a hole, and `default_value` is what the Makefile gives it.
typedef struct {
    const char *name;
    const char *value;
    const char *marked;
    bool simple;
    bool defined;
    bool knob;
    bool used_as_array;   // some recipe, or an array that is one, uses it as whole words
    bool option;          // a hole refers to its option (-DNAME=value): build.c declares it
    const char *fallback; // a knob's value when no option or environment variable gives one (marked)
    const char *source;   // the text the Makefile assigned, for the comments of build.c
    bool built_in;        // make's own default (CC, CXX, ...)
    bool command_line;    // set in a way a Makefile assignment must not replace (not used yet)
    SmithMakeWhere where;
    uint32_t order; // the order of the first assignment, for emitting arrays in the Makefile's order
} SmithMakeVar;

typedef struct {
    const char *expression; // a C expression of type const char *
} SmithMakeHole;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} SmithMakeBuffer;

typedef struct {
    uint32_t slots[smith_make_slots]; // index + 1 into vars, 0 for empty
    SmithMakeVar vars[smith_make_vars_max];
    size_t var_count;
    SmithMakeHole holes[smith_make_holes_max];
    size_t hole_count;
    SmithMakeDiag diags[smith_make_diags_max];
    size_t diag_count;
    SmithMakeWhere here; // what is being read or expanded, for the diagnostics
    char *pool;
    size_t pool_used;
    const char *root; // the folder the Makefile is in, as the translation was asked for
    const char *file; // the Makefile, relative to root
    uint32_t order;
    bool no_knobs;          // an undefined variable is empty, not a knob (make's own rules use CFLAGS and the like)
    bool generated_include; // a Makefile that this one makes is included: a variable it does not define may be there
    bool reading;           // the Makefile is being read (a $(eval) then adds lines to it)
    bool in_eval;           // the line being read is the text of an $(eval)
    const char *evals[64];  // the text of $(eval) calls the reader has not read yet
    size_t eval_count;
} SmithMake;

extern SmithMake smith_make;

void *smith_make_alloc(size_t size);

[[noreturn]] void smith_make_too_many(const char *things, size_t limit);

const char **smith_make_new_list(size_t count);

char *smith_make_dup(const char *text, size_t length);

const char *smith_make_copy(const char *text);

[[gnu::format(printf, 1, 2)]] const char *smith_make_format(const char *format, ...);

void smith_make_buffer_init(SmithMakeBuffer *buffer, size_t capacity);

void smith_make_put(SmithMakeBuffer *buffer, const char *text, size_t length);

void smith_make_put_char(SmithMakeBuffer *buffer, char c);

void smith_make_put_text(SmithMakeBuffer *buffer, const char *text);

[[gnu::format(printf, 2, 3)]] void smith_make_put_format(SmithMakeBuffer *buffer, const char *format, ...);

const char *smith_make_take(const SmithMakeBuffer *buffer, size_t from);

[[gnu::format(printf, 2, 3)]] void smith_make_report_at(SmithMakeWhere where, const char *format, ...);

[[gnu::format(printf, 1, 2)]] void smith_make_report(const char *format, ...);

bool smith_make_blank(char c);

bool smith_make_space(char c);

bool smith_make_next_word(const char *text, size_t length, size_t *at, size_t *start, size_t *size);

size_t smith_make_trim_length(const char *text, size_t length, size_t *from);

const char *smith_make_trim(const char *text);

bool smith_make_has_hole(const char *text, size_t length);

const char *smith_make_unmark(const char *text);

typedef struct {
    const char *text;
    size_t length;
} SmithMakeWord;

SmithMakeWord *smith_make_words(const char *text, size_t length, size_t *count);

const char *smith_make_word_text(SmithMakeWord word);

bool smith_make_match(const char *pattern, size_t pattern_length, const char *text, size_t length,
                             const char **stem, size_t *stem_length);

const char *smith_make_substitute(const char *pattern, size_t pattern_length, const char *stem,
                                         size_t stem_length);

const char *smith_make_hole_text(const char *expression);

size_t smith_make_hole_at(const char *text, size_t length, size_t i, size_t *number);

uint32_t smith_make_hash(const char *name, size_t length);

SmithMakeVar *smith_make_var_find(const char *name, size_t length);

SmithMakeVar *smith_make_var_get(const char *name, size_t length);

size_t smith_make_var_index(const SmithMakeVar *var);

void smith_make_var_set(SmithMakeVar *var, const char *value, const char *marked, bool simple);

const char *smith_make_var_value(const char *name);

