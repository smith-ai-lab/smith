// The targets a Makefile builds: the explicit ones, the pattern rules and make's built-in C rules applied to what needs
// them, with prerequisites and recipes. Included by smith.c after make_read.c.

constexpr size_t smith_make_nodes_max = 32768;
constexpr size_t smith_make_commands_max = 512;

typedef struct {
    const char *text; // expanded, without the leading @, - and +
    bool silent;
    bool ignore;
    SmithMakeWhere where;
} SmithMakeCommand;

typedef struct {
    const char *name;
    SmithMakeWhere where;
    const char **prerequisites;
    size_t prerequisite_count;
    const char **order_only;
    size_t order_count;
    const char *stem;
    int64_t rule; // the rule whose recipe it runs, or -1
    const char *guard;
    bool phony;
    bool has_recipe;
    bool built_in;   // its recipe is make's own
    const char *cwd; // the folder its commands run in, when it comes from the Makefile of another folder
    SmithMakeCommand *commands;
    size_t command_count;
    bool expanded;
} SmithMakeNode;

typedef struct {
    SmithMakeNode *nodes;
    size_t node_count;
    uint32_t *slots;
    size_t builtin_first; // the first built-in pattern rule
    const char **missing;
    size_t missing_count;
} SmithMakeGraph;

static SmithMakeGraph smith_make_graph;

// make's built-in rules for C and C++: the recipe, with the variables make's own text uses.
typedef struct {
    const char *target;
    const char *prerequisite;
    const char *recipe;
} SmithMakeBuiltin;

