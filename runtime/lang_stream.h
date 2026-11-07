// Generated split of smith.c (#2685): what runtime/lang_stream.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

void smith_lang_stream_open(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_stream_close(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_reset_declaration(SmithLang *lang);

size_t smith_lang_append(char *buffer, size_t capacity, size_t used, const char *format, ...);

extern bool (*smith_lang_import_asker)(const SmithLang *lang, const char **spellings);

size_t smith_lang_assemble_includes(SmithLang *lang, char *out, size_t capacity, bool relative);

size_t smith_lang_assemble_meta(const SmithLang *lang, char *out, size_t capacity);

void smith_lang_assemble_aligned(SmithLang *lang);

void smith_lang_finish(SmithLang *lang);

void smith_lang_end_of_file(SmithLang *lang, const SmithHost *host, const SmithToken *token);

SmithLang *smith_lang_new(void);

bool smith_lang_feed(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_misused_keyword(const char *text, size_t length);

const char *smith_lang_keyword_name_use(const char *text, size_t size, size_t start, size_t end);

size_t smith_lang_keyword_message(char *out, size_t capacity, const char *keyword, size_t length, const char *noun);

const char *smith_lang_module_name(const SmithLang *lang);
