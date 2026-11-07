// Generated split of smith.c (#2685): what runtime/lang_private.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

void smith_lang_priv_begin(SmithLang *lang);

void smith_lang_priv_end(SmithLang *lang);

void smith_lang_priv_abandon(SmithLang *lang);

void smith_lang_priv_form(SmithLang *lang, uint8_t form, bool linked);

void smith_lang_priv_write(SmithLang *lang, const SmithToken *token);

void smith_lang_priv_name(SmithLang *lang, const char *name, size_t length, const char *cname,
                                 size_t cname_length);

void smith_lang_priv_resolve(SmithLang *lang);

size_t smith_lang_priv_open(SmithLang *lang, char *out, size_t capacity, size_t used);

size_t smith_lang_priv_close(SmithLang *lang, char *out, size_t capacity, size_t used);

size_t smith_lang_priv_section(SmithLang *lang, char *out, size_t capacity, size_t used, bool c_linkage);

size_t smith_lang_priv_linked_line(const SmithLang *lang, char *out, size_t capacity, size_t used);

size_t smith_lang_priv_hidden_line(const SmithLang *lang, char *out, size_t capacity, size_t used);

void smith_lang_hidden_read(SmithLang *lang, const char *text, size_t length, const char *prefix,
                                   size_t prefix_length);

bool smith_lang_is_hidden(const SmithLang *lang, const char *lowered, size_t length);

void smith_lang_linked_read(SmithLang *lang, const SmithHost *host);

bool smith_lang_is_linked(const SmithLang *lang, const char *name, size_t length);

