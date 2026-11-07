// Generated split of smith.c (#2685): what runtime/lang_import.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

void smith_lang_module_line(SmithLang *lang, const SmithHost *host, const SmithToken *keyword);

void smith_lang_module_to_path(const char *module, char *path, size_t capacity);

bool smith_lang_is_import(SmithLang *lang, const SmithHost *host, const SmithToken *keyword);

void smith_lang_imports_last(SmithLang *lang, const SmithHost *host, uint32_t location);

void smith_lang_import(SmithLang *lang, const SmithHost *host, const SmithToken *keyword);

bool smith_lang_is_module_line(SmithLang *lang, const SmithHost *host, const SmithToken *keyword);

