// The tools of `smith build -t <tool>`: the ones of ninja's that build.c's docs and tests rely on. Each reads the
// manifest (and, for deps, the logs) and prints; none runs a command.

typedef void (*SmithToolVisit)(SmithExec *exec, uint32_t edge, void *context);

// Calls visit for every edge that the targets need, each once, the inputs' edges first. With no targets: all of them.
static void smith_tool_walk(SmithExec *exec, const uint32_t *targets, size_t count, SmithToolVisit visit,
                            void *context) {
    SmithGraph *graph = &exec->graph;
    size_t capacity = graph->edge_count + count + 2;
    uint32_t *stack = smith_exec_push(exec, capacity * sizeof(uint32_t), alignof(uint32_t));
    size_t depth = 0;
    if (count == 0) {
        for (uint32_t number = 1; number <= graph->edge_count; number++)
            visit(exec, number, context);
        return;
    }
    for (size_t t = 0; t < count; t++) {
        uint32_t edge_number = graph->nodes[targets[t]].edge;
        if (edge_number == 0 || graph->edges[edge_number].scan != SmithScan_None) continue;
        graph->edges[edge_number].scan = SmithScan_Visiting;
        graph->edges[edge_number].cursor = 0;
        stack[depth++] = edge_number;
        for (size_t steps = 0; depth > 0; steps++) {
            if (steps > (graph->link_count + graph->edge_count + 2) * 2) smith_exec_fail("the walk did not end");
            SmithEdge *edge = &graph->edges[stack[depth - 1]];
            uint32_t total = edge->explicit_count + edge->implicit_count + edge->order_count;
            bool pushed = false;
            while (edge->cursor < total && !pushed) {
                uint32_t input_edge = graph->nodes[graph->ids[edge->inputs + edge->cursor++]].edge;
                if (input_edge != 0 && graph->edges[input_edge].scan == SmithScan_None) {
                    graph->edges[input_edge].scan = SmithScan_Visiting;
                    graph->edges[input_edge].cursor = 0;
                    stack[depth++] = input_edge;
                    pushed = true;
                }
            }
            if (pushed) continue;
            edge->scan = SmithScan_Done;
            visit(exec, stack[--depth], context);
        }
    }
}

// The nodes named on the command line after the tool's own options, or none.
static size_t smith_tool_nodes(SmithExec *exec, size_t first, uint32_t *nodes) {
    size_t count = 0;
    for (size_t i = first; i < exec->options.tool_arg_count; i++) {
        const char *name = exec->options.tool_args[i];
        char folded[smith_exec_name_max];
        size_t length = strlen(name);
        if (length >= sizeof folded) smith_exec_fail("unknown target '%s'", name);
        memcpy(folded, name, length + 1);
        length = smith_exec_canonical(folded, length);
        uint32_t node = smith_exec_node(exec, folded, length, false, nullptr);
        if (node == UINT32_MAX) smith_exec_fail("unknown target '%s'", name);
        nodes[count++] = node;
    }
    return count;
}

// ---------------------------------------------------------------- targets

static int smith_tool_targets(SmithExec *exec) {
    const SmithGraph *graph = &exec->graph;
    const char *mode = exec->options.tool_arg_count > 0 ? exec->options.tool_args[0] : "depth";
    const char *rule = exec->options.tool_arg_count > 1 ? exec->options.tool_args[1] : nullptr;
    bool all = strcmp(mode, "all") == 0;
    bool by_rule = strcmp(mode, "rule") == 0;
    if (!all && !by_rule && strcmp(mode, "depth") != 0) smith_exec_fail("targets: unknown mode '%s'", mode);
    for (uint32_t number = 1; number <= graph->edge_count; number++) {
        const SmithEdge *edge = &graph->edges[number];
        if (by_rule && (rule == nullptr || strcmp(edge->rule, rule) != 0)) continue;
        for (uint32_t k = 0; k < edge->output_count; k++) {
            const SmithNode *node = &graph->nodes[smith_plan_output(exec, edge, k)];
            if (!all && !by_rule && node->consumer != 0) continue; // the roots are the nodes nothing reads
            if (by_rule)
                printf("%s\n", node->path);
            else
                printf("%s: %s\n", node->path, edge->rule);
        }
    }
    return 0;
}

