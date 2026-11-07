// Generated split of smith.c (#2685): what runtime/lang_enum.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_generic.h"
#include "lang_main.h"
#include "lang_slice.h"

bool smith_lang_enum_begin(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_enum_equals(SmithLang *lang, const SmithHost *host);

bool smith_lang_enum_member(SmithLang *lang, const SmithHost *host, const char *spelling,
                                   const SmithToken *tokens, size_t count, SmithLangSliceCur *cur);

bool smith_lang_enum_return(SmithLang *lang, const SmithHost *host);

void smith_lang_enum_reset(void);

bool smith_lang_enum_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                   SmithLangFrame *frame);

bool smith_lang_try_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                  SmithLangFrame *frame);

extern bool smith_lang_eshape_start;

void smith_lang_enum_aside(bool save);

extern uint32_t smith_lang_enum_return_dot;

void smith_lang_instance_define_enums(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                             const SmithLangInstance *instance, uint32_t place, uint32_t instance_id);

uint32_t smith_lang_eshape_put_call(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                           const SmithLangInstance *instance, uint32_t at, uint32_t end, uint32_t place,
                                           uint32_t instance_id, bool define);

size_t smith_lang_eshape_lines(const SmithLang *lang, char *out, size_t capacity, size_t used);

void smith_lang_eshape_import(const char *text, uint32_t length, const char *bind, size_t bind_length);

void smith_lang_eshape_builtins(SmithLang *lang);

bool smith_lang_eshape_named(const SmithToken *name);

bool smith_lang_enum_function(SmithLang *lang, const SmithHost *host, const SmithToken *token);

size_t smith_lang_enum_pragma(SmithToken *out, size_t used, const SmithToken *like, const char *literal);

bool smith_lang_enum_token(SmithLang *lang, const SmithHost *host, const SmithToken *token);

bool smith_lang_enum_argument(SmithLang *lang, const SmithHost *host, const char *callee, size_t index,
                                     const SmithToken *tokens, size_t count, SmithLangSliceCur *cur);

