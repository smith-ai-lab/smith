// Generated split of smith.c (#2685): what runtime/lang_decl.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_main.h"

// What came before a token, as far as lowering it is concerned.
typedef struct {
    bool after_member; // after . or ->: a field, never a name in scope
    bool after_tag;    // after struct, union or enum
    char tag_word;     // 's', 'u' or 'e': which, after a tag word
    bool typeish;      // the tokens before end a type, so a name now is a declarator
    bool lazy;         // typeish is not known yet: ask when it matters (it can take a lookup)
    char before;       // the first character of the token before, if it was a punctuator
} SmithLangContext;

typedef struct {
    bool replaced;
    bool is_type;
    bool expand; // the text is a macro of the compiler's (__UINT32_TYPE__): let it expand
    const char *text;
    size_t length;
    const char *header;
    size_t header_length;
} SmithLangLowered;

uint32_t smith_lang_hash(const char *text, size_t length);

SmithLangName *smith_lang_name_find(SmithLang *lang, const char *text, size_t length);

bool smith_lang_is_param(const SmithLang *lang, const char *name, size_t length);

bool smith_lang_param_here(SmithLang *lang, const SmithLangName *known, const char *name, size_t length);

SmithLangBinding *smith_lang_binding_find(SmithLang *lang, const char *text, size_t length);

bool smith_lang_follows_declarator(const SmithToken *next);

bool smith_lang_declares_name(const SmithLang *lang, const SmithToken *next, bool typeish);

SmithLangContext smith_lang_context(SmithLang *lang, const SmithHost *host, bool lazy);

void smith_lang_track(SmithLang *lang, const SmithToken *token, bool is_type);

const char *smith_lang_declare(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                      uint32_t location, SmithLangNameKind kind, bool exported);

bool smith_lang_is_own_word(SmithLang *lang, const SmithHost *host, const SmithToken *word);

void smith_lang_note_own_word(SmithLang *lang, const SmithToken *word);

bool smith_lang_namespace(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                 const SmithLangBinding *binding, SmithToken *result);

void smith_lang_tag_due(SmithLang *lang, const char *name, size_t length, char word, const char *spelling,
                               bool late);

SmithLangLowered smith_lang_lower_tag(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                             uint32_t location, const SmithToken **next, char word);

void smith_lang_check_c_name(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                    uint32_t location, bool *is_type);

SmithLangLowered smith_lang_lower(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                         uint32_t location, const SmithLangContext *context, const SmithToken *next);

