// `$(MAKE) -C dir goal`: the Makefile in dir is read too, in a state of its own, and the targets its goal needs join
// the graph of this one, named with the folder in front and run with `cd dir` first (the graph is flattened). Included
// by smith.c after make_cc.c.

constexpr size_t smith_make_subs_max = 32;

typedef struct {
    const char *path; // the folder, from the project's root
    const char **goals;
    size_t goal_count;
    const char *default_goal; // the goal of the folder's Makefile that no goal was given for, under the folder
} SmithMakeSub;

static SmithMakeSub smith_make_subs[smith_make_subs_max];
static size_t smith_make_sub_count;

// The state of the Makefile being translated, put away while a Makefile in another folder is read.
static SmithMake smith_make_saved_make;
static SmithMakeReader smith_make_saved_reader;
static SmithMakeGraph smith_make_saved_graph;
static SmithMakeAuto smith_make_saved_auto;

// path joined to a name: dir/name, leaving an absolute name and a name that starts with a hole as they are.
static const char *smith_make_under(const char *dir, const char *name) {
    if (dir == nullptr || dir[0] == '\0' || name[0] == '/' || name[0] == smith_make_hole_begin) return name;
    return smith_make_format("%s/%s", dir, name);
}

typedef enum { SmithMakeCall_None, SmithMakeCall_Self, SmithMakeCall_Folder, SmithMakeCall_Other } SmithMakeCall;

// A command that runs make: on this Makefile (its goals), in another folder (-C dir and its goals), or in another way.
static SmithMakeCall smith_make_parse_make(const SmithMakeCommand *command, const char **folder, const char ***goals,
                                           size_t *count) {
    SmithMakeArgv argv;
    if (!smith_make_lex(smith_make_unmark(command->text), true, &argv) || argv.count == 0) return SmithMakeCall_None;
    if (strcmp(smith_make_basename(argv.args[0].text), "make") != 0) return SmithMakeCall_None;
    *folder = nullptr;
    for (size_t i = 1; i < argv.count; i++) {
        const char *word = argv.args[i].text;
        if (strcmp(word, "-C") == 0 && i + 1 < argv.count) {
            *folder = argv.args[++i].text;
        } else if (strncmp(word, "--directory=", 12) == 0) {
            *folder = word + 12;
        } else if (strncmp(word, "-C", 2) == 0 && word[2] != '\0') {
            *folder = word + 2;
        } else if (strcmp(word, "-f") == 0 || strncmp(word, "--file", 6) == 0 || strncmp(word, "-f", 2) == 0) {
            return SmithMakeCall_Other;
        } else if (strchr(word, '=') != nullptr) {
            smith_make_report_at(command->where, "a variable on a $(MAKE) command line is not supported: %s", word);
        } else if (word[0] == '-') {
            if (strcmp(word, "-j") == 0 && i + 1 < argv.count && argv.args[i + 1].text[0] >= '0' &&
                argv.args[i + 1].text[0] <= '9')
                i++;
        } else {
            smith_make_append(goals, count, &word, 1);
        }
    }
    return *folder != nullptr ? SmithMakeCall_Folder : SmithMakeCall_Self;
}

// ---- Reading the other Makefile

static const char *smith_make_find_makefile(void) {
    static const char *const names[] = {"GNUmakefile", "makefile", "Makefile"};
    DIR *folder = opendir(".");
    if (folder == nullptr) return nullptr;
    const char *found = nullptr;
    for (size_t i = 0; i < sizeof names / sizeof names[0] && found == nullptr; i++) {
        rewinddir(folder);
        for (const struct dirent *entry = readdir(folder); entry != nullptr; entry = readdir(folder)) {
            if (strcmp(entry->d_name, names[i]) == 0) found = names[i];
        }
    }
    closedir(folder);
    return found;
}

