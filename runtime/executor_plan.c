// What is out of date, and in what order to do it: ninja's own rules, over the graph of the manifest.
//
// The scan visits every node the targets need, depth first (with a stack of its own, since nothing recurses), and marks
// a node dirty when the edge that makes it is. An edge is dirty when an input is dirty, an output is missing or older
// than the newest input, the command differs from the one that made the output, or the depfile's contents (kept in the
// depfile log) name a file newer than the output. The plan is then the dirty edges and the clean ones that wait for a
// dirty order-only input. After a restat step runs, an output that did not change cleans the edges that were dirty
// only because of it.

constexpr uint32_t smith_plan_none = UINT32_MAX;

static int64_t smith_plan_stat_path(const char *path) {
    struct stat info = {};
    if (stat(path, &info) != 0) return 0;
    struct timespec modified = smith_platform_mtime(&info);
    int64_t mtime = (int64_t)modified.tv_sec * (int64_t)smith_exec_nanoseconds + (int64_t)modified.tv_nsec;
    return mtime > 0 ? mtime : 1;
}

static int64_t smith_plan_mtime(SmithExec *exec, uint32_t number) {
    SmithNode *node = &exec->graph.nodes[number];
    if (node->mtime < 0) node->mtime = smith_plan_stat_path(node->path);
    return node->mtime;
}

// The number of inputs of every kind, and the one at position k: the explicit and implicit ones, then what the depfile
// log named, then the order-only ones.
static uint32_t smith_plan_input_count(const SmithEdge *edge) {
    return edge->explicit_count + edge->implicit_count + edge->dep_count + edge->order_count;
}

static uint32_t smith_plan_input(const SmithExec *exec, const SmithEdge *edge, uint32_t k) {
    const uint32_t *ids = exec->graph.ids;
    uint32_t plain = edge->explicit_count + edge->implicit_count;
    if (k < plain) return ids[edge->inputs + k];
    if (k < plain + edge->dep_count) return ids[edge->deps + (k - plain)];
    return ids[edge->inputs + plain + (k - plain - edge->dep_count)];
}

static bool smith_plan_is_order_only(const SmithEdge *edge, uint32_t k) {
    return k >= edge->explicit_count + edge->implicit_count + edge->dep_count;
}

static uint32_t smith_plan_output(const SmithExec *exec, const SmithEdge *edge, uint32_t k) {
    return exec->graph.ids[edge->outputs + k];
}

// Where -d explain writes: stderr, or in the units runner the text of the call in process.
#ifdef SMITH_CORE
static FILE *smith_plan_core_stream;
#endif
static FILE *smith_plan_stream(void) {
#ifdef SMITH_CORE
    if (smith_plan_core_stream != nullptr) return smith_plan_core_stream;
#endif
    return stderr;
}

