// Generated split of smith.c (#2685): what runtime/lang_target.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

size_t smith_lang_target_header_prelude(const SmithLang *lang, char *out, size_t capacity, size_t used);

size_t smith_lang_target_include_prelude(const SmithLang *lang, char *out, size_t capacity, size_t used);

void smith_lang_target_prelude(const SmithLang *lang, FILE *out);

int smith_lang_target_macro_value(const SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                  int64_t *value);

void smith_lang_target_use(SmithLang *lang, const SmithHost *host, const SmithToken *name, const SmithToken *member,
                           SmithToken *result, char *digits, size_t capacity);

bool smith_lang_ifc_begin(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_ifc_close(SmithLang *lang, const SmithHost *host);

void smith_lang_ifc_constant(SmithLang *lang, const SmithHost *host);
