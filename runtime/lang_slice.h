// Generated split of smith.c (#2685): what runtime/lang_slice.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_generic.h"
#include "lang_main.h"
#include "lang_tuple.h"

constexpr size_t smith_lang_slice_chain_max = 200;

constexpr size_t smith_lang_slice_cur_max = 600;

constexpr size_t smith_lang_slice_dims_max = 8;

constexpr size_t smith_lang_slice_text_max = 4096;

constexpr char smith_lang_slice_prefix[] = "smith_array_";

size_t smith_lang_text_skip(const char *text, size_t length, size_t at);

bool smith_lang_text_has_slice(const char *text, size_t length);

bool smith_lang_text_has_str(const char *text, size_t length);

bool smith_lang_slice_active(const SmithLang *lang, const SmithHost *host);

typedef struct {
    const SmithToken *tokens;
    size_t count;
} SmithLangSliceSpan;

SmithTokenKind smith_lang_slice_kind(const char *word);

size_t smith_lang_slice_pattern(SmithToken *out, size_t used, size_t capacity, const char *pattern,
                                       const SmithLangSliceSpan *spans, const SmithToken *like);

// `f32[:][2]` is two dimensions, the first of them run-time. The struct of a run-time array keeps its sizes in
// `count[k]`, k the number of `:`; the fixed ones are in its name (`smith_array_f32_n_2`), and everything else (the
// strides, the checks) is written from them. The first dimension is always a `:`: a row of fixed size is a `[:]`.
typedef struct {
    size_t count;
    bool runtime[smith_lang_slice_dims_max];
    uint64_t fixed[smith_lang_slice_dims_max];
    bool restrict_data; // `[restrict :]`: the data pointer is `__restrict` (C and C++ headers both read it; its own
                        // struct, `smith_array_restrict_...`)
} SmithLangSliceDims;

SmithLangSliceDims smith_lang_slice_dims_one(void);

size_t smith_lang_slice_dims_runtime(const SmithLangSliceDims *dims);

bool smith_lang_slice_tag_dims_count(const char *tag, size_t *all, size_t *runtime);

bool smith_lang_slice_dims_parse(const char *tag, SmithLangSliceDims *dims, size_t *stem);

void smith_lang_slice_tag(const char *spelled, char *tag, size_t capacity);

bool smith_lang_slice_tag_of(SmithLang *lang, const SmithToken *elem, size_t count,
                                    const SmithLangSliceDims *dims, char *tag, size_t capacity, const char **problem,
                                    uint32_t *where);

bool smith_lang_slice_defined(const SmithLang *lang, const char *tag);

size_t smith_lang_slice_define(SmithLang *lang, const SmithHost *host, SmithToken *out, size_t used,
                                      size_t capacity, const char *tag, const SmithToken *elem, size_t count,
                                      const SmithLangSliceDims *dims, const SmithToken *like);

bool smith_lang_slice_statement_word(const SmithToken *token);

void smith_lang_slice_flag(SmithLang *lang);

bool smith_lang_slice_begin(SmithLang *lang, const SmithHost *host, const SmithToken *first);

bool smith_lang_slice_typeof(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_slice_str(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_slice_boundary(const SmithToken *token);

typedef enum {
    SmithLangSlice_None,
    SmithLangSlice_Slice,
    SmithLangSlice_Pointer,
    SmithLangSlice_Array
} SmithLangSliceKind;

typedef struct {
    SmithLangSliceKind kind;
    char tag[smith_lang_tuple_name_max];   // a slice: its struct
    char plain[smith_lang_tuple_name_max]; // a slice: its struct without `restrict` (what a cut or a row makes)
    char stem[smith_lang_tuple_name_max];  // a slice: its tag without the dimensions
    SmithLangSliceDims dims;               // a slice: its dimensions
    char element[256];                     // a pointer or an array: the element as the parser writes it
    uint64_t count;                        // an array: how many
    uint32_t rank;                         // an array: its dimensions, `array_dims` their sizes
    uint64_t array_dims[smith_lang_slice_dims_max];
} SmithLangSliceType;

bool smith_lang_slice_type(const SmithHost *host, const char *text, SmithLangSliceType *out);

bool smith_lang_slice_text(SmithLang *lang, const SmithToken *tokens, size_t count, char *out, size_t capacity);

// What the lowering made of a chain, being written.
typedef struct {
    SmithToken tokens[smith_lang_slice_cur_max];
    size_t count;
    bool changed;
    bool overflow;
    bool sliced; // the last subscript was a slice (`xs[a:b]`)
} SmithLangSliceCur;

void smith_lang_slice_add(SmithLangSliceCur *cur, const SmithToken *tokens, size_t count);

typedef struct {
    size_t colon;  // index of the slice's `:` in the inner tokens, or SIZE_MAX for an index
    size_t second; // a second `:` (a step), or SIZE_MAX
} SmithLangSliceShape;

SmithLangSliceShape smith_lang_slice_shape(const SmithToken *inner, size_t count);

bool smith_lang_slice_cut_type(SmithLang *lang, const SmithHost *host, const SmithToken *expr, size_t count,
                                      SmithLangSliceType *out);

size_t smith_lang_slice_right(SmithLang *lang, const SmithHost *host, size_t from, SmithToken *out,
                                     size_t capacity);

bool smith_lang_slice_declared_text(SmithLang *lang, const SmithHost *host, char *text, size_t capacity,
                                           bool *pointer);

bool smith_lang_slice_spelled(const SmithHost *host, const char *spelling, SmithExprType *found);

bool smith_lang_slice_init_compound(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_slice_equals(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_slice_cover(SmithToken *tokens, size_t count);

bool smith_lang_slice_chain(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_slice_return(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_slice_countof(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                     const char *tokens, const SmithToken *span, size_t close);

typedef struct {
    const char *text; // words, or nullptr: the generic's tokens [first, last)
    uint32_t first;
    uint32_t last;
} SmithLangSlicePart;

void smith_lang_slice_parts(SmithLangReplay *replay, uint32_t place, const SmithLangSlicePart *parts,
                                   size_t count);

void smith_lang_slice_replay_argument(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                             uint32_t place, const char *from_tag, const char *to_tag, uint32_t first,
                                             uint32_t last);

void smith_lang_slice_replay_array(SmithLang *lang, SmithLangReplay *replay, uint32_t place, const char *to_tag,
                                          bool literal, uint32_t first, uint32_t last);

bool smith_lang_slice_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                    SmithLangFrame *frame);

