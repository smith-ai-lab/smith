// The progress line, the jobs and the loop that runs them. A job is a process; smith_io_run waits for all of them at
// once on the io's kqueue, and each job's output is printed whole when it ends, as ninja does.

constexpr int64_t smith_run_wait_ns = 50'000'000; // how long one wait for the loop is, so that a signal is seen soon
constexpr size_t smith_run_stop_rounds = 100;     // rounds (of that wait) a job gets to end after the signal
constexpr size_t smith_run_loops_max = (size_t)1 << 40;

static volatile sig_atomic_t smith_run_signal;

static void smith_run_on_signal(int signal_number) {
    smith_run_signal = signal_number;
}

static void smith_run_watch_signals(void) {
    struct sigaction action = {};
    action.sa_handler = smith_run_on_signal; // no SA_RESTART: a wait is interrupted and the flag is looked at
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
}

// ---------------------------------------------------------------- the printer

static void smith_run_printer_open(SmithExec *exec) {
    SmithPrinter *printer = &exec->printer;
    const char *term = getenv("TERM");
    printer->smart = isatty(STDOUT_FILENO) && term != nullptr && strcmp(term, "dumb") != 0;
    struct winsize size = {};
    bool known = printer->smart && ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0;
    printer->columns = known ? size.ws_col : 80;
    printer->held = (SmithGrow){.arena = exec->arena};
}

// Prints text on a line of its own: after the progress line, which is left on the screen.
static void smith_run_emit(SmithExec *exec, const char *text, size_t length) {
    SmithPrinter *printer = &exec->printer;
    if (length == 0) return;
    if (printer->locked) {
        smith_grow_append(&printer->held, text, length);
        return;
    }
    if (printer->smart && printer->dirty) {
        fputc('\n', stdout);
        printer->dirty = false;
    }
    fwrite(text, 1, length, stdout);
}

// The progress line for an edge: [finished/total] and what it does, cut in the middle to fit a terminal.
static void smith_run_progress(SmithExec *exec, const SmithEdge *edge) {
    SmithPrinter *printer = &exec->printer;
    if (exec->options.quiet || printer->locked) return;
    const char *what = exec->options.verbose || edge->description == nullptr ? edge->command : edge->description;
    if (!printer->smart) {
        printf("[%zu/%zu] %s\n", exec->finished_edges, exec->command_edges, what);
        return;
    }
    char line[1024];
    int length = snprintf(line, sizeof line, "[%zu/%zu] %s", exec->finished_edges, exec->command_edges, what);
    size_t size = length > 0 && (size_t)length < sizeof line ? (size_t)length : sizeof line - 1;
    size_t columns = printer->columns;
    if (size > columns && columns > 3) {
        size_t keep = columns - 3;
        size_t head = keep / 2;
        printf("\r%.*s...%s\x1b[K", (int)head, line, line + size - (keep - head));
    } else {
        printf("\r%.*s\x1b[K", (int)(size < columns ? size : columns), line);
    }
    printer->dirty = true;
    fflush(stdout);
}

// Leaves the terminal tidy: the progress line is ended with a newline.
static void smith_run_printer_close(SmithExec *exec) {
    SmithPrinter *printer = &exec->printer;
    if (printer->held.size > 0) {
        fwrite(printer->held.data, 1, printer->held.size, stdout);
        printer->held.size = 0;
    }
    if (printer->smart && printer->dirty) fputc('\n', stdout);
    printer->dirty = false;
    fflush(stdout);
}

static void smith_run_lock(SmithExec *exec, bool locked) {
    SmithPrinter *printer = &exec->printer;
    printer->locked = locked;
    if (locked) return;
    if (printer->held.size > 0) {
        if (printer->smart && printer->dirty) fputc('\n', stdout);
        printer->dirty = false;
        fwrite(printer->held.data, 1, printer->held.size, stdout);
        printer->held.size = 0;
    }
}

// ---------------------------------------------------------------- jobs

static uint64_t smith_run_now_ms(const SmithExec *exec) {
    return (smith_clock_now(&exec->io->clock) - exec->origin) / smith_exec_milliseconds;
}

static void smith_run_command_argv(const SmithEdge *edge, const char *argv[4]) {
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = edge->command;
    argv[3] = nullptr;
}

