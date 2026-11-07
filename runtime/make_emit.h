// Generated split of smith.c (#2685): what runtime/make_emit.c shares with the other files.
#pragma once

#include "core.h"
#include "make.h"

constexpr size_t smith_make_body_size = (size_t)256 << 20;

typedef struct {
    const char *text; // the word as the shell reads it: unquoted, with its holes
    int32_t span;     // the longest whole-word use of a variable that starts here, or -1
} SmithMakeArg;

typedef struct {
    size_t var;
    size_t first; // the words first .. last - 1 are the variable's value
    size_t last;
} SmithMakeSpan2;

typedef struct {
    SmithMakeArg *args;
    size_t count;
    SmithMakeSpan2 *spans;
    size_t span_count;
} SmithMakeArgv;

bool smith_make_lex(const char *text, bool shell, SmithMakeArgv *out);

const char *smith_make_array_name(const SmithMakeVar *var);

typedef struct {
    SmithMakeBuffer *body;
    bool words_used; // build.c needs the helper that splits an option into words
} SmithMakeOutput;

extern SmithMakeOutput smith_make_output;

typedef struct {
    size_t start; // the unit's text is buffer[start .. end)
    size_t end;
    const char *type; // the variable it declares, if any, as `type name = ` at text[decl_at .. decl_at + decl_length)
    const char *name;
    size_t decl_at;
    size_t decl_length;
    bool maybe_unused; // nothing reads the variable (an option the build never uses): [[maybe_unused]]
} SmithMakeUnit;

typedef struct {
    SmithMakeBuffer *buffer;
    SmithMakeUnit *units;
    size_t count;
} SmithMakeUnits;

extern SmithMakeUnits *smith_make_unit_list;

void smith_make_units_init(SmithMakeUnits *list, SmithMakeBuffer *buffer);

void smith_make_unit_begin(SmithMakeUnits *list);

void smith_make_unit_end(void);

void smith_make_put_decl(SmithMakeBuffer *out, const char *type, const char *name, bool maybe_unused);

// Every array a list named, in order, so that an array is defined after the arrays its definition names.
constexpr size_t smith_make_ref_log_max = 65536;

extern size_t smith_make_ref_log[smith_make_ref_log_max];

extern size_t smith_make_ref_log_count;

const char *smith_make_list(const SmithMakeArgv *argv, const size_t *indices, size_t count);

const char *smith_make_list_all(const SmithMakeArgv *argv);

const char *smith_make_names(const char *const *names, size_t count);
