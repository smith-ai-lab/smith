// Generated split of smith.c (#2685): what runtime/lang_trivia.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

bool smith_lang_header_append(SmithLang *lang, const char *text, size_t length);

void smith_lang_header_line(SmithLang *lang, const char *text, size_t length, uint32_t location);

void smith_lang_include_append(SmithLang *lang, const char *text, size_t length);

void smith_lang_add_include(SmithLang *lang, const char *text, size_t length, bool angled);

void smith_lang_note_use(SmithLang *lang, uint16_t via, const char *name, size_t length, bool tag);

void smith_lang_trivia_to(SmithLang *lang, uint32_t location);

void smith_lang_trivia_taken(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_doc_capture(SmithLang *lang);

void smith_lang_trivia_export(SmithLang *lang);