// A task, when a run is accounted (runtime/account.c), goes through `smith --smith-account`, which adds a line to the
// account file for it. Returns the argv to run.
static const char *const *smith_job_argv(SmithExec *exec, const SmithEdge *edge, const char *argv[8]) {
    const char *file = getenv("SMITH_ACCOUNT_FILE");
    const char *self = getenv("SMITH_SELF");
    constexpr char task_prefix[] = ".smith/task/";
    const char *output = exec->graph.nodes[smith_plan_output(exec, edge, 0)].path;
    if (file == nullptr || *file == '\0' || self == nullptr ||
        strncmp(output, task_prefix, sizeof task_prefix - 1) != 0) {
        smith_run_command_argv(edge, argv);
        return argv;
    }
    argv[0] = self;
    argv[1] = "--smith-account";
    argv[2] = file;
    argv[3] = output + sizeof task_prefix - 1;
    argv[4] = "/bin/sh";
    argv[5] = "-c";
    argv[6] = edge->command;
    argv[7] = nullptr;
    return argv;
}

static bool smith_job_process_start(SmithExec *exec, SmithJob *job) {
    const char *argv_storage[8];
    const char *const *argv = smith_job_argv(exec, &exec->graph.edges[job->edge], argv_storage);
    SmithError error = smith_processes_capture_async(&exec->io->processes, &job->arena, argv, &job->capture);
    if (error.code != 0) snprintf(job->message, sizeof job->message, "%s", error.message);
    return error.code == 0;
}

static bool smith_job_process_poll(SmithExec *exec, SmithJob *job) {
    (void)exec;
    if (!smith_capture_poll(&job->capture)) return false;
    job->signal = job->capture.signal;
    job->status = job->capture.signal != 0 ? 128 + job->capture.signal : job->capture.exit_status;
    job->output = job->capture.output;
    job->output_size = job->capture.output_size;
    job->truncated = job->capture.truncated;
    if (job->capture.error.code != 0) {
        snprintf(job->message, sizeof job->message, "%s", job->capture.error.message);
        if (job->status == 0) job->status = smith_exec_failed;
    }
    return true;
}

static void smith_job_process_stop(SmithExec *exec, SmithJob *job, int signal_number) {
    (void)exec;
    // Never a process that has been waited for: its id may be another program's by now, even while its output is read.
    (void)smith_capture_stop(&job->capture, signal_number);
}

// A console job has the terminal, so its output is not captured, and it stays in this process group so that a signal
// from the terminal reaches it.
static bool smith_job_console_start(SmithExec *exec, SmithJob *job) {
    const char *argv[4];
    smith_run_command_argv(&exec->graph.edges[job->edge], argv);
    fflush(stdout);
    SmithError error = smith_processes_spawn_terminal(&exec->io->processes, argv, &job->process);
    if (error.code != 0) {
        snprintf(job->message, sizeof job->message, "%s", error.message);
        return false;
    }
    smith_processes_wait_async(&exec->io->processes, &job->wait, job->process);
    return true;
}

static bool smith_job_console_poll(SmithExec *exec, SmithJob *job) {
    (void)exec;
    if (!job->wait.done) return false;
    job->signal = job->wait.signal;
    job->status = job->wait.signal != 0 ? 128 + job->wait.signal : job->wait.exit_status;
    return true;
}

static void smith_job_console_stop(SmithExec *exec, SmithJob *job, int signal_number) {
    (void)exec;
    if (!job->wait.done && job->process.id > 0) kill(job->process.id, signal_number);
}

// -n: nothing runs, and every command succeeds.
static bool smith_job_dry_start(SmithExec *exec, SmithJob *job) {
    (void)exec;
    (void)job;
    return true;
}

static bool smith_job_dry_poll(SmithExec *exec, SmithJob *job) {
    (void)exec;
    job->status = 0;
    return true;
}

static void smith_job_dry_stop(SmithExec *exec, SmithJob *job, int signal_number) {
    (void)exec;
    (void)job;
    (void)signal_number;
}

static const SmithJobKind smith_job_process = {smith_job_process_start, smith_job_process_poll, smith_job_process_stop};
static const SmithJobKind smith_job_console = {smith_job_console_start, smith_job_console_poll, smith_job_console_stop};
static const SmithJobKind smith_job_dry = {smith_job_dry_start, smith_job_dry_poll, smith_job_dry_stop};