// -d explain: the reason an output is out of date, on stderr.
static void smith_plan_explain(const SmithExec *exec, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void smith_plan_explain(const SmithExec *exec, const char *format, ...) {
    if (!exec->options.explain) return;
    va_list arguments;
    va_start(arguments, format);
    fputs("smith explain: ", smith_plan_stream());
    vfprintf(smith_plan_stream(), format, arguments);
    fputc('\n', smith_plan_stream());
    va_end(arguments);
}

// The reason an output is out of date: remembered when it is the first of this plan (the error of a manifest that is
// never settled names it), and shown by -d explain.
static void smith_plan_reason(SmithExec *exec, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void smith_plan_reason(SmithExec *exec, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    if (exec->first_reason[0] == '\0') vsnprintf(exec->first_reason, sizeof exec->first_reason, format, arguments);
    va_end(arguments);
    va_start(arguments, format);
    if (exec->options.explain) {
        fputs("smith explain: ", smith_plan_stream());
        vfprintf(smith_plan_stream(), format, arguments);
        fputc('\n', smith_plan_stream());
    }
    va_end(arguments);
}

// Whether the output of an edge is out of date, given the newest of the edge's inputs that are not order-only.
static bool smith_plan_output_dirty(SmithExec *exec, const SmithEdge *edge, uint32_t recent, uint32_t output) {
    const SmithNode *node = &exec->graph.nodes[output];
    const SmithNode *newest = recent != smith_plan_none ? &exec->graph.nodes[recent] : nullptr;
    if (edge->phony) return smith_plan_input_count(edge) == 0 && node->mtime == 0;
    if (node->mtime == 0) {
        smith_plan_reason(exec, "output %s doesn't exist", node->path);
        return true;
    }
    const SmithLogEntry *entry = smith_state_log_find(exec, node->path);
    if (newest != nullptr && node->mtime < newest->mtime) {
        // A restat step that found its output unchanged recorded the newest input's time instead of its own.
        int64_t mtime = edge->restat && entry != nullptr ? entry->mtime : node->mtime;
        if (mtime < newest->mtime) {
            smith_plan_reason(exec, "output %s older than most recent input %s (%lld vs %lld)", node->path,
                              newest->path, (long long)mtime, (long long)newest->mtime);
            return true;
        }
    }
    if (entry != nullptr) {
        if (!edge->generator && entry->hash != edge->command_hash) {
            smith_plan_reason(exec, "command line changed for %s", node->path);
            return true;
        }
        if (newest != nullptr && entry->mtime < newest->mtime) {
            smith_plan_reason(exec, "recorded time of %s (%lld) is older than most recent input %s (%lld)", node->path,
                              (long long)entry->mtime, newest->path, (long long)newest->mtime);
            return true;
        }
    }
    if (entry == nullptr && !edge->generator)
        smith_plan_reason(exec, "command line not found in log for %s", node->path);
    return entry == nullptr && !edge->generator;
}

static bool smith_plan_outputs_dirty(SmithExec *exec, const SmithEdge *edge, uint32_t recent) {
    for (uint32_t k = 0; k < edge->output_count; k++) {
        if (smith_plan_output_dirty(exec, edge, recent, smith_plan_output(exec, edge, k))) return true;
    }
    return false;
}

// Reads what the depfile log says the edge read last time. Nothing recorded, or an output newer than the record, makes
// the edge dirty.
static void smith_plan_load_deps(SmithExec *exec, SmithEdge *edge) {
    SmithGraph *graph = &exec->graph;
    const SmithNode *first = &graph->nodes[smith_plan_output(exec, edge, 0)];
    const SmithDepsEntry *entry = smith_state_deps_find(exec, first->path);
    if (entry == nullptr || first->mtime > entry->mtime) {
        smith_plan_reason(exec, entry == nullptr ? "deps for %s are missing" : "stored deps info out of date for %s",
                          first->path);
        edge->deps_missing = true;
        return;
    }
    if (graph->id_count + entry->dep_count > smith_exec_ids_max ||
        graph->link_count + entry->dep_count > smith_exec_ids_max)
        smith_exec_fail("%s: too many inputs", exec->manifest);
    edge->deps = (uint32_t)graph->id_count;
    uint32_t number = (uint32_t)(edge - graph->edges);
    for (uint32_t i = 0; i < entry->dep_count; i++) {
        bool created = false;
        char folded[smith_exec_name_max];
        size_t length = strlen(entry->deps[i]);
        if (length >= sizeof folded) smith_exec_fail("%s: a path is longer than it can be", entry->deps[i]);
        memcpy(folded, entry->deps[i], length + 1);
        length = smith_exec_canonical(folded, length);
        uint32_t node = smith_exec_node(exec, folded, length, true, &created);
        if (created) graph->nodes[node].from_deps = true;
        graph->ids[graph->id_count++] = node;
        graph->links[graph->link_count] = (SmithLink){.edge = number, .next = graph->nodes[node].consumer};
        graph->nodes[node].consumer = (uint32_t)++graph->link_count;
    }
    edge->dep_count = entry->dep_count;
}

[[noreturn]] static void smith_plan_cycle(SmithExec *exec, const uint32_t *stack, size_t depth, uint32_t again) {
    fflush(stdout);
    fprintf(stderr, "smith: error: dependency cycle: ");
    size_t from = 0;
    for (size_t i = 0; i < depth; i++) {
        if (stack[i] == again) from = i;
    }
    for (size_t i = from; i < depth; i++)
        fprintf(stderr, "%s -> ", exec->graph.nodes[stack[i]].path);
    fprintf(stderr, "%s\n", exec->graph.nodes[again].path);
    exit(smith_exec_failed);
}

// An edge whose inputs were all scanned: is it dirty, and are its outputs ready.
static void smith_plan_finish_scan(SmithExec *exec, SmithEdge *edge) {
    SmithGraph *graph = &exec->graph;
    uint32_t total = smith_plan_input_count(edge);
    uint32_t recent = smith_plan_none;
    bool dirty = edge->deps_missing;
    edge->outputs_ready = true;
    for (uint32_t k = 0; k < total; k++) {
        uint32_t number = smith_plan_input(exec, edge, k);
        const SmithNode *input = &graph->nodes[number];
        if (input->edge != 0 && !graph->edges[input->edge].outputs_ready) edge->outputs_ready = false;
        if (smith_plan_is_order_only(edge, k)) continue;
        if (input->dirty) {
            smith_plan_explain(exec, "%s is dirty", input->path);
            dirty = true;
        } else if (recent == smith_plan_none || input->mtime > graph->nodes[recent].mtime)
            recent = number;
    }
    if (!dirty) dirty = smith_plan_outputs_dirty(exec, edge, recent);
    for (uint32_t k = 0; k < edge->output_count; k++) {
        SmithNode *output = &graph->nodes[smith_plan_output(exec, edge, k)];
        if (dirty) output->dirty = true;
        output->scan = SmithScan_Done;
    }
    edge->dirty = dirty;
    if (dirty && !(edge->phony && total == 0)) edge->outputs_ready = false;
    edge->scan = SmithScan_Done;
}

// Starts the scan of the edge that makes a node: the times of its outputs, and what the depfile log read.
static void smith_plan_begin_edge(SmithExec *exec, uint32_t stack_depth, const uint32_t *stack, SmithEdge *edge) {
    if (edge->scan == SmithScan_Visiting) { // another output of an edge that is being scanned: the stack is a cycle
        uint32_t again = stack[stack_depth - 1];
        for (uint32_t i = 0; i < stack_depth; i++) {
            if (&exec->graph.edges[exec->graph.nodes[stack[i]].edge] == edge) {
                again = stack[i];
                break;
            }
        }
        smith_plan_cycle(exec, stack, stack_depth, again);
    }
    edge->scan = SmithScan_Visiting;
    edge->cursor = 0;
    for (uint32_t k = 0; k < edge->output_count; k++)
        (void)smith_plan_mtime(exec, smith_plan_output(exec, edge, k));
    if (edge->has_deps) smith_plan_load_deps(exec, edge);
}

// Scans the node and everything it needs.
static void smith_plan_scan(SmithExec *exec, uint32_t root, uint32_t *stack) {
    SmithGraph *graph = &exec->graph;
    size_t depth = 0;
    stack[depth++] = root;
    for (size_t steps = 0; depth > 0; steps++) {
        if (steps > (graph->node_count + graph->link_count + 1) * 4) smith_exec_fail("the scan did not end");
        SmithNode *node = &graph->nodes[stack[depth - 1]];
        if (node->scan == SmithScan_Done) {
            depth -= 1;
            continue;
        }
        if (node->scan == SmithScan_None) {
            node->scan = SmithScan_Visiting;
            if (node->edge == 0) { // a file nothing makes: dirty when it is missing
                node->dirty = smith_plan_mtime(exec, stack[depth - 1]) == 0;
                node->scan = SmithScan_Done;
                depth -= 1;
                continue;
            }
            if (graph->edges[node->edge].scan == SmithScan_Done) {
                node->scan = SmithScan_Done;
                depth -= 1;
                continue;
            }
            smith_plan_begin_edge(exec, (uint32_t)depth, stack, &graph->edges[node->edge]);
        }
        SmithEdge *edge = &graph->edges[node->edge];
        uint32_t total = smith_plan_input_count(edge);
        bool pushed = false;
        while (edge->cursor < total && !pushed) {
            uint32_t input = smith_plan_input(exec, edge, edge->cursor);
            if (graph->nodes[input].scan == SmithScan_Visiting) smith_plan_cycle(exec, stack, depth, input);
            if (graph->nodes[input].scan == SmithScan_None) {
                stack[depth++] = input;
                pushed = true;
            } else {
                edge->cursor += 1;
            }
        }
        if (pushed) continue;
        smith_plan_finish_scan(exec, edge);
        depth -= 1;
    }
}

// ---------------------------------------------------------------- the plan

// The nodes the build is asked for: the names on the command line, else the manifest's defaults, else every node
// that nothing reads. Returns how many.
static size_t smith_plan_targets(SmithExec *exec, uint32_t *targets) {
    SmithGraph *graph = &exec->graph;
    size_t count = 0;
    for (size_t i = 0; i < exec->options.target_count; i++) {
        const char *name = exec->options.targets[i];
        size_t length = strlen(name);
        char folded[smith_exec_name_max];
        if (length >= sizeof folded) smith_exec_fail("unknown target '%s'", name);
        memcpy(folded, name, length + 1);
        length = smith_exec_canonical(folded, length);
        uint32_t node = smith_exec_node(exec, folded, length, false, nullptr);
        if (node == UINT32_MAX && exec->options.verb != nullptr) {
            smith_exec_fail("smith %s: %s defines no task or alias named %s; add smith_task(b, \"%s\", ...)", name,
                            smith_project_build_file(), name, name);
        }
        if (node == UINT32_MAX) smith_exec_fail("unknown target '%s'", name);
        targets[count++] = node;
    }
    if (count > 0) return count;
    for (size_t i = 0; i < graph->default_count; i++)
        targets[count++] = graph->defaults[i];
    if (graph->default_count > 0) return count;
    for (size_t i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i].edge != 0 && graph->nodes[i].consumer == 0) targets[count++] = (uint32_t)i;
    }
    return count;
}

