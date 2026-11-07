// Generated split of smith.c (#2685): what runtime/lang_fields.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_constexpr.h"
#include "lang_generic.h"
#include "lang_main.h"

constexpr size_t smith_lang_ty_pieces_max = 11;

constexpr size_t smith_lang_ty_piece_size = 32;

constexpr size_t smith_lang_fields_pieces_max = 40;

// What a use is written as: a few tokens.
typedef struct {
    size_t count;
    SmithTokenKind kind[smith_lang_fields_pieces_max];
    char text[smith_lang_fields_pieces_max][smith_lang_name_max];
} SmithLangFieldPieces;

// One field of `fields(T)[index]`: where the expression ends, which field it is.
typedef struct {
    uint32_t consumed; // the tokens of `fields ( T ) [ index ]`
    uint32_t type_at;  // the token T
    size_t index;
    SmithFieldInfo info;
    char message[smith_lang_message_max];
} SmithLangFieldRef;

uint32_t smith_lang_fields_countof_len(uint32_t at, uint32_t limit);

bool smith_lang_fields_countof_at(uint32_t at, uint32_t limit);

bool smith_lang_fields_stage_expression(uint32_t first, uint32_t last, const SmithLangInstance *instance,
                                               uint32_t location, uint32_t *staged_first, uint32_t *staged_count);

bool smith_lang_fields_expression_type(uint32_t first, uint32_t last);

int smith_lang_fields_group(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                   uint32_t first, uint32_t last, uint32_t place, SmithLangFieldRef *ref);

bool smith_lang_fields_count(const SmithHost *host, const SmithLangGeneric *generic,
                                    const SmithLangInstance *instance, uint32_t at, uint32_t location, int64_t *count,
                                    char *message, size_t capacity);

uint32_t smith_lang_fields_typeof_end(uint32_t at, uint32_t limit);

bool smith_lang_fields_element(const SmithHost *host, char *spelling, size_t capacity);

bool smith_lang_cx_field_type(SmithLangCx *cx, uint32_t first, uint32_t last, char *spelling, size_t capacity);

bool smith_lang_fields_in(uint32_t first, uint32_t last);

bool smith_lang_fields_begins(const SmithLangReplay *replay, uint32_t at, uint32_t limit);

// The arrays `fields(T)` is, one per type, that the instance being written declared (smith_lang_fields_declare).
typedef struct {
    uint32_t instance;
    size_t count;
    char spelling[16][smith_lang_cx_name_max];
} SmithLangFieldArrays;

extern SmithLangFieldArrays smith_lang_field_arrays;

int smith_lang_fields_use(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay, uint32_t at,
                                 uint32_t limit, SmithLangFieldPieces *pieces, uint32_t *consumed);

void smith_lang_countof_finish(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                      SmithLangFrame *frame);

bool smith_lang_fields_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                     SmithLangFrame *frame);

void smith_lang_fields_define(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                     uint32_t location, uint32_t id);

void smith_lang_fields_declare(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                      const SmithLangInstance *instance, uint32_t id, uint32_t location);

uint32_t smith_lang_fields_cx_piece(const SmithLangCx *cx, uint32_t at, uint32_t last, char *piece,
                                           size_t capacity);

bool smith_lang_fields_valued(uint32_t first, uint32_t last);

bool smith_lang_fields_valued_type(SmithLangCx *cx, uint32_t first, uint32_t last, char *spelling,
                                          size_t capacity);