static const SmithMakeBuiltin smith_make_builtins[] = {
    {"%.o", "%.c", "$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<"},
    {"%.o", "%.cc", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<"},
    {"%.o", "%.cpp", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<"},
    {"%.o", "%.cxx", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<"},
    {"%.o", "%.C", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<"},
    {"%", "%.o", "$(CC) $(LDFLAGS) $(TARGET_ARCH) $^ $(LOADLIBES) $(LDLIBS) -o $@"},
    {"%", "%.c", "$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) $^ $(LOADLIBES) $(LDLIBS) -o $@"},
    {"%", "%.cc", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) $^ $(LOADLIBES) $(LDLIBS) -o $@"},
    {"%", "%.cpp", "$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) $^ $(LOADLIBES) $(LDLIBS) -o $@"},
};

static void smith_make_graph_init(void) {
    SmithMakeGraph *g = &smith_make_graph;
    SmithMakeReader *r = &smith_make_reader;
    g->nodes = smith_make_alloc(smith_make_nodes_max * sizeof(SmithMakeNode));
    g->slots = smith_make_alloc(smith_make_nodes_max * 2 * sizeof(uint32_t));
    g->missing = smith_make_new_list(4096);
    g->builtin_first = r->rule_count;
    for (size_t i = 0; i < sizeof smith_make_builtins / sizeof smith_make_builtins[0]; i++) {
        const SmithMakeBuiltin *builtin = &smith_make_builtins[i];
        SMITH_ASSERT(r->rule_count < smith_make_rules_max && r->recipe_line_count < smith_make_recipe_lines_max);
        SmithMakeRule *rule = &r->rules[r->rule_count];
        *rule = (SmithMakeRule){.pattern = true, .recipe_from = -1, .recipe_first = r->recipe_line_count};
        rule->targets = smith_make_new_list(2);
        rule->targets[0] = builtin->target;
        rule->target_count = 1;
        rule->prerequisites = smith_make_new_list(2);
        rule->prerequisites[0] = builtin->prerequisite;
        rule->prerequisite_count = 1;
        rule->recipe_count = 1;
        r->recipe_lines[r->recipe_line_count++] = (SmithMakeRecipeLine){builtin->recipe, {"(built in)", 0}};
        r->patterns[r->pattern_count++] = (uint32_t)r->rule_count++;
    }
}

// ---- Nodes

static SmithMakeNode *smith_make_node_find(const char *name) {
    SmithMakeGraph *g = &smith_make_graph;
    uint32_t mask = (uint32_t)(smith_make_nodes_max * 2 - 1);
    uint32_t at = smith_make_hash(name, strlen(name)) & mask;
    for (size_t probes = 0; probes < smith_make_nodes_max * 2; probes++) {
        uint32_t slot = g->slots[at];
        if (slot == 0) return nullptr;
        if (strcmp(g->nodes[slot - 1].name, name) == 0) return &g->nodes[slot - 1];
        at = (at + 1) & mask;
    }
    return nullptr;
}

static SmithMakeNode *smith_make_node_new(const char *name) {
    SmithMakeGraph *g = &smith_make_graph;
    if (g->node_count >= smith_make_nodes_max) smith_die(smith_exit_usage, "make: too many targets", nullptr);
    uint32_t mask = (uint32_t)(smith_make_nodes_max * 2 - 1);
    uint32_t at = smith_make_hash(name, strlen(name)) & mask;
    while (g->slots[at] != 0)
        at = (at + 1) & mask;
    SmithMakeNode *node = &g->nodes[g->node_count];
    *node = (SmithMakeNode){.name = name, .rule = -1};
    g->slots[at] = (uint32_t)++g->node_count;
    return node;
}

// ---- Pattern rules

typedef bool (*SmithMakeCheck)(const char *name);

static const SmithMakeRule *smith_make_recipe_rule(const SmithMakeRule *rule) {
    if (rule->recipe_from < 0) return rule;
    return &smith_make_reader.rules[rule->recipe_from];
}

// A file that is there, or a target some rule names.
static bool smith_make_check_exists(const char *name) {
    const SmithMakeTarget *target = smith_make_target_find(name);
    if (target != nullptr && target->first != 0) {
        const SmithMakeReader *r = &smith_make_reader;
        for (uint32_t ref = target->first; ref != 0; ref = r->refs[ref - 1].next) {
            if (smith_make_recipe_rule(&r->rules[r->refs[ref - 1].rule])->recipe_count > 0) return true;
        }
    }
    if (target != nullptr && target->phony) return true;
    return smith_make_exists(name);
}

typedef struct {
    const SmithMakeRule *rule;
    const char *stem;
    size_t stem_length;
} SmithMakePick;

static bool smith_make_prerequisites_ok(const SmithMakeRule *rule, const char *stem, size_t stem_length,
                                        const char *name, SmithMakeCheck check) {
    for (size_t i = 0; i < rule->prerequisite_count; i++) {
        const char *prerequisite =
            smith_make_substitute(rule->prerequisites[i], strlen(rule->prerequisites[i]), stem, stem_length);
        if (strcmp(prerequisite, name) == 0 || !check(prerequisite)) return false;
    }
    return true;
}

// The pattern rule for name with the shortest stem (the first defined on a tie) whose prerequisites pass `check`.
static bool smith_make_pattern_pick(const char *name, SmithMakeCheck check, SmithMakePick *pick) {
    const SmithMakeReader *r = &smith_make_reader;
    bool found = false;
    for (size_t i = 0; i < r->pattern_count; i++) {
        const SmithMakeRule *rule = &r->rules[r->patterns[i]];
        for (size_t t = 0; t < rule->target_count; t++) {
            const char *stem = nullptr;
            size_t stem_length = 0;
            if (!smith_make_match(rule->targets[t], strlen(rule->targets[t]), name, strlen(name), &stem, &stem_length))
                continue;
            if (found && stem_length >= pick->stem_length) continue;
            if (!smith_make_prerequisites_ok(rule, stem, stem_length, name, check)) continue;
            *pick = (SmithMakePick){rule, stem, stem_length};
            found = true;
        }
    }
    return found;
}

// Whether a name can be made, one rule deep, then two (a source from a generator, an object from that source).
static bool smith_make_check_one(const char *name) {
    SmithMakePick pick;
    return smith_make_check_exists(name) || smith_make_pattern_pick(name, smith_make_check_exists, &pick);
}

static bool smith_make_check_two(const char *name) {
    SmithMakePick pick;
    return smith_make_check_one(name) || smith_make_pattern_pick(name, smith_make_check_one, &pick);
}

// ---- Resolving a name

static const char **smith_make_substituted(const char *const *list, size_t count, const char *stem,
                                           size_t stem_length) {
    const char **out = smith_make_new_list((count + 1));
    for (size_t i = 0; i < count; i++)
        out[i] = smith_make_substitute(list[i], strlen(list[i]), stem, stem_length);
    out[count] = nullptr;
    return out;
}

static void smith_make_append(const char ***list, size_t *count, const char *const *more, size_t more_count) {
    const char **joined = smith_make_new_list((*count + more_count + 1));
    for (size_t i = 0; i < *count; i++)
        joined[i] = (*list)[i];
    for (size_t i = 0; i < more_count; i++)
        joined[*count + i] = more[i];
    *count += more_count;
    joined[*count] = nullptr;
    *list = joined;
}

// The explicit rules of a target: the prerequisites of all, the recipe of the last that has one.
static bool smith_make_same_guard(const char *a, const char *b) {
    return (a == nullptr && b == nullptr) || (a != nullptr && b != nullptr && strcmp(a, b) == 0);
}

static void smith_make_node_explicit(SmithMakeNode *node, const SmithMakeTarget *target) {
    const SmithMakeReader *r = &smith_make_reader;
    node->phony = target->phony;
    node->where = target->where;
    for (uint32_t ref = target->first; ref != 0; ref = r->refs[ref - 1].next) {
        const SmithMakeRule *rule = &r->rules[r->refs[ref - 1].rule];
        smith_make_append(&node->prerequisites, &node->prerequisite_count, rule->prerequisites,
                          rule->prerequisite_count);
        smith_make_append(&node->order_only, &node->order_count, rule->order_only, rule->order_only_count);
        if (rule->stem != nullptr) node->stem = rule->stem;
        if (smith_make_recipe_rule(rule)->recipe_count > 0) {
            if (node->has_recipe && !smith_make_same_guard(node->guard, rule->guard))
                smith_make_report_at(rule->where,
                                     "%s has recipes under different tests of the platform or an "
                                     "option; only the last is kept",
                                     node->name);
            node->guard = rule->guard;
            node->rule = (int64_t)(smith_make_recipe_rule(rule) - r->rules);
            node->where = rule->where;
            node->has_recipe = true;
        } else if (!node->has_recipe) {
            node->where = rule->where;
        }
    }
}

static void smith_make_node_implicit(SmithMakeNode *node, const SmithMakePick *pick) {
    const SmithMakeReader *r = &smith_make_reader;
    const SmithMakeRule *rule = pick->rule;
    smith_make_append(
        &node->prerequisites, &node->prerequisite_count,
        smith_make_substituted(rule->prerequisites, rule->prerequisite_count, pick->stem, pick->stem_length),
        rule->prerequisite_count);
    smith_make_append(&node->order_only, &node->order_count,
                      smith_make_substituted(rule->order_only, rule->order_only_count, pick->stem, pick->stem_length),
                      rule->order_only_count);
    node->stem = smith_make_dup(pick->stem, pick->stem_length);
    node->rule = (int64_t)(smith_make_recipe_rule(rule) - r->rules);
    node->has_recipe = true;
    node->built_in = (size_t)node->rule >= smith_make_graph.builtin_first;
    if (rule->guard != nullptr && node->guard == nullptr) node->guard = rule->guard;
    if (!node->built_in)
        node->where = rule->where;
    else if (node->where.file == nullptr)
        node->where = (SmithMakeWhere){"(make's built-in rule)", 0};
}

// The node of a name, made on first use. A name nothing makes and no file is has no node.
static SmithMakeNode *smith_make_node_for(const char *name) {
    SmithMakeNode *found = smith_make_node_find(name);
    if (found != nullptr) return found;
    const SmithMakeTarget *target = smith_make_target_find(name);
    SmithMakePick pick;
    bool implicit = false;
    bool explicit_recipe = false;
    if (target != nullptr) {
        const SmithMakeReader *r = &smith_make_reader;
        for (uint32_t ref = target->first; ref != 0; ref = r->refs[ref - 1].next) {
            if (smith_make_recipe_rule(&r->rules[r->refs[ref - 1].rule])->recipe_count > 0) explicit_recipe = true;
        }
    }
    if (!explicit_recipe && !(target != nullptr && target->phony))
        implicit = smith_make_pattern_pick(name, smith_make_check_two, &pick);
    if (target == nullptr && !implicit) return nullptr;
    SmithMakeNode *node = smith_make_node_new(name);
    if (target != nullptr) smith_make_node_explicit(node, target);
    if (implicit) smith_make_node_implicit(node, &pick);
    return node;
}

// ---- The graph

static void smith_make_note_missing(const char *name) {
    SmithMakeGraph *g = &smith_make_graph;
    for (size_t i = 0; i < g->missing_count; i++) {
        if (strcmp(g->missing[i], name) == 0) return;
    }
    if (g->missing_count < 4096) g->missing[g->missing_count++] = name;
}

// Every target the Makefile has, and what they need, as nodes.
static void smith_make_graph_build(void) {
    SmithMakeGraph *g = &smith_make_graph;
    const SmithMakeReader *r = &smith_make_reader;
    smith_make_graph_init();
    for (size_t i = 0; i < r->target_count; i++) {
        const SmithMakeTarget *target = &r->targets[i];
        if (smith_make_is_special(target->name) || smith_make_is_suffix_rule(target->name)) continue;
        smith_make_node_for(target->name);
    }
    for (size_t at = 0; at < g->node_count; at++) { // g->node_count grows as prerequisites are found
        for (size_t pass = 0; pass < 2; pass++) {
            const SmithMakeNode *node = &g->nodes[at];
            const char *const *list = pass == 0 ? node->prerequisites : node->order_only;
            size_t count = pass == 0 ? node->prerequisite_count : node->order_count;
            for (size_t k = 0; k < count; k++) {
                const char *name = list[k];
                if (smith_make_node_for(name) == nullptr && !smith_make_exists(name) && strcmp(name, "FORCE") != 0)
                    smith_make_note_missing(name);
            }
        }
    }
}

// ---- Recipes

static const char *smith_make_join_unique(const char *const *list, size_t count, bool unique) {
    SmithMakeBuffer out;
    smith_make_buffer_init(&out, 65536 + count * 256);
    for (size_t i = 0; i < count; i++) {
        bool seen = false;
        for (size_t k = 0; unique && k < i; k++) {
            if (strcmp(list[k], list[i]) == 0) seen = true;
        }
        if (seen) continue;
        if (out.length > 0) smith_make_put_char(&out, ' ');
        smith_make_put_text(&out, list[i]);
    }
    return smith_make_take(&out, 0);
}

static void smith_make_set_automatic(const SmithMakeNode *node) {
    SmithMakeAuto *a = &smith_make_auto;
    a->active = true;
    a->target = node->name;
    a->first = node->prerequisite_count > 0 ? node->prerequisites[0] : "";
    a->prerequisites = smith_make_join_unique(node->prerequisites, node->prerequisite_count, true);
    a->all = smith_make_join_unique(node->prerequisites, node->prerequisite_count, false);
    a->newer = a->prerequisites;
    a->order_only = smith_make_join_unique(node->order_only, node->order_count, true);
    a->stem = node->stem != nullptr ? node->stem : "";
}

// Expands a node's recipe lines, with its automatic variables. Idempotent.
static void smith_make_node_expand(SmithMakeNode *node) {
    if (node->expanded) return;
    node->expanded = true;
    if (node->rule < 0) return;
    const SmithMakeReader *r = &smith_make_reader;
    const SmithMakeRule *rule = &r->rules[node->rule];
    if (rule->recipe_count > smith_make_commands_max)
        smith_die(smith_exit_usage, "make: a recipe has too many lines", node->name);
    node->commands = smith_make_alloc((rule->recipe_count + 1) * sizeof(SmithMakeCommand));
    smith_make_set_automatic(node);
    for (size_t i = 0; i < rule->recipe_count; i++) {
        const SmithMakeRecipeLine *line = &r->recipe_lines[rule->recipe_first + i];
        const char *text = line->text;
        SmithMakeCommand *command = &node->commands[node->command_count++];
        command->where = line->where;
        smith_make.here = line->where;
        while (*text == '@' || *text == '-' || *text == '+' || smith_make_blank(*text)) {
            if (*text == '@') command->silent = true;
            if (*text == '-') command->ignore = true;
            text++;
        }
        smith_make.no_knobs = node->built_in;
        command->text = smith_make_expand_text(text, true);
        smith_make.no_knobs = false;
    }
    smith_make_auto.active = false;
}
