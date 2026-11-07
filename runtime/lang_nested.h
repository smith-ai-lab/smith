// Generated split of smith.c (#2685): what runtime/lang_nested.c shares with the other files.
#pragma once

#include "core.h"
#include "lang.h"
#include "lang_generic.h"
#include "lang_main.h"

constexpr size_t smith_lang_nested_max = 8;

constexpr size_t smith_lang_nested_names_max = 16;

typedef struct {
    uint32_t start; // [start, end) of smith_lang_gt: `[](P) -> R { ... }`
    uint32_t end;
    uint32_t params_from, params_to; // the tokens between the parentheses
    bool has_type;
    uint32_t type_from, type_to; // `-> R`: the tokens of R
    uint32_t body_from, body_to; // the `{` and the `}`
    char mangled[smith_lang_name_max];
} SmithLangNested;

typedef struct {
    SmithLangNested list[smith_lang_nested_max];
    size_t count;
    char pack_tag[smith_lang_name_max];
    bool failed; // an error was reported: a lambda stands for `nullptr`, so that the body adds no second error
    // the probe of an inferred return type: a parameter of the lambda is a member of a compound literal
    char probe_tag[smith_lang_name_max];
    const SmithLangGenToken *probe_names[smith_lang_nested_names_max];
    size_t probe_count;
} SmithLangNestedState;

extern SmithLangNestedState smith_lang_nested;

bool smith_lang_nested_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay, SmithLangFrame *frame);

bool smith_lang_nested_member_at(const SmithLangReplay *replay, uint32_t at, uint32_t limit);

bool smith_lang_nested_write(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                             const SmithLangInstance *instance, uint32_t location, uint32_t id);

bool smith_lang_argl_slot(const SmithLangGeneric *generic, size_t slot, uint32_t *from, uint32_t *to);

bool smith_lang_argl_check(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic, size_t slot,
                           SmithLangSpan span, uint32_t place);

void smith_lang_argl_write(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                           const SmithLangInstance *instance, uint32_t location, uint32_t id);