typedef struct {
    uint32_t node;
    uint32_t parent; // the edge that needs it, or 0
} SmithPlanStep;

// When the missing file is the LLVM install's smith-clang.id (<cache>/<name>/install/lib/smith-clang.id), says which
// names are installed beside it and how to use one (SMITH_LLVM_NAME), or to build the pack.
static void smith_plan_llvm_hint(const char *path) {
    static const char tail[] = "/lib/smith-clang.id";
    size_t length = strlen(path);
    if (length < sizeof(tail) || strcmp(path + length - (sizeof(tail) - 1), tail) != 0) return;
    char folder[4096];
    if (length >= sizeof(folder)) return;
    memcpy(folder, path, length - (sizeof(tail) - 1)); // <cache>/<name>/install
    folder[length - (sizeof(tail) - 1)] = '\0';
    for (int up = 0; up < 2; up++) { // bounded: install, then the name
        char *slash = strrchr(folder, '/');
        if (slash == nullptr) return;
        *slash = '\0';
    }
    DIR *dir = opendir(folder);
    if (dir == nullptr) return;
    fprintf(stderr, "smith: no usable LLVM install; installed under %s:", folder);
    int count = 0;
    for (const struct dirent *entry = readdir(dir); entry != nullptr && count < 64; entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        fprintf(stderr, " %s", entry->d_name);
        count++;
    }
    closedir(dir);
    fprintf(stderr,
            "\nsmith: set SMITH_LLVM_NAME=<one of them> to build with that one (a smith under test for a change "
            "that does not touch the LLVM pack), or run smith build llvm\n");
}