// ---------------------------------------------------------------- clean

typedef struct {
    bool generators;
    const char *rule;
    size_t removed;
} SmithToolClean;

static void smith_tool_remove(SmithExec *exec, const char *path, SmithToolClean *clean) {
    if (smith_files_info(&exec->io->files, path).error.code != 0) return;
    if (smith_files_remove(&exec->io->files, path).code == 0) clean->removed += 1;
}

static void smith_tool_clean_edge(SmithExec *exec, uint32_t number, void *context) {
    SmithToolClean *clean = context;
    const SmithEdge *edge = &exec->graph.edges[number];
    if (edge->phony || (edge->generator && !clean->generators)) return;
    if (clean->rule != nullptr && strcmp(edge->rule, clean->rule) != 0) return;
    for (uint32_t k = 0; k < edge->output_count; k++)
        smith_tool_remove(exec, exec->graph.nodes[smith_plan_output(exec, edge, k)].path, clean);
    if (edge->depfile != nullptr) smith_tool_remove(exec, edge->depfile, clean);
}

// -t clean [-g] [-r rule] [targets]: removes what the build made (the manifest's own rule only with -g).
static int smith_tool_clean(SmithExec *exec) {
    SmithToolClean clean = {};
    size_t first = 0;
    for (; first < exec->options.tool_arg_count; first++) {
        const char *arg = exec->options.tool_args[first];
        if (strcmp(arg, "-g") == 0) {
            clean.generators = true;
        } else if (strcmp(arg, "-r") == 0 && first + 1 < exec->options.tool_arg_count) {
            clean.rule = exec->options.tool_args[++first];
        } else {
            break;
        }
    }
    uint32_t *nodes = smith_exec_push(exec, (exec->options.tool_arg_count + 1) * sizeof(uint32_t), alignof(uint32_t));
    size_t count = smith_tool_nodes(exec, first, nodes);
    printf("Cleaning...");
    smith_tool_walk(exec, nodes, count, smith_tool_clean_edge, &clean);
    printf(" %zu files.\n", clean.removed);
    return 0;
}

// ---------------------------------------------------------------- compdb

static void smith_tool_json(const char *text) {
    putchar('"');
    for (const char *c = text; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\') {
            putchar('\\');
            putchar(*c);
        } else if (*c == '\n') {
            fputs("\\n", stdout);
        } else if ((unsigned char)*c < 0x20) {
            printf("\\u%04x", (unsigned)(unsigned char)*c);
        } else {
            putchar(*c);
        }
    }
    putchar('"');
}

typedef struct {
    const char *directory;
    bool first;
} SmithToolCompdb;

static void smith_tool_compdb_edge(SmithExec *exec, uint32_t number, void *context) {
    SmithToolCompdb *compdb = context;
    const SmithEdge *edge = &exec->graph.edges[number];
    if (edge->phony || edge->explicit_count == 0) return;
    if (exec->options.tool_arg_count > 0) {
        bool wanted = false;
        for (size_t i = 0; i < exec->options.tool_arg_count; i++)
            wanted = wanted || strcmp(exec->options.tool_args[i], edge->rule) == 0;
        if (!wanted) return;
    }
    if (!compdb->first) putchar(',');
    compdb->first = false;
    fputs("\n  {\n    \"directory\": ", stdout);
    smith_tool_json(compdb->directory);
    fputs(",\n    \"command\": ", stdout);
    smith_tool_json(edge->command);
    fputs(",\n    \"file\": ", stdout);
    smith_tool_json(exec->graph.nodes[exec->graph.ids[edge->inputs]].path);
    fputs(",\n    \"output\": ", stdout);
    smith_tool_json(exec->graph.nodes[smith_plan_output(exec, edge, 0)].path);
    fputs("\n  }", stdout);
}

