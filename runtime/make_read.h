// Generated split of smith.c (#2685): what runtime/make_read.c shares with the other files.
#pragma once

#include "core.h"
#include "make.h"

constexpr size_t smith_make_includes_max = 16;

constexpr size_t smith_make_conds_max = 64;

constexpr size_t smith_make_rules_max = 16384;

constexpr size_t smith_make_recipe_lines_max = 262144;

constexpr size_t smith_make_log_max = 8192;

typedef struct {
    const char *name;
    char *text;
    size_t length;
    size_t pos;
    uint32_t line;
    bool eval; // the text of an $(eval), not a file
} SmithMakeSource;

typedef struct {
    const char *text;
    SmithMakeWhere where;
} SmithMakeRecipeLine;

typedef struct {
    SmithMakeWhere where;
    const char **targets;
    size_t target_count;
    const char **prerequisites;
    size_t prerequisite_count;
    const char **order_only;
    size_t order_only_count;
    size_t recipe_first;
    size_t recipe_count;
    const char *guard; // a C condition, or nullptr: the rule is under a test of the platform or an option
    bool pattern;
    bool from_eval;      // made by an $(eval): the names of its variables were made by an expansion
    const char *stem;    // a static pattern rule's stem for its one target
    int64_t recipe_from; // the rule whose recipe lines these are, when a line made several rules; else -1
} SmithMakeRule;

typedef struct {
    uint32_t rule;
    uint32_t next; // index + 1 of the next reference, 0 for none
} SmithMakeRef;

typedef struct {
    const char *name;
    uint32_t first; // index + 1 into refs
    uint32_t last;
    bool phony;
    SmithMakeWhere where;
} SmithMakeTarget;

typedef struct {
    SmithMakeVar *var;
    SmithMakeVar state;
} SmithMakeLogEntry;

typedef struct {
    bool outer;   // the region around the conditional is read
    bool active;  // the branch being read is read
    bool taken;   // a branch was read already
    bool dynamic; // build.c decides
    bool chained; // an `else ifeq`: its endif is the parent's too
    bool in_else;
    const char *test;
    size_t log_start;
    size_t results_start;
    size_t results_end;
} SmithMakeCond;

typedef struct {
    SmithMakeSource sources[smith_make_includes_max];
    size_t source_count;
    SmithMakeCond conds[smith_make_conds_max];
    size_t cond_count;
    size_t dynamic_count;
    SmithMakeLogEntry log[smith_make_log_max];
    size_t log_count;
    SmithMakeLogEntry results[smith_make_log_max];
    size_t result_count;
    SmithMakeRule *rules;
    size_t rule_count;
    SmithMakeRef *refs;
    size_t ref_count;
    SmithMakeTarget *targets;
    size_t target_count;
    uint32_t *target_slots;
    uint32_t *patterns; // rules that are pattern rules, in order
    size_t pattern_count;
    SmithMakeRecipeLine *recipe_lines;
    size_t recipe_line_count;
    bool in_rule;
    int64_t current_rule; // -1 for none
    const char *default_goal;
    const char **generated_includes;
    size_t generated_count;
} SmithMakeReader;

extern SmithMakeReader smith_make_reader;

extern SmithMakeBuffer smith_make_joined;

extern SmithMakeBuffer smith_make_define_body;

SmithMakeTarget *smith_make_target_find(const char *name);

bool smith_make_is_special(const char *name);

bool smith_make_is_suffix_rule(const char *name);

bool smith_make_exists(const char *path);

bool smith_make_has_suffix(const char *text, const char *suffix);

void smith_make_read(const char *path);