// Adds the edges a target needs to the plan: those that are not ready, each once.
static void smith_plan_add(SmithExec *exec, uint32_t root, SmithPlanStep *stack) {
    SmithGraph *graph = &exec->graph;
    size_t depth = 0;
    stack[depth++] = (SmithPlanStep){root, 0};
    for (size_t steps = 0; depth > 0; steps++) {
        if (steps > (graph->node_count + graph->link_count + 2) * 2) smith_exec_fail("the plan did not end");
        SmithPlanStep step = stack[--depth];
        const SmithNode *node = &graph->nodes[step.node];
        if (node->edge == 0) {
            if (node->dirty && !node->from_deps) {
                smith_plan_llvm_hint(node->path);
                if (step.parent == 0) smith_exec_fail("'%s' missing and no known rule to make it", node->path);
                const SmithEdge *parent = &graph->edges[step.parent];
                smith_exec_fail("'%s', needed by '%s', missing and no known rule to make it", node->path,
                                graph->nodes[smith_plan_output(exec, parent, 0)].path);
            }
            continue;
        }
        SmithEdge *edge = &graph->edges[node->edge];
        if (edge->outputs_ready || edge->want != SmithWant_None) continue;
        edge->want = edge->dirty ? SmithWant_Start : SmithWant_Nothing;
        uint32_t total = smith_plan_input_count(edge);
        if (depth + total > graph->node_count + graph->link_count) smith_exec_fail("the plan stack overflowed");
        for (uint32_t k = 0; k < total; k++)
            stack[depth++] = (SmithPlanStep){smith_plan_input(exec, edge, k), node->edge};
    }
}

