// Generated split of smith.c (#2685): what runtime/lang_constexpr.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_generic.h"
#include "lang_main.h"

constexpr size_t smith_lang_cx_stack_max = 32;

constexpr size_t smith_lang_cx_types_max = 8;

constexpr size_t smith_lang_cx_name_max = 256;

typedef struct {
    bool is_type;
    int64_t number;
    uint8_t type; // is_type: the index of its canonical spelling
} SmithLangCxValue;

typedef struct {
    uint8_t code; // a binary operator (an index of smith_lang_cx_binary), a unary one (100 + a character), or open
    int8_t precedence;
} SmithLangCxOp;

typedef struct {
    SmithLang *lang;
    const SmithHost *host;
    const SmithLangInstance *instance;
    const SmithLangReplay *replay; // the argument list and loop variables the condition may name
    uint32_t location;
    bool unknown_name; // the evaluation failed at a name that is no constant or type: `unknown_at` is its token
    uint32_t unknown_at;
    char canonical[smith_lang_cx_types_max][smith_lang_cx_name_max];
    size_t canonical_count;
    char message[smith_lang_message_max];
    SmithLangCxValue values[smith_lang_cx_stack_max];
    size_t value_count;
    SmithLangCxOp ops[smith_lang_cx_stack_max];
    size_t op_count;
} SmithLangCx;

constexpr uint32_t smith_lang_cx_value_nodes = 4096;

[[gnu::format(printf, 2, 3)]] bool smith_lang_cx_fail(SmithLangCx *cx, const char *format, ...);

SmithLangGenToken *smith_lang_cx_token(uint32_t at);

bool smith_lang_cx_word(const SmithLangGenToken *token, const char *word);

uint32_t smith_lang_cx_skip_group(uint32_t at, uint32_t limit);

uint32_t smith_lang_cx_statement_end(uint32_t at, uint32_t limit);

bool smith_lang_cx_simple(SmithLangCx *cx, uint32_t first, uint32_t last, int64_t *result);

bool smith_lang_cx_text(const SmithLangCx *cx, uint32_t first, uint32_t last, char *text, size_t capacity);

bool smith_lang_cx_eval(SmithLangCx *cx, uint32_t first, uint32_t last, int64_t *result);

void smith_lang_replay_if(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay, SmithLangFrame *frame);

bool smith_lang_generic_if_constexpr(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                     SmithLangWord word);

void smith_lang_for_constexpr_outside(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_generic_for_constexpr(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                      SmithLangWord word);

bool smith_lang_result_call(const SmithLangGeneric *generic);

const char *smith_lang_type_call_ask(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                     SmithLangInstance *instance, uint32_t first, uint32_t last, char *spelling,
                                     size_t capacity, uint64_t *handle);

void smith_lang_gen_put_slice_result(SmithLang *lang, const SmithHost *host, uint64_t handle, uint32_t location,
                                     uint32_t instance_id);

void smith_lang_gen_put_result(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                               const SmithLangInstance *instance, uint32_t location, uint32_t instance_id);