// ---------------------------------------------------------------- what a command leaves behind

// A depfile is one Makefile rule, `output: dep dep ...`, read as ninja reads it: `\ ` is a space, `\#` a #, `\\` a
// backslash and `$$` a $; a backslash before a newline goes on; a colon followed by a blank ends a target. A word that
// ends in a colon after the first (the targets `-MP` adds) names nothing.
typedef struct {
    const char *cursor;
    const char *end;
} SmithDepfile;

static bool smith_depfile_blank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool smith_depfile_at_join(const SmithDepfile *file) {
    return *file->cursor == '\\' && file->cursor + 1 < file->end &&
           (file->cursor[1] == '\n' || file->cursor[1] == '\r');
}

// Skips blanks and joined lines. False at the end.
static bool smith_depfile_skip(SmithDepfile *file) {
    for (size_t guard = 0; file->cursor < file->end && guard <= (size_t)(file->end - file->cursor) + 1; guard++) {
        if (smith_depfile_blank(*file->cursor))
            file->cursor += 1;
        else if (smith_depfile_at_join(file))
            file->cursor += 2;
        else
            return true;
    }
    return false;
}

// The next word, unescaped into `word`. *colon says whether a colon that ends a target came after it.
static size_t smith_depfile_word(SmithDepfile *file, char *word, size_t capacity, bool *colon) {
    size_t length = 0;
    *colon = false;
    while (file->cursor < file->end && length + 1 < capacity) {
        char c = *file->cursor;
        char next = file->cursor + 1 < file->end ? file->cursor[1] : '\0';
        if (smith_depfile_blank(c) || smith_depfile_at_join(file)) break;
        if (c == '\\' && (next == ' ' || next == '#' || next == '\\')) {
            word[length++] = next;
            file->cursor += 2;
        } else if (c == '$' && next == '$') {
            word[length++] = '$';
            file->cursor += 2;
        } else if (c == ':' && (next == '\0' || smith_depfile_blank(next) || next == '\\')) {
            *colon = true;
            file->cursor += 1;
            break;
        } else {
            word[length++] = c;
            file->cursor += 1;
        }
    }
    word[length] = '\0';
    return length;
}

// Reads the dependencies into the arena. `first` gets the first target, as written. Returns false when there is none.
static bool smith_run_parse_depfile(SmithExec *exec, const char *text, size_t size, const char ***deps, uint32_t *count,
                                    char *first, size_t first_capacity) {
    const char **list =
        (const char **)smith_exec_push(exec, (size / 2 + 2) * sizeof(const char *), alignof(const char *));
    SmithDepfile file = {.cursor = text, .end = text + size};
    uint32_t used = 0;
    bool after_colon = false;
    bool have_target = false;
    first[0] = '\0';
    for (size_t words = 0; smith_depfile_skip(&file) && words <= size; words++) {
        char word[smith_exec_name_max];
        bool colon = false;
        size_t length = smith_depfile_word(&file, word, sizeof word, &colon);
        if (!after_colon && !have_target && length > 0) {
            snprintf(first, first_capacity, "%s", word);
            have_target = true;
        }
        if (colon) {
            after_colon = true;
        } else if (after_colon && length > 0) {
            length = smith_exec_canonical(word, length);
            list[used++] = smith_exec_copy(exec, word, length);
        }
    }
    *deps = list;
    *count = used;
    return have_target;
}