static bool smith_plan_in_plan(const SmithEdge *edge) {
    return edge->want == SmithWant_Nothing || edge->want == SmithWant_Start;
}

// Puts an edge whose inputs are all done where it goes next: a command in the ready list, anything else among the edges
// that finish at once.
static void smith_plan_schedule(SmithExec *exec, uint32_t number) {
    SmithEdge *edge = &exec->graph.edges[number];
    if (edge->want == SmithWant_Start && !edge->phony)
        exec->ready[exec->ready_count++] = number;
    else
        exec->finishing[exec->finishing_count++] = number;
}

// Counts what the plan holds, and finds the edges that can go first.
static void smith_plan_start(SmithExec *exec) {
    SmithGraph *graph = &exec->graph;
    exec->wanted = 0;
    exec->command_edges = 0;
    for (uint32_t number = 1; number <= graph->edge_count; number++) {
        SmithEdge *edge = &graph->edges[number];
        if (!smith_plan_in_plan(edge)) continue;
        if (edge->want == SmithWant_Start) {
            exec->wanted += 1;
            if (!edge->phony) exec->command_edges += 1;
        }
        edge->waiting = 0;
        uint32_t total = smith_plan_input_count(edge);
        for (uint32_t k = 0; k < total; k++) {
            const SmithNode *input = &graph->nodes[smith_plan_input(exec, edge, k)];
            if (input->edge != 0 && !graph->edges[input->edge].outputs_ready) edge->waiting += 1;
        }
    }
    for (uint32_t number = 1; number <= graph->edge_count; number++) {
        if (smith_plan_in_plan(&graph->edges[number]) && graph->edges[number].waiting == 0)
            smith_plan_schedule(exec, number);
    }
}

// The edge is done, with or without a command: its outputs are ready, and the edges that waited only for it can go.
static void smith_plan_done(SmithExec *exec, uint32_t number) {
    SmithGraph *graph = &exec->graph;
    SmithEdge *edge = &graph->edges[number];
    if (edge->want == SmithWant_Start || edge->want == SmithWant_Running) exec->wanted -= 1;
    edge->want = SmithWant_Done;
    edge->outputs_ready = true;
    for (uint32_t k = 0; k < edge->output_count; k++) {
        const SmithNode *output = &graph->nodes[smith_plan_output(exec, edge, k)];
        for (uint32_t link = output->consumer; link != 0; link = graph->links[link - 1].next) {
            uint32_t consumer = graph->links[link - 1].edge;
            if (!smith_plan_in_plan(&graph->edges[consumer])) continue;
            SMITH_ASSERT(graph->edges[consumer].waiting > 0);
            graph->edges[consumer].waiting -= 1;
            if (graph->edges[consumer].waiting == 0) smith_plan_schedule(exec, consumer);
        }
    }
}

