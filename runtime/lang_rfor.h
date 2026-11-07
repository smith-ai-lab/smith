// Generated split of smith.c (#2685): what runtime/lang_rfor.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

bool smith_lang_rfor(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_rfor_count(SmithLang *lang, const SmithToken *token);

void smith_lang_rfor_close(SmithLang *lang, const SmithToken *token);