// The edge's depfile: its dependencies go to the depfile log and the file is removed. A missing depfile is no
// dependencies. False, with the reason in the job, if it does not name the edge's first output.
static bool smith_run_take_depfile(SmithExec *exec, const SmithEdge *edge, SmithJob *job) {
    SmithRead file = smith_files_read(&exec->io->files, exec->arena, edge->depfile);
    if (file.error.code != 0 && file.error.code != ENOENT) {
        snprintf(job->message, sizeof job->message, "%s", file.error.message);
        return false;
    }
    const char **deps = nullptr;
    uint32_t count = 0;
    char first[smith_exec_name_max] = "";
    const char *output = exec->graph.nodes[smith_plan_output(exec, edge, 0)].path;
    if (file.error.code == 0 && smith_run_parse_depfile(exec, (const char *)file.data.data, file.data.count[0], &deps,
                                                        &count, first, sizeof first)) {
        char folded[smith_exec_name_max];
        snprintf(folded, sizeof folded, "%s", first);
        (void)smith_exec_canonical(folded, strlen(folded));
        if (strcmp(folded, output) != 0) {
            snprintf(job->message, sizeof job->message, "expected depfile '%s' to mention '%s', got '%s'",
                     edge->depfile, output, first);
            return false;
        }
    }
    (void)smith_files_remove(&exec->io->files, edge->depfile);
    static const char *none[1];
    if (deps == nullptr) deps = none;
    smith_state_record_deps(exec, edge, smith_plan_stat_path(output), deps, count);
    return true;
}

// The file system's idea of now, in nanoseconds since the epoch: the clock that modification times come from.
static int64_t smith_run_wall_ns(void) {
    struct timespec now = {};
    clock_gettime(CLOCK_REALTIME, &now);
    return (int64_t)now.tv_sec * (int64_t)smith_exec_nanoseconds + (int64_t)now.tv_nsec;
}

// After a restat step: an output that has the time it had before did not change, and cleans what depended on it only.
// Returns the time to record for the step, as ninja does: when the command started, so that an input modified after
// that makes the output out of date; a restat or generator step that changed an output records that output's time if it
// is later.
static int64_t smith_run_restat(SmithExec *exec, SmithEdge *edge, int64_t started, uint32_t *stack) {
    if (!edge->restat && !edge->generator) return started;
    SmithGraph *graph = &exec->graph;
    int64_t newest = started;
    bool cleaned = false;
    for (uint32_t k = 0; k < edge->output_count; k++) {
        uint32_t number = smith_plan_output(exec, edge, k);
        int64_t fresh = smith_plan_stat_path(graph->nodes[number].path);
        if (fresh > newest) newest = fresh;
        bool same = edge->restat && graph->nodes[number].mtime == fresh;
        graph->nodes[number].mtime = fresh;
        if (same) {
            smith_plan_clean(exec, number, stack);
            cleaned = true;
        }
    }
    return cleaned ? started : newest;
}

// An interrupted command may have left half an output: it is removed when it changed, or when the command has a
// depfile. A file the command did not touch stays.
static void smith_run_remove_partial(SmithExec *exec, const SmithEdge *edge) {
    for (uint32_t k = 0; k < edge->output_count; k++) {
        const SmithNode *node = &exec->graph.nodes[smith_plan_output(exec, edge, k)];
        int64_t fresh = smith_plan_stat_path(node->path);
        if (edge->depfile != nullptr || node->mtime != fresh) (void)smith_files_remove(&exec->io->files, node->path);
    }
    if (edge->depfile != nullptr) (void)smith_files_remove(&exec->io->files, edge->depfile);
}

// ---------------------------------------------------------------- finishing a job

