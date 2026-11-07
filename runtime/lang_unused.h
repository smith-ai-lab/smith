// Generated split of smith.c (#2685): what runtime/lang_unused.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

extern unsigned smith_lang_unused_serial;

extern bool smith_lang_unused_has_last;

extern bool smith_lang_brackets_extern_seen;

bool smith_lang_unused_token(SmithLang *lang, const SmithHost *host, const SmithToken *token);
