// Generated split of smith.c (#2685): what runtime/lang_head.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

SmithToken smith_lang_held_token(const SmithLang *lang, const SmithLangHeld *held);

void smith_lang_resolve(SmithLang *lang, const SmithHost *host, const SmithToken *next);

// A module's symbol is `_` and its path (`_std::test::order`) on every platform: Mach-O's own spelling of a C name,
// and on Linux the same, so that a header written on one platform (include/smith/*.h, generated on macOS) names the
// symbols a compile on the other defines (issue #305).
constexpr char smith_lang_symbol_underscore[] = "_";

typedef enum { SmithLangArray_None, SmithLangArray_Sized, SmithLangArray_Unsized } SmithLangArray;

SmithLangArray smith_lang_array_size(const SmithToken *token);

void smith_lang_unsized_error(SmithLang *lang, const SmithHost *host, uint32_t location);

void smith_lang_array_token(SmithLang *lang, const SmithHost *host, const char *text, size_t length,
                                   SmithTokenKind kind, uint32_t before, uint32_t after);

void smith_lang_emit_head(SmithLang *lang, const SmithHost *host, bool header_too, bool with_static);

void smith_lang_end_declaration(SmithLang *lang);

void smith_lang_push_brace(SmithLang *lang, const SmithHost *host, SmithLangBrace kind, uint32_t location);

void smith_lang_enter_body(SmithLang *lang, const SmithHost *host, uint32_t location);

void smith_lang_head(SmithLang *lang, const SmithHost *host, const SmithToken *token);