static void smith_run_text(SmithExec *exec, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void smith_run_text(SmithExec *exec, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(exec->scratch, smith_exec_text_max, format, arguments);
    va_end(arguments);
    if (length > 0)
        smith_run_emit(exec, exec->scratch,
                       (size_t)length < smith_exec_text_max ? (size_t)length : smith_exec_text_max - 1);
}

// FAILED: the status and the outputs, the command, then what it printed.
static void smith_run_report_failure(SmithExec *exec, const SmithJob *job, const SmithEdge *edge) {
    size_t used = (size_t)snprintf(exec->scratch, smith_exec_text_max, "FAILED: [code=%d] ", job->status);
    for (uint32_t k = 0; k < edge->output_count && used + smith_exec_name_max < smith_exec_text_max; k++) {
        used += (size_t)snprintf(exec->scratch + used, smith_exec_text_max - used, "%s ",
                                 exec->graph.nodes[smith_plan_output(exec, edge, k)].path);
    }
    // printed as it is: formatting the scratch into itself would overlap
    smith_run_emit(exec, exec->scratch, used);
    smith_run_text(exec, "\n%s\n", edge->command);
    if (job->message[0] != '\0') smith_run_text(exec, "smith: %s\n", job->message);
}

// What a failed job printed, bounded (issue #78): a long output shows its excerpt (smith_quiet_excerpt) and the rest is
// in `.smith/log/<its first output>.log`. False when the output is short, or when this build is verbose, and so is
// printed whole.
static bool smith_run_bound_failure(SmithExec *exec, const SmithJob *job, const SmithEdge *edge) {
    if (exec->options.verbose || smith_quiet_full() ||
        smith_quiet_count_lines(job->output, job->output_size) <= smith_quiet_lines)
        return false;
    char path[PATH_MAX];
    const char *name = edge->output_count > 0 ? exec->graph.nodes[smith_plan_output(exec, edge, 0)].path : "job";
    smith_quiet_log_path(path, sizeof path, name);
    FILE *log = fopen(path, "w");
    if (log != nullptr) {
        fwrite(job->output, 1, job->output_size, log);
        fclose(log);
    }
    static char excerpt[smith_quiet_excerpt_max];
    size_t hidden = 0;
    size_t length = smith_quiet_excerpt(excerpt, job->output, job->output_size, &hidden);
    smith_run_emit(exec, excerpt, length);
    if (log != nullptr)
        smith_run_text(exec, "smith: %zu more lines of this output; the full output is in %s\n", hidden, path);
    return true;
}

static void smith_run_release(SmithExec *exec, SmithJob *job, const SmithEdge *edge) {
    if (edge->pool != 0 && exec->graph.pools[edge->pool - 1].running > 0)
        exec->graph.pools[edge->pool - 1].running -= 1;
    if (edge->pool == smith_exec_pool_console) smith_run_lock(exec, false);
    job->active = false;
    if (job->heavy) exec->heavy_running -= 1;
    smith_slots_give(job->token); // the worker's core goes back to the pool
    job->heavy = false;
    job->token = -1;
    exec->running -= 1;
}

// The job ended: report it, and when it succeeded let the edges that waited for it go.
static void smith_run_finish(SmithExec *exec, SmithJob *job, uint32_t *stack) {
    SmithGraph *graph = &exec->graph;
    SmithEdge *edge = &graph->edges[job->edge];
    uint64_t end_ms = smith_run_now_ms(exec);
    bool success = job->status == 0;
    if (!success && exec->stopped != 0) { // it was stopped by the signal, or was failing as it came: not reported
        smith_run_remove_partial(exec, edge);
        smith_run_release(exec, job, edge);
        return;
    }
    if (success && edge->has_deps && !exec->options.dry_run) success = smith_run_take_depfile(exec, edge, job);
    if (!success && job->status == 0) job->status = smith_exec_failed;
    exec->finished_edges += 1;
    if (edge->pool != smith_exec_pool_console) smith_run_progress(exec, edge);
    if (!success) {
        smith_run_report_failure(exec, job, edge);
        exec->failures += 1;
        if (exec->failures_left > 0) exec->failures_left -= 1;
    }
    if (job->output_size > 0 && (success || !smith_run_bound_failure(exec, job, edge)))
        smith_run_emit(exec, job->output, job->output_size);
    if (job->truncated)
        smith_run_text(exec, "\nsmith: the output of this job was longer than %zu MiB; its end is cut\n",
                       smith_exec_job_arena >> 20);
    if (success) {
        int64_t mtime = exec->options.dry_run ? 0 : smith_run_restat(exec, edge, job->wall, stack);
        if (!exec->options.dry_run) smith_state_record(exec, edge, job->started, end_ms, mtime);
        smith_plan_done(exec, job->edge);
        smith_plan_settle(exec);
    }
    smith_run_release(exec, job, edge);
    if (job->arena.capacity != 0) smith_arena_reset(&job->arena);
}

// ---------------------------------------------------------------- starting jobs

// The ready edge that should start next: the longest chain first, and a pool that is full has to wait.
static size_t smith_run_pick(const SmithExec *exec) {
    const SmithGraph *graph = &exec->graph;
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < exec->ready_count; i++) {
        const SmithEdge *edge = &graph->edges[exec->ready[i]];
        if (edge->pool != 0) {
            const SmithPool *pool = &graph->pools[edge->pool - 1];
            if (pool->depth != 0 && pool->running >= pool->depth) continue;
        }
        if (best == SIZE_MAX) {
            best = i;
            continue;
        }
        const SmithEdge *other = &graph->edges[exec->ready[best]];
        if (edge->weight > other->weight || (edge->weight == other->weight && exec->ready[i] < exec->ready[best]))
            best = i;
    }
    return best;
}