// Finishes the edges that need no command, and those they let go of, until none is left.
static void smith_plan_settle(SmithExec *exec) {
    for (size_t steps = 0; exec->finishing_count > 0; steps++) {
        if (steps > exec->graph.edge_count * 2 + 2) smith_exec_fail("the plan did not settle");
        uint32_t number = exec->finishing[--exec->finishing_count];
        smith_plan_done(exec, number);
    }
}

// An output of a restat step did not change: the edges that were dirty only because of it may be clean. They stay in
// the plan, without a command, until what they wait for is done.
static void smith_plan_clean(SmithExec *exec, uint32_t first, uint32_t *stack) {
    SmithGraph *graph = &exec->graph;
    size_t depth = 0;
    stack[depth++] = first;
    for (size_t steps = 0; depth > 0; steps++) {
        if (steps > graph->node_count + graph->link_count + 2) smith_exec_fail("the clean-up did not end");
        SmithNode *node = &graph->nodes[stack[--depth]];
        node->dirty = false;
        for (uint32_t link = node->consumer; link != 0; link = graph->links[link - 1].next) {
            SmithEdge *edge = &graph->edges[graph->links[link - 1].edge];
            if (edge->want != SmithWant_Start || edge->deps_missing) continue;
            uint32_t total = smith_plan_input_count(edge);
            uint32_t recent = smith_plan_none;
            bool dirty = false;
            for (uint32_t k = 0; k < total && !dirty; k++) {
                uint32_t input = smith_plan_input(exec, edge, k);
                if (smith_plan_is_order_only(edge, k)) continue;
                dirty = graph->nodes[input].dirty;
                if (recent == smith_plan_none || graph->nodes[input].mtime > graph->nodes[recent].mtime) recent = input;
            }
            if (dirty || smith_plan_outputs_dirty(exec, edge, recent)) continue;
            edge->want = SmithWant_Nothing;
            exec->wanted -= 1;
            if (!edge->phony) exec->command_edges -= 1;
            for (uint32_t k = 0; k < edge->output_count; k++)
                stack[depth++] = smith_plan_output(exec, edge, k);
        }
    }
}

// The commands from each edge in the plan to the end of it, the edge's own included: the edges with the longest chain
// after them start first, as in ninja. (The logged durations were tried instead: game's clean builds got a little
// faster and learner's `check test` a little slower, so the plain count, which is the same as ninja's, stays.)
static void smith_plan_weigh(SmithExec *exec) {
    SmithGraph *graph = &exec->graph;
    uint32_t *order = smith_exec_push(exec, (graph->edge_count + 1) * sizeof(uint32_t), alignof(uint32_t));
    uint32_t *waiting = smith_exec_push(exec, (graph->edge_count + 1) * sizeof(uint32_t), alignof(uint32_t));
    size_t head = 0;
    size_t tail = 0;
    for (uint32_t number = 1; number <= graph->edge_count; number++) {
        waiting[number] = graph->edges[number].waiting;
        if (smith_plan_in_plan(&graph->edges[number]) && waiting[number] == 0) order[tail++] = number;
    }
    for (; head < tail; head++) {
        const SmithEdge *edge = &graph->edges[order[head]];
        for (uint32_t k = 0; k < edge->output_count; k++) {
            const SmithNode *output = &graph->nodes[smith_plan_output(exec, edge, k)];
            for (uint32_t link = output->consumer; link != 0; link = graph->links[link - 1].next) {
                uint32_t consumer = graph->links[link - 1].edge;
                if (smith_plan_in_plan(&graph->edges[consumer]) && --waiting[consumer] == 0) order[tail++] = consumer;
            }
        }
    }
    for (size_t i = tail; i > 0; i--) {
        SmithEdge *edge = &graph->edges[order[i - 1]];
        uint32_t own = edge->phony ? 0 : 1;
        uint32_t after = 0;
        for (uint32_t k = 0; k < edge->output_count; k++) {
            const SmithNode *output = &graph->nodes[smith_plan_output(exec, edge, k)];
            for (uint32_t link = output->consumer; link != 0; link = graph->links[link - 1].next) {
                const SmithEdge *consumer = &graph->edges[graph->links[link - 1].edge];
                if (smith_plan_in_plan(consumer) && consumer->weight > after) after = consumer->weight;
            }
        }
        edge->weight = own + after;
    }
}

