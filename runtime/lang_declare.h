// Generated split of smith.c (#2685): what runtime/lang_declare.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

void smith_lang_open_state(SmithLang *lang, const SmithHost *host, const SmithToken *token);

void smith_lang_close_state(SmithLang *lang);

void smith_lang_punctuator_state(SmithLang *lang, char c);

void smith_lang_semicolon_state(SmithLang *lang, char c);

void smith_lang_word_state(SmithLang *lang, const SmithToken *token, SmithLangWord word);

void smith_lang_declare_process(SmithLang *lang, const SmithHost *host, const SmithToken *token);