static SmithJob *smith_run_free_job(SmithExec *exec) {
    size_t active = 0;
    for (size_t i = 0; i < (size_t)exec->options.jobs; i++)
        active += exec->jobs[i].active ? 1 : 0;
    if (active >= smith_slots_limit((size_t)exec->options.jobs)) return nullptr; // the slot's share of the cores
    for (size_t i = 0; i < (size_t)exec->options.jobs; i++) {
        if (!exec->jobs[i].active) return &exec->jobs[i];
    }
    return nullptr;
}

// Makes the folder a path is in, as ninja does for every output before its command runs.
static void smith_run_make_parents(SmithExec *exec, const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == nullptr || slash == path) return;
    char folder[smith_exec_name_max];
    size_t length = (size_t)(slash - path);
    if (length >= sizeof folder) return;
    memcpy(folder, path, length);
    folder[length] = '\0';
    (void)smith_files_make_folder(&exec->io->files, folder);
}

static void smith_run_start(SmithExec *exec, SmithJob *job, uint32_t number) {
    SmithEdge *edge = &exec->graph.edges[number];
    edge->want = SmithWant_Running;
    // a function of build.c or the regeneration is not heavy; the first other step takes the host's slot (slots.c)
    if (!exec->options.dry_run && strncmp(edge->command, ".smith/program ", 15) != 0) smith_slots_need();
    if (!exec->options.dry_run) {
        for (uint32_t k = 0; k < edge->output_count; k++)
            smith_run_make_parents(exec, exec->graph.nodes[smith_plan_output(exec, edge, k)].path);
        if (edge->depfile != nullptr) smith_run_make_parents(exec, edge->depfile);
    }
    if (job->arena.capacity == 0) job->arena = smith_arena_carve(exec->arena, smith_exec_job_arena);
    SmithArena arena = job->arena;
    *job = (SmithJob){
        .active = true, .edge = number, .started = smith_run_now_ms(exec), .wall = smith_run_wall_ns(), .arena = arena};
    job->kind = exec->options.dry_run                   ? &smith_job_dry
                : edge->pool == smith_exec_pool_console ? &smith_job_console
                                                        : &smith_job_process;
    exec->started_edges += 1;
    exec->running += 1;
    if (edge->pool != 0) exec->graph.pools[edge->pool - 1].running += 1;
    if (edge->pool == smith_exec_pool_console) {
        const char *what = exec->options.verbose || edge->description == nullptr ? edge->command : edge->description;
        if (!exec->options.quiet)
            smith_run_text(exec, "[%zu/%zu] %s\n", exec->finished_edges + 1, exec->command_edges, what);
        fflush(stdout);
        smith_run_lock(exec, true);
    } else if (exec->printer.smart) {
        smith_run_progress(exec, edge);
    }
    if (!job->kind->start(exec, job)) job->status = smith_exec_failed;
}

// ---------------------------------------------------------------- the loop

static bool smith_run_may_start(const SmithExec *exec) {
    return exec->stopped == 0 && exec->failures_left != 0 && exec->ready_count > 0;
}

static void smith_run_start_ready(SmithExec *exec, uint32_t *stack) {
    for (size_t guard = 0; smith_run_may_start(exec) && guard <= exec->graph.edge_count; guard++) {
        SmithJob *job = smith_run_free_job(exec);
        size_t pick = smith_run_pick(exec);
        if (job == nullptr || pick == SIZE_MAX) return;
        uint32_t number = exec->ready[pick];
        // A heavy step is a worker that needs a core of the host's pool: the first rides on the run's base token, each
        // other takes one more and the step waits (the loop asks again) when none is free (slots.c).
        bool heavy = !exec->options.dry_run && strncmp(exec->graph.edges[number].command, ".smith/program ", 15) != 0;
        int token = smith_slots_unlimited;
        if (heavy) {
            smith_slots_need();
            if (exec->heavy_running > 0) token = smith_slots_extra();
            if (token == smith_slots_denied) return;
        }
        exec->ready[pick] = exec->ready[--exec->ready_count];
        smith_run_start(exec, job, number);
        job->heavy = heavy;
        job->token = token;
        if (heavy) exec->heavy_running += 1;
        bool could_not_start = job->status != 0 && job->message[0] != '\0';
        if (could_not_start || job->kind->poll(exec, job)) smith_run_finish(exec, job, stack);
    }
}