// Puts the state of this Makefile away and starts an empty one that shares the memory, the holes and the reports.
static void smith_make_state_enter(void) {
    smith_make_saved_make = smith_make;
    smith_make_saved_reader = smith_make_reader;
    smith_make_saved_graph = smith_make_graph;
    smith_make_saved_auto = smith_make_auto;
    SmithMake *state = &smith_make;
    state->var_count = 0;
    memset(state->slots, 0, sizeof state->slots);
    state->order = 0;
    state->no_knobs = false;
    state->generated_include = false;
    state->eval_count = 0;
    smith_make_auto = (SmithMakeAuto){};
}

// Brings this Makefile back. What the other one made that belongs to the whole (memory, holes, reports) stays; its
// options join this one's variables, since a command line sets both.
static void smith_make_state_leave(void) {
    static SmithMakeVar options[256];
    static SmithMakeHole holes[smith_make_holes_max];
    static SmithMakeDiag diags[smith_make_diags_max];
    size_t option_count = 0;
    for (size_t i = 0; i < smith_make.var_count && option_count < 256; i++) {
        if (smith_make.vars[i].option) options[option_count++] = smith_make.vars[i];
    }
    memcpy(holes, smith_make.holes, sizeof holes);
    memcpy(diags, smith_make.diags, sizeof diags);
    size_t hole_count = smith_make.hole_count;
    size_t diag_count = smith_make.diag_count;
    size_t pool_used = smith_make.pool_used;
    smith_make = smith_make_saved_make;
    smith_make_reader = smith_make_saved_reader;
    smith_make_graph = smith_make_saved_graph;
    smith_make_auto = smith_make_saved_auto;
    smith_make.pool_used = pool_used;
    memcpy(smith_make.holes, holes, sizeof holes);
    smith_make.hole_count = hole_count;
    memcpy(smith_make.diags, diags, sizeof diags);
    smith_make.diag_count = diag_count;
    for (size_t i = 0; i < option_count; i++) {
        SmithMakeVar *var = smith_make_var_get(options[i].name, strlen(options[i].name));
        if (var->option) continue;
        *var = options[i];
        var->defined = true;
        var->knob = true;
    }
}

typedef struct {
    const char **names; // the targets the goals need, in the folder's own names
    size_t count;
} SmithMakeNeeded;

// The nodes the goals need: the goals and what they need, found in the other Makefile's graph.
static SmithMakeNeeded smith_make_needed(const char *const *goals, size_t goal_count) {
    const SmithMakeGraph *g = &smith_make_graph;
    SmithMakeNeeded needed = {smith_make_new_list(g->node_count + goal_count + 2), 0};
    const char *fallback = smith_make_reader.default_goal;
    if (goal_count == 0 && fallback != nullptr) {
        goals = &fallback;
        goal_count = 1;
    }
    for (size_t i = 0; i < goal_count; i++)
        needed.names[needed.count++] = goals[i];
    for (size_t at = 0; at < needed.count; at++) { // needed.count grows as prerequisites are found
        const SmithMakeNode *node = smith_make_node_find(needed.names[at]);
        if (node == nullptr) continue;
        for (size_t pass = 0; pass < 2; pass++) {
            const char *const *list = pass == 0 ? node->prerequisites : node->order_only;
            size_t count = pass == 0 ? node->prerequisite_count : node->order_count;
            for (size_t k = 0; k < count; k++) {
                bool seen = false;
                for (size_t s = 0; s < needed.count && !seen; s++)
                    seen = strcmp(needed.names[s], list[k]) == 0;
                if (!seen && needed.count < g->node_count + goal_count + 1) needed.names[needed.count++] = list[k];
            }
        }
    }
    return needed;
}

static const char **smith_make_under_list(const char *dir, const char *const *list, size_t count) {
    const char **out = smith_make_new_list(count + 1);
    for (size_t i = 0; i < count; i++)
        out[i] = smith_make_under(dir, list[i]);
    out[count] = nullptr;
    return out;
}

