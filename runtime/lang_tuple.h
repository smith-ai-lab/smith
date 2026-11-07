// Generated split of smith.c (#2685): what runtime/lang_tuple.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

constexpr size_t smith_lang_tuple_name_max = 512; // the spelling of a tuple's fields

constexpr size_t smith_lang_tuple_long = 120; // a spelling longer than this is cut and hashed

constexpr char smith_lang_tuple_prefix[] = "smith_tuple_";

extern size_t smith_lang_tuple_used;

const char *smith_lang_tuple_keep(const char *text, size_t length);

size_t smith_lang_tuple_resolve(SmithLang *lang, const SmithToken *span, size_t i, size_t to, char *out,
                                size_t capacity);

bool smith_lang_tuple_spell(SmithLang *lang, const SmithToken *span, size_t from, size_t to, char *out, size_t capacity,
                            const char **problem, uint32_t *where);

void smith_lang_tuple_tag(const char *spelled, char *tag, size_t capacity);

bool smith_lang_tuple_replace(SmithLang *lang, size_t count, const SmithToken *tokens, size_t made);

SmithToken smith_lang_tuple_made(SmithTokenKind kind, const char *text, size_t length, const SmithToken *like,
                                 bool space);

void smith_lang_tuple_begin(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_tuple_in_generic(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_tuple_previous(SmithLang *lang, const SmithHost *host, unsigned back, SmithToken *token);

bool smith_lang_tuple_base(SmithLang *lang, const SmithHost *host, char *out, size_t capacity);

bool smith_lang_tuple_subscript(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_tuple_countof(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_tuple_return(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_tuple_hoist(char *body, size_t *used, size_t capacity);