static void smith_run_poll_jobs(SmithExec *exec, uint32_t *stack) {
    for (size_t i = 0; i < (size_t)exec->options.jobs; i++) {
        SmithJob *job = &exec->jobs[i];
        if (job->active && job->kind->poll(exec, job)) smith_run_finish(exec, job, stack);
    }
}

// A signal came in: the running jobs are told, and given a moment to end; the ones that do not are killed.
static void smith_run_stop_jobs(SmithExec *exec, uint32_t *stack, int signal_number) {
    for (size_t i = 0; i < (size_t)exec->options.jobs; i++) {
        if (exec->jobs[i].active) exec->jobs[i].kind->stop(exec, &exec->jobs[i], signal_number);
    }
    for (size_t round = 0; exec->running > 0 && round < smith_run_stop_rounds * 2; round++) {
        (void)smith_io_run(exec->io, smith_run_wait_ns);
        smith_run_poll_jobs(exec, stack);
        if (round == smith_run_stop_rounds) {
            for (size_t i = 0; i < (size_t)exec->options.jobs; i++) {
                if (exec->jobs[i].active) exec->jobs[i].kind->stop(exec, &exec->jobs[i], SIGKILL);
            }
        }
    }
}

// Runs the plan until it is done, a job fails past what -k allows, or a signal stops it. Returns 0, 1 or the status of
// an interrupt.
static int smith_run_plan(SmithExec *exec) {
    uint32_t *stack = smith_exec_push(exec, (exec->graph.node_count + exec->graph.link_count + 8) * sizeof(uint32_t),
                                      alignof(uint32_t));
    exec->origin = smith_clock_now(&exec->io->clock);
    exec->failures_left = exec->options.failures_allowed;
    exec->failures = 0;
    exec->started_edges = 0;
    exec->finished_edges = 0;
    if (exec->jobs == nullptr)
        exec->jobs = smith_exec_push(exec, smith_exec_jobs_max * sizeof(SmithJob), alignof(SmithJob));
    for (size_t round = 0; round < smith_run_loops_max; round++) {
        if (smith_run_signal != 0 && exec->stopped == 0) {
            exec->stopped = smith_run_signal;
            smith_run_stop_jobs(exec, stack, exec->stopped);
        }
        smith_run_start_ready(exec, stack);
        if (exec->running == 0) break;
        fflush(stdout);
        (void)smith_io_run(exec->io, smith_run_wait_ns);
        smith_run_poll_jobs(exec, stack);
    }
    smith_run_printer_close(exec);
    if (exec->stopped != 0) {
        fprintf(stderr, "smith: build stopped: interrupted by user.\n");
        return 128 + exec->stopped;
    }
    if (exec->failures == 0) return 0;
    fprintf(stderr, "smith: build stopped: %s.\n",
            exec->failures_left == 0 ? "subcommand failed" : "cannot make progress due to previous errors");
    return smith_exec_failed;
}

// Plans the targets and runs the plan. Prints that there is nothing to do when there is not.
static int smith_run_targets(SmithExec *exec, const uint32_t *targets, size_t count, bool say_done) {
    if (!smith_plan_build(exec, targets, count)) {
        if (say_done && !exec->options.quiet) printf("smith: no work to do.\n");
        return 0;
    }
    return smith_run_plan(exec);
}

// Whether the manifest has a rule of its own: the node, or UINT32_MAX when nothing makes it.
static uint32_t smith_run_manifest_node(SmithExec *exec) {
    char folded[smith_exec_name_max];
    size_t length = strlen(exec->manifest);
    if (length >= sizeof folded) return UINT32_MAX;
    memcpy(folded, exec->manifest, length + 1);
    length = smith_exec_canonical(folded, length);
    uint32_t node = smith_exec_node(exec, folded, length, false, nullptr);
    if (node == UINT32_MAX || exec->graph.nodes[node].edge == 0) return UINT32_MAX;
    return node;
}