// Scans and plans the targets. Returns whether there is anything to do.
static bool smith_plan_build(SmithExec *exec, const uint32_t *targets, size_t count) {
    SmithGraph *graph = &exec->graph;
    size_t stack_size = (graph->node_count + graph->link_count + 8) * sizeof(SmithPlanStep);
    SmithPlanStep *stack = smith_exec_push(exec, stack_size, alignof(SmithPlanStep));
    uint32_t *scan_stack =
        smith_exec_push(exec, (graph->node_count + graph->link_count + 8) * sizeof(uint32_t), alignof(uint32_t));
    exec->ready = smith_exec_push(exec, (graph->edge_count + 1) * sizeof(uint32_t), alignof(uint32_t));
    exec->finishing = smith_exec_push(exec, (graph->edge_count + 1) * sizeof(uint32_t), alignof(uint32_t));
    exec->ready_count = 0;
    exec->finishing_count = 0;
    for (size_t i = 0; i < count; i++)
        smith_plan_scan(exec, targets[i], scan_stack);
    for (size_t i = 0; i < count; i++)
        smith_plan_add(exec, targets[i], stack);
    smith_plan_start(exec);
    smith_plan_weigh(exec);
    smith_plan_settle(exec);
    return exec->wanted > 0;
}

#ifdef SMITH_CORE
#include "smith_core_plan.h"

// Defined in executor_run.c, which this file comes before.
static int64_t smith_run_restat(SmithExec *exec, SmithEdge *edge, int64_t started, uint32_t *stack);
static void smith_run_make_parents(SmithExec *exec, const char *path);

static bool smith_plan_core_listed(const char *const *list, const char *path) {
    for (size_t i = 0; list != nullptr && list[i] != nullptr; i++) { // bounded by the list
        if (strcmp(list[i], path) == 0) return true;
    }
    return false;
}

static void smith_plan_core_set_time(const char *path, int64_t seconds) {
    struct timespec times[2] = {{.tv_sec = (time_t)seconds}, {.tv_sec = (time_t)seconds}};
    (void)utimensat(AT_FDCWD, path, times, 0);
}

// The depfile of an edge as the build gave it: the names after "output: ".
static uint32_t smith_plan_core_deps(SmithExec *exec, const SmithCorePlan *build, const char *first, const char **names,
                                     uint32_t room) {
    size_t length = strlen(first);
    for (size_t i = 0; build->depends != nullptr && build->depends[i] != nullptr; i++) { // bounded by the list
        const char *line = build->depends[i];
        if (strncmp(line, first, length) != 0 || line[length] != ':') continue;
        uint32_t count = 0;
        for (const char *p = line + length + 1; *p != '\0' && count < room;) { // bounded by the line
            while (*p == ' ')
                p++;
            const char *end = p;
            while (*end != '\0' && *end != ' ')
                end++;
            if (end > p) names[count++] = smith_exec_copy(exec, p, (size_t)(end - p));
            p = end;
        }
        return count;
    }
    return 0;
}

// The fake command of an edge: it makes the outputs its text names (a task that only writes a log makes none), unless
// the build says it leaves an existing one as it is. The depfile is recorded as the executor does after a command.
static void smith_plan_core_command(SmithExec *exec, const SmithCorePlan *build, SmithEdge *edge) {
    for (uint32_t k = 0; k < edge->output_count; k++) {
        const char *path = exec->graph.nodes[smith_plan_output(exec, edge, k)].path;
        bool exists = smith_plan_stat_path(path) != 0;
        if (strstr(edge->command, path) == nullptr || (exists && smith_plan_core_listed(build->unchanged, path)))
            continue;
        smith_run_make_parents(exec, path);
        FILE *file = fopen(path, "wb");
        if (file == nullptr) continue;
        fputs("made\n", file);
        fclose(file);
        smith_plan_core_set_time(path, build->time);
    }
    if (!edge->has_deps) return;
    const char *names[64];
    const char *first = exec->graph.nodes[smith_plan_output(exec, edge, 0)].path;
    uint32_t count = smith_plan_core_deps(exec, build, first, names, 64);
    smith_state_record_deps(exec, edge, smith_plan_stat_path(first), names, count);
}

