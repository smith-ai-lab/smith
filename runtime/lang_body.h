// Generated split of smith.c (#2685): what runtime/lang_body.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

void smith_lang_body_note(SmithLang *lang);

void smith_lang_body_write(SmithLang *lang);

void smith_lang_body_use(SmithLang *lang, const SmithHost *host, uint32_t location);