// -t compdb [rules]: the compile commands in the JSON compilation database format.
static int smith_tool_compdb(SmithExec *exec) {
    char directory[PATH_MAX];
    if (getcwd(directory, sizeof directory) == nullptr) smith_exec_fail("cannot find the current folder");
    SmithToolCompdb compdb = {.directory = directory, .first = true};
    fputs("[", stdout);
    smith_tool_walk(exec, nullptr, 0, smith_tool_compdb_edge, &compdb);
    fputs("\n]\n", stdout);
    return 0;
}

// ---------------------------------------------------------------- commands and deps

static void smith_tool_command_edge(SmithExec *exec, uint32_t number, void *context) {
    (void)context;
    const SmithEdge *edge = &exec->graph.edges[number];
    if (!edge->phony) printf("%s\n", edge->command);
}

static int smith_tool_commands(SmithExec *exec) {
    uint32_t *nodes = smith_exec_push(exec, (exec->options.tool_arg_count + 1) * sizeof(uint32_t), alignof(uint32_t));
    size_t count = smith_tool_nodes(exec, 0, nodes);
    if (count == 0) smith_exec_fail("commands: name the targets");
    smith_tool_walk(exec, nodes, count, smith_tool_command_edge, nullptr);
    return 0;
}

static void smith_tool_print_deps(SmithExec *exec, const char *path) {
    const SmithDepsEntry *entry = smith_state_deps_find(exec, path);
    if (entry == nullptr) {
        printf("%s: deps not found\n", path);
        return;
    }
    bool valid = smith_plan_stat_path(path) <= entry->mtime;
    printf("%s: #deps %u, deps mtime %lld (%s)\n", path, entry->dep_count, (long long)entry->mtime,
           valid ? "VALID" : "STALE");
    for (uint32_t i = 0; i < entry->dep_count; i++)
        printf("    %s\n", entry->deps[i]);
    printf("\n");
}

// -t deps [targets]: what the depfile log says each output read.
static int smith_tool_deps(SmithExec *exec) {
    if (exec->options.tool_arg_count > 0) {
        for (size_t i = 0; i < exec->options.tool_arg_count; i++) {
            char folded[smith_exec_name_max];
            size_t length = strlen(exec->options.tool_args[i]);
            if (length >= sizeof folded) smith_exec_fail("unknown target '%s'", exec->options.tool_args[i]);
            memcpy(folded, exec->options.tool_args[i], length + 1);
            (void)smith_exec_canonical(folded, length);
            smith_tool_print_deps(exec, folded);
        }
        return 0;
    }
    for (size_t i = 0; i < exec->state.deps_count; i++)
        smith_tool_print_deps(exec, exec->state.deps[i].path);
    return 0;
}

// ---------------------------------------------------------------- cleandead

// -t cleandead: removes the outputs the log remembers that the manifest no longer makes. A file the manifest still
// names, as an input, stays: it may be a source that was once an output.
static int smith_tool_cleandead(SmithExec *exec) {
    SmithToolClean clean = {};
    printf("Cleaning...");
    for (size_t i = 0; i < exec->state.log_count; i++) {
        const char *path = exec->state.log[i].path;
        uint32_t node = smith_exec_node(exec, path, strlen(path), false, nullptr);
        if (node != UINT32_MAX) continue;
        smith_tool_remove(exec, path, &clean);
    }
    printf(" %zu files.\n", clean.removed);
    return 0;
}

static int smith_tool_run(SmithExec *exec) {
    const char *tool = exec->options.tool;
    if (strcmp(tool, "targets") == 0) return smith_tool_targets(exec);
    if (strcmp(tool, "clean") == 0) return smith_tool_clean(exec);
    if (strcmp(tool, "compdb") == 0) return smith_tool_compdb(exec);
    if (strcmp(tool, "commands") == 0) return smith_tool_commands(exec);
    if (strcmp(tool, "deps") == 0) return smith_tool_deps(exec);
    if (strcmp(tool, "cleandead") == 0) return smith_tool_cleandead(exec);
    smith_exec_fail("unknown tool '%s'; smith build has: targets, clean, compdb, commands, deps, cleandead", tool);
}
