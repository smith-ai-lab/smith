// Generated split of smith.c (#2685): what runtime/lang_try.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

static constexpr char smith_lang_try_hint[] =
    "`try` returns the failure to the caller as it is: the function must return the same kind as the call (a `result` "
    "with the same error type, or an `option`); to change the error, write it out: `if (r == .error) return {.error = "
    "...};`";

typedef enum { SmithLangTryKind_None, SmithLangTryKind_Result, SmithLangTryKind_Option } SmithLangTryKind;

SmithLangTryKind smith_lang_try_kind(const char *type);

bool smith_lang_try_token(SmithLang *lang, const SmithHost *host, const SmithToken *token);
