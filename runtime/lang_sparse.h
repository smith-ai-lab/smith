// Generated split of smith.c (#2685): what runtime/lang_sparse.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

bool smith_lang_previous(SmithLang *lang, const SmithHost *host, unsigned back, SmithToken *token);

void smith_lang_remember(SmithLang *lang, const SmithToken *token);

bool smith_lang_previous_is(SmithLang *lang, const SmithHost *host, unsigned back, const char *punctuator);

bool smith_lang_sparse_member(SmithLang *lang, const SmithHost *host);

bool smith_lang_sparse_in_attribute(SmithLang *lang, const SmithHost *host);

bool smith_lang_sparse_typeish(SmithLang *lang, const SmithHost *host);

void smith_lang_sparse(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_sparse_ident(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_body(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_type_name(SmithLang *lang, const char *name, size_t length);

bool smith_lang_fast_brace(SmithLang *lang, const SmithHost *host, bool open);
