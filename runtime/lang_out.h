// Generated split of smith.c (#2685): what runtime/lang_out.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

[[gnu::format(printf, 4, 5)]] void smith_lang_error(SmithLang *lang, const SmithHost *host, uint32_t location,
                                                    const char *format, ...);

void smith_lang_limit(SmithLang *lang, const char *table, size_t size);

[[gnu::format(printf, 4, 5)]] void smith_lang_note(const SmithHost *host, uint32_t error_location, uint32_t location,
                                                   const char *format, ...);

bool smith_lang_word_char(char c);

bool smith_lang_put(char *buffer, size_t capacity, size_t *used, const char *text, size_t length, bool line_start,
                    bool space);

// A pool of tokens with their text. `smith_lang_gt` is a stack: the tokens of the generics, which stay for the whole
// file, and above them the calls being read, which are popped when they have been written. `smith_lang_it` only grows:
// the types the instances were asked for.
typedef struct {
    SmithLangGenToken *tokens;
    char *text;
    uint32_t count;
    uint32_t text_used;
    uint32_t capacity;
    uint32_t text_capacity;
    uint32_t keep; // smith_lang_it only: what an instance's types hold (a call typed inside another's arguments)
    uint32_t keep_text;
} SmithLangPool;

extern SmithLangGenToken smith_lang_gt_tokens[smith_lang_gt_max];

extern char smith_lang_gt_chars[smith_lang_gt_text_max];

extern SmithLangGenToken smith_lang_it_tokens[smith_lang_it_max];

extern char smith_lang_it_chars[smith_lang_it_text_max];

extern SmithLangPool smith_lang_gt;

extern SmithLangPool smith_lang_it;

constexpr unsigned smith_lang_gen_flag_line_start = 1U;

constexpr unsigned smith_lang_gen_flag_space = 2U;

constexpr unsigned smith_lang_gen_flag_expand = 4U;

constexpr unsigned smith_lang_gen_flag_value = 16U; // an `auto` parameter's name in the result type's `typeof(...)`

constexpr unsigned smith_lang_gen_flag_slice = 32U; // `smith_array_T_n`: the tag of a slice of the type parameter T

constexpr unsigned smith_lang_gen_flag_made =
    64U; // a captured token the lowering made: written again, still no source's

uint32_t smith_lang_pool_add(SmithLangPool *pool, SmithLangGenToken token, const char *text, size_t length);

const char *smith_lang_pool_text(const SmithLangPool *pool, const SmithLangGenToken *token);

SmithLangGenToken smith_lang_gt_from(const SmithToken *token);

SmithToken smith_lang_gt_token(const SmithLangPool *pool, const SmithLangGenToken *kept);

bool smith_lang_capturing(const SmithLang *lang);

void smith_lang_dump_directives(SmithLang *lang, uint32_t before);

bool smith_lang_tag_before(const char *text, size_t used);

void smith_lang_flush_kept(SmithLang *lang, const SmithHost *host);

void smith_lang_emit(SmithLang *lang, const SmithHost *host, SmithToken token);

void smith_lang_emit_code(SmithLang *lang, const SmithHost *host, SmithToken token);

void smith_lang_emit_header(SmithLang *lang, SmithToken token);

void smith_lang_emit_kept(SmithLang *lang, const SmithHost *host, const SmithToken *token);

SmithToken smith_lang_synth(SmithTokenKind kind, const char *text, size_t length, const SmithToken *like);

SmithToken smith_lang_static_keyword(const SmithToken *like);

const SmithToken *smith_lang_peek(SmithLang *lang, const SmithHost *host, size_t index, bool unexpanded);

SmithToken smith_lang_take(SmithLang *lang, const SmithHost *host);

SmithLangText smith_lang_scratch_store(SmithLang *lang, const char *text, size_t length);

const char *smith_lang_scratch_str(SmithLangText text);

const char *smith_lang_prefixed(SmithLang *lang, const char *name, size_t length);