// The plan's commands, run one after another in the order the plan gives.
static size_t smith_plan_core_run(SmithExec *exec, const SmithCorePlan *build, FILE *out) {
    uint32_t *stack = smith_exec_push(exec, (exec->graph.node_count + exec->graph.link_count + 8) * sizeof(uint32_t),
                                      alignof(uint32_t));
    int64_t started = build->time * (int64_t)smith_exec_nanoseconds;
    size_t ran = 0;
    for (size_t round = 0; exec->ready_count > 0 && round <= exec->graph.edge_count * 2; round++) {
        uint32_t number = exec->ready[--exec->ready_count];
        SmithEdge *edge = &exec->graph.edges[number];
        if (edge->want != SmithWant_Start) continue;
        edge->want = SmithWant_Running;
        smith_plan_core_command(exec, build, edge);
        fprintf(out, "ran %s\n", exec->graph.nodes[smith_plan_output(exec, edge, 0)].path);
        ran += 1;
        int64_t mtime = smith_run_restat(exec, edge, started, stack);
        smith_state_record(exec, edge, 0, 0, mtime);
        smith_plan_done(exec, number);
        smith_plan_settle(exec);
    }
    return ran;
}

bool smith_core_plan_write(const char *folder, const char *name, const char *text, int64_t time) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s", folder, name) >= (int)sizeof path) return false;
    char parents[PATH_MAX];
    snprintf(parents, sizeof parents, "%s", path);
    for (char *slash = strchr(parents + strlen(folder) + 1, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        (void)mkdir(parents, 0755);
        *slash = '/';
    }
    FILE *file = fopen(path, "wb");
    if (file == nullptr) return false;
    bool whole = fputs(text, file) >= 0;
    whole = fclose(file) == 0 && whole;
    smith_plan_core_set_time(path, time);
    return whole;
}

// include/smith_core_plan.h: a `smith build`'s plan and fake run, in process.
bool smith_core_plan(const SmithCorePlan *build, char *output, size_t output_size, size_t *ran) {
    int home = open(".", O_RDONLY);
    FILE *out = fmemopen(output, output_size, "w");
    if (home < 0 || out == nullptr || chdir(build->folder) != 0) {
        if (out != nullptr) fclose(out);
        if (home >= 0) close(home);
        return false;
    }
    SmithIo io = smith_io_make(0, nullptr);
    SmithExec *exec = smith_arena_push(&io.arena, sizeof(SmithExec), alignof(SmithExec));
    exec->io = &io;
    exec->arena = &io.arena;
    exec->options.explain = true;
    exec->manifest = build->manifest;
    SmithArena scratch = smith_arena_carve(&io.arena, smith_exec_text_max);
    exec->scratch = (char *)scratch.memory;
    smith_plan_core_stream = out;
    smith_manifest_read(exec);
    smith_state_open(exec);
    const char *const *names = build->targets;
    exec->options.targets = names;
    for (size_t i = 0; names != nullptr && names[i] != nullptr; i++)
        exec->options.target_count = i + 1;
    size_t room = exec->graph.node_count + exec->graph.default_count + exec->options.target_count + 1;
    uint32_t *targets = smith_exec_push(exec, room * sizeof(uint32_t), alignof(uint32_t));
    size_t count = smith_plan_targets(exec, targets);
    size_t commands = 0;
    if (smith_plan_build(exec, targets, count))
        commands = smith_plan_core_run(exec, build, out);
    else
        fputs("no work to do\n", out);
    smith_plan_core_stream = nullptr;
    fclose(out);
    smith_io_close(&io);
    bool back = fchdir(home) == 0;
    close(home);
    *ran = commands;
    return back;
}
#endif
