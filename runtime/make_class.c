// What each target of the Makefile becomes: dropped, an alias, an object, a program, a step or a task. Included by
// smith.c after make_cc.c.

typedef enum {
    SmithMakeKind_Drop,
    SmithMakeKind_Alias,
    SmithMakeKind_Object,
    SmithMakeKind_Program,
    SmithMakeKind_Step,
    SmithMakeKind_Task,
} SmithMakeKind;

typedef struct {
    SmithMakeKind kind;
    const char *c_name;    // object_3, program_1, step_7
    const char *emit_name; // the name of a task or an alias in build.c
    size_t *kept;          // the commands that do something
    size_t kept_count;
    const char **prerequisites; // without FORCE, with the goals of a forwarded $(MAKE)
    size_t prerequisite_count;
    bool always;
    bool test;
    bool directory; // made by `mkdir -p`: smith makes the folder of an output itself
    const char *note;
} SmithMakeInfo;

static SmithMakeInfo *smith_make_infos;
static size_t smith_make_object_count;
static size_t smith_make_program_count;
static size_t smith_make_step_count;

static SmithMakeInfo *smith_make_info_of(const SmithMakeNode *node) {
    return &smith_make_infos[node - smith_make_graph.nodes];
}

// ---- Verbs

static const char *const smith_make_verbs[] = {"fmt", "lint", "docs", "prove", "check", "run", "test"};

static bool smith_make_is_verb(const char *name) {
    for (size_t i = 0; i < sizeof smith_make_verbs / sizeof smith_make_verbs[0]; i++) {
        if (strcmp(name, smith_make_verbs[i]) == 0) return true;
    }
    return false;
}

// A task or alias name build.c allows: letters, digits and _ . / - only.
static bool smith_make_task_name_ok(const char *name) {
    if (name[0] == '\0' || name[0] == '/' || name[0] == '-') return false;
    for (const char *c = name; *c != '\0'; c++) {
        bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' ||
                  *c == '.' || *c == '/' || *c == '-';
        if (!ok) return false;
    }
    size_t length = strlen(name);
    return name[length - 1] != '/' && strstr(name, "//") == nullptr && strstr(name, "/./") == nullptr &&
           strstr(name, "/../") == nullptr;
}

// ---- Commands that do nothing smith needs

static bool smith_make_path_prefix(const char *prefix, const char *path) {
    size_t length = strlen(prefix);
    while (length > 1 && prefix[length - 1] == '/')
        length--;
    return strncmp(prefix, path, length) == 0 && (path[length] == '/' || path[length] == '\0');
}

// An empty command, `:`, `true`, or `mkdir -p` of the target or the folders on its way. *directory is set when the
// target itself is a folder.
static bool smith_make_noise(const SmithMakeNode *node, const SmithMakeCommand *command, bool *directory) {
    SmithMakeArgv argv;
    const char *text = smith_make_trim(smith_make_unmark(command->text));
    if (text[0] == '\0') return true;
    if (!smith_make_lex(text, true, &argv) || argv.count == 0) return false;
    const char *program = argv.args[0].text;
    if (argv.count == 1 && (strcmp(program, ":") == 0 || strcmp(program, "true") == 0)) return true;
    if (strcmp(program, "mkdir") != 0 || argv.count < 3 || strcmp(argv.args[1].text, "-p") != 0) return false;
    for (size_t i = 2; i < argv.count; i++) {
        const char *folder = argv.args[i].text;
        if (!smith_make_path_prefix(folder, node->name)) return false;
        if (strcmp(folder, node->name) == 0) *directory = true;
    }
    return true;
}

// ---- $(MAKE)

// `make [flags] [goals]` run by the Makefile on itself: its goals, appended to *goals. False if the command is another
// make (another folder or file) that this reading does not follow.
static bool smith_make_forward(const SmithMakeCommand *command, const char ***goals, size_t *count) {
    SmithMakeArgv argv;
    if (!smith_make_lex(smith_make_unmark(command->text), true, &argv) || argv.count == 0) return false;
    if (strcmp(smith_make_basename(argv.args[0].text), "make") != 0) return false;
    for (size_t i = 1; i < argv.count; i++) {
        const char *word = argv.args[i].text;
        if (strcmp(word, "-C") == 0 || strcmp(word, "-f") == 0 || strncmp(word, "--directory", 11) == 0 ||
            strncmp(word, "--file", 6) == 0 || strncmp(word, "-C", 2) == 0 || strncmp(word, "-f", 2) == 0)
            return false;
        if (strchr(word, '=') != nullptr) {
            smith_make_report_at(command->where, "a variable on a $(MAKE) command line is not supported: %s", word);
            continue;
        }
        if (word[0] == '-') {
            if (strcmp(word, "-j") == 0 && i + 1 < argv.count && argv.args[i + 1].text[0] >= '0' &&
                argv.args[i + 1].text[0] <= '9')
                i++;
            continue;
        }
        smith_make_append(goals, count, &word, 1);
    }
    return true;
}