// Reads the Makefile of folder `path` (from the project's root) and returns copies of the nodes the goals need.
static size_t smith_make_read_folder(const char *path, const char *const *goals, size_t goal_count,
                                     SmithMakeNode **copies, const char **default_goal) {
    int home = open(".", O_RDONLY);
    if (home < 0 || chdir(path) != 0) {
        smith_make_report("$(MAKE) -C %s: cannot enter the folder", path);
        if (home >= 0) close(home);
        return 0;
    }
    const char *makefile = smith_make_find_makefile();
    if (makefile == nullptr) {
        smith_make_report("$(MAKE) -C %s: no Makefile there", path);
        fchdir(home);
        close(home);
        return 0;
    }
    smith_make_state_enter();
    smith_make_read(makefile);
    smith_make_graph_build();
    SmithMakeNeeded needed = smith_make_needed(goals, goal_count);
    SmithMakeNode *out = smith_make_alloc((needed.count + 1) * sizeof(SmithMakeNode));
    size_t made = 0;
    for (size_t i = 0; i < needed.count; i++) {
        SmithMakeNode *node = smith_make_node_find(needed.names[i]);
        if (node == nullptr) continue;
        smith_make_node_expand(node);
        SmithMakeNode copy = *node;
        copy.name = smith_make_under(path, node->name);
        copy.prerequisites = smith_make_under_list(path, node->prerequisites, node->prerequisite_count);
        copy.order_only = smith_make_under_list(path, node->order_only, node->order_count);
        copy.where.file = smith_make_format("%s/%s", path, node->where.file);
        copy.cwd = path;
        out[made++] = copy;
    }
    *default_goal =
        smith_make_reader.default_goal != nullptr ? smith_make_under(path, smith_make_reader.default_goal) : nullptr;
    smith_make_state_leave();
    fchdir(home);
    close(home);
    *copies = out;
    return made;
}

// Adds the nodes of a folder's Makefile that its goals need.
static void smith_make_import(SmithMakeSub *sub) {
    SmithMakeNode *copies = nullptr;
    size_t count = smith_make_read_folder(sub->path, sub->goals, sub->goal_count, &copies, &sub->default_goal);
    for (size_t i = 0; i < count; i++) {
        if (smith_make_node_find(copies[i].name) != nullptr) continue;
        SmithMakeNode *node = smith_make_node_new(copies[i].name);
        *node = copies[i];
    }
}

// The record of a folder already read for these goals.
static SmithMakeSub *smith_make_sub_find(const char *path, const char *const *goals, size_t goal_count) {
    for (size_t i = 0; i < smith_make_sub_count; i++) {
        SmithMakeSub *sub = &smith_make_subs[i];
        bool same = strcmp(sub->path, path) == 0 && sub->goal_count == goal_count;
        for (size_t k = 0; same && k < goal_count; k++)
            same = strcmp(sub->goals[k], goals[k]) == 0;
        if (same) return sub;
    }
    return nullptr;
}

// Every `make -C dir goal` among the commands, until there are none left that were not followed.
static void smith_make_import_all(void) {
    SmithMakeGraph *g = &smith_make_graph;
    for (size_t round = 0; round < 8; round++) {
        bool followed = false;
        for (size_t i = 0; i < g->node_count; i++) {
            SmithMakeNode *node = &g->nodes[i];
            smith_make_node_expand(node);
            for (size_t j = 0; j < node->command_count; j++) {
                const char *folder = nullptr;
                const char **goals = smith_make_new_list(1);
                size_t count = 0;
                if (smith_make_parse_make(&node->commands[j], &folder, &goals, &count) != SmithMakeCall_Folder)
                    continue;
                const char *path = smith_make_under(node->cwd, folder);
                if (smith_make_sub_find(path, goals, count) != nullptr) continue;
                if (smith_make_sub_count >= smith_make_subs_max) {
                    smith_make_report_at(node->commands[j].where,
                                         "more than %zu $(MAKE) -C sub-makes (the limit); %s is not followed",
                                         smith_make_subs_max, path);
                    continue;
                }
                size_t before = g->node_count;
                SmithMakeSub *sub = &smith_make_subs[smith_make_sub_count++];
                *sub = (SmithMakeSub){path, goals, count, nullptr};
                smith_make_import(sub);
                followed = followed || g->node_count != before;
            }
        }
        if (!followed) break;
    }
}
