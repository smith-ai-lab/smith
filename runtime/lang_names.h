// Generated split of smith.c (#2685): what runtime/lang_names.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

bool smith_lang_header_word(const char *text, size_t length, const char *marker, size_t *cursor, size_t *start,
                            size_t *end);

SmithLangWritten *smith_lang_written_lookup(const SmithLang *lang, const char *text, size_t length);

void smith_lang_written_add(SmithLang *lang, const char *lowered, size_t lowered_length, const char *written,
                            size_t written_length, size_t prefix_length, SmithLangText module, bool direct,
                            const char *member, size_t member_length);

const SmithLangCName *smith_lang_cname_find(const SmithLang *lang, const char *fallback, size_t length);

void smith_lang_c_prefix(const char *module, char *prefix, size_t capacity);

void smith_lang_written_read(SmithLang *lang, const SmithHost *host, const char *header, const char *module,
                             const char *bind, size_t bind_length);

void smith_lang_written_access(SmithLang *lang, const SmithLangBinding *binding, size_t lowered_length,
                               const char *member, size_t member_length);

void smith_lang_note_qualifier(SmithLang *lang, const SmithLangBinding *binding, const SmithToken *cursor);