// ---- The pass

// Whether the only command runs a program that a prerequisite makes (`./game`, `build/game`).
static bool smith_make_runs_program(const SmithMakeNode *node, const SmithMakeInfo *info) {
    SmithMakeArgv argv;
    if (info->kept_count != 1 || !smith_make_lex(node->commands[info->kept[0]].text, true, &argv) || argv.count == 0)
        return false;
    const char *program = argv.args[0].text;
    if (strncmp(program, "./", 2) == 0) program += 2;
    for (size_t i = 0; i < node->prerequisite_count; i++) {
        if (strcmp(program, node->prerequisites[i]) == 0) return true;
    }
    return false;
}

static void smith_make_info_commands(SmithMakeNode *node, SmithMakeInfo *info) {
    bool directory = false;
    bool any_real = false;
    info->kept = smith_make_alloc((node->command_count + 1) * sizeof(size_t));
    for (size_t j = 0; j < node->command_count; j++) {
        const SmithMakeCommand *command = &node->commands[j];
        if (smith_make_noise(node, command, &directory)) continue;
        if (smith_make_forward(command, &info->prerequisites, &info->prerequisite_count)) {
            if (info->prerequisite_count == 0) smith_make_report_at(command->where, "$(MAKE) with no goal");
            continue;
        }
        info->kept[info->kept_count++] = j;
        any_real = true;
    }
    info->directory = directory && !any_real;
}

static void smith_make_info_node(SmithMakeNode *node, SmithMakeInfo *info) {
    smith_make_node_expand(node);
    info->prerequisites = smith_make_new_list((node->prerequisite_count + 1));
    for (size_t k = 0; k < node->prerequisite_count; k++) {
        if (strcmp(node->prerequisites[k], "FORCE") == 0) {
            info->always = true;
        } else {
            info->prerequisites[info->prerequisite_count++] = node->prerequisites[k];
        }
    }
    if (strcmp(node->name, "clean") == 0) {
        info->kind = SmithMakeKind_Drop;
        info->note = "`clean` is dropped: smith build -t clean removes what the build made";
        return;
    }
    if (strcmp(node->name, "FORCE") == 0 && !node->has_recipe) {
        info->kind = SmithMakeKind_Drop;
        return;
    }
    if (!node->has_recipe) {
        info->kind = node->phony ? SmithMakeKind_Alias : SmithMakeKind_Drop;
        return;
    }
    smith_make_info_commands(node, info);
    if (node->phony && strcmp(node->name, "run") == 0 && smith_make_runs_program(node, info)) {
        info->kind = SmithMakeKind_Drop;
        info->note = "`run` is `smith run`, which runs the program that smith_default names";
    } else if (info->directory) {
        info->kind = SmithMakeKind_Drop;
        info->note = "a folder made by mkdir -p: smith makes the folder of an output itself";
    } else if (info->kept_count == 0) {
        info->kind = SmithMakeKind_Alias;
    } else {
        info->kind = node->phony ? SmithMakeKind_Task : SmithMakeKind_Step;
    }
}

// The name a task or alias has in build.c: a verb's name is taken, so `test` is make-test.
static const char *smith_make_emit_name(const SmithMakeNode *node, SmithMakeInfo *info) {
    if (smith_make_is_verb(node->name)) {
        info->note = smith_make_format("`%s` is a verb in smith (`smith %s` does it for a project), so the Makefile's "
                                       "target of that name is the task make-%s",
                                       node->name, node->name, node->name);
        return smith_make_format("make-%s", node->name);
    }
    return node->name;
}

static bool smith_make_known_object(const char *output, const SmithMakeNode **node) {
    const SmithMakeNode *found = smith_make_node_find(output);
    if (found == nullptr || smith_make_info_of(found)->kind != SmithMakeKind_Object) return false;
    *node = found;
    return true;
}

// A step whose one command compiles one source into the node's target becomes an object.
static void smith_make_try_object(SmithMakeNode *node, SmithMakeInfo *info) {
    if (info->kind != SmithMakeKind_Step || info->kept_count != 1 || node->guard != nullptr) return;
    SmithMakeArgv argv;
    SmithMakeCc cc;
    if (!smith_make_lex(node->commands[info->kept[0]].text, true, &argv) || !smith_make_parse_cc(&argv, &cc)) return;
    if (!smith_make_is_compile(&cc) || strcmp(argv.args[cc.output].text, node->name) != 0) return;
    info->kind = SmithMakeKind_Object;
    info->c_name = smith_make_format("object_%zu", ++smith_make_object_count);
}

// The prerequisites of a link that its command does not name make it a step, not a program.
static bool smith_make_link_covers(const SmithMakeNode *node, const SmithMakeInfo *info, const SmithMakeArgv *argv,
                                   const SmithMakeCc *cc) {
    if (node->order_count > 0) return false;
    for (size_t k = 0; k < info->prerequisite_count; k++) {
        bool found = false;
        for (size_t i = 0; i < cc->object_count && !found; i++)
            found = strcmp(argv->args[cc->objects[i]].text, info->prerequisites[k]) == 0;
        for (size_t i = 0; i < cc->other_count && !found; i++)
            found = strcmp(argv->args[cc->others[i]].text, info->prerequisites[k]) == 0;
        if (!found) return false;
    }
    return true;
}

static void smith_make_try_program(SmithMakeNode *node, SmithMakeInfo *info) {
    if (info->kind != SmithMakeKind_Step || info->kept_count != 1 || node->guard != nullptr) return;
    SmithMakeArgv argv;
    SmithMakeCc cc;
    if (!smith_make_lex(node->commands[info->kept[0]].text, true, &argv) || !smith_make_parse_cc(&argv, &cc)) return;
    if (!smith_make_is_link(&cc) || strcmp(argv.args[cc.output].text, node->name) != 0) return;
    for (size_t i = 0; i < cc.object_count; i++) {
        const SmithMakeNode *object = nullptr;
        if (!smith_make_known_object(argv.args[cc.objects[i]].text, &object)) return;
    }
    if (!smith_make_link_covers(node, info, &argv, &cc)) return;
    info->kind = SmithMakeKind_Program;
    info->c_name = smith_make_format("program_%zu", ++smith_make_program_count);
}

// Decides what every node becomes.
static void smith_make_classify(void) {
    SmithMakeGraph *g = &smith_make_graph;
    smith_make_infos = smith_make_alloc((g->node_count + 1) * sizeof(SmithMakeInfo));
    for (size_t i = 0; i < g->node_count; i++)
        smith_make_info_node(&g->nodes[i], &smith_make_infos[i]);
    for (size_t i = 0; i < g->node_count; i++)
        smith_make_try_object(&g->nodes[i], &smith_make_infos[i]);
    for (size_t i = 0; i < g->node_count; i++)
        smith_make_try_program(&g->nodes[i], &smith_make_infos[i]);
    for (size_t i = 0; i < g->node_count; i++) {
        SmithMakeInfo *info = &smith_make_infos[i];
        if (info->kind == SmithMakeKind_Step) info->c_name = smith_make_format("step_%zu", ++smith_make_step_count);
        if (info->kind == SmithMakeKind_Task) info->c_name = smith_make_format("task_%zu", i + 1);
        info->emit_name = smith_make_emit_name(&g->nodes[i], info);
        if ((info->kind == SmithMakeKind_Task || info->kind == SmithMakeKind_Alias) &&
            !smith_make_task_name_ok(info->emit_name)) {
            smith_make_report_at(g->nodes[i].where,
                                 "the target %s cannot be a task: a task name holds only letters, "
                                 "digits and _ . / -",
                                 g->nodes[i].name);
            info->kind = SmithMakeKind_Drop;
        }
    }
}

// ---- The names a step lists as inputs

constexpr size_t smith_make_inputs_max = 65536;

// What a list of prerequisites means to build.c: files, the outputs of steps and the names of tasks; a target that only
// groups others (no recipe, no file) is replaced by what it groups, and a folder or FORCE is dropped.
static const char **smith_make_inputs(const char *const *names, size_t count, size_t *out_count) {
    const char **work = smith_make_new_list(smith_make_inputs_max);
    const char **result = smith_make_new_list((smith_make_inputs_max + 1));
    size_t depth = 0;
    size_t used = 0;
    for (size_t i = count; i > 0; i--)
        work[depth++] = names[i - 1];
    for (size_t steps = 0; depth > 0 && steps < smith_make_inputs_max * 4; steps++) {
        const char *name = work[--depth];
        const SmithMakeNode *node = smith_make_node_find(name);
        if (node == nullptr) {
            if (strcmp(name, "FORCE") != 0 && used < smith_make_inputs_max) result[used++] = name;
            continue;
        }
        const SmithMakeInfo *info = smith_make_info_of(node);
        if (info->kind == SmithMakeKind_Drop && !node->has_recipe && !smith_make_exists(name)) {
            for (size_t k = info->prerequisite_count; k > 0 && depth < smith_make_inputs_max; k--)
                work[depth++] = info->prerequisites[k - 1];
            continue;
        }
        if (info->kind == SmithMakeKind_Drop && info->directory) continue;
        if (info->kind == SmithMakeKind_Drop && smith_make_exists(name)) {
            if (used < smith_make_inputs_max) result[used++] = name;
            continue;
        }
        if (info->kind == SmithMakeKind_Drop) continue;
        bool named = info->kind == SmithMakeKind_Alias || info->kind == SmithMakeKind_Task;
        if (used < smith_make_inputs_max) result[used++] = named ? info->emit_name : name;
    }
    result[used] = nullptr;
    *out_count = used;
    return result;
}
