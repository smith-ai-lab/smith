// What smith keeps between builds, in the manifest's builddir (.smith). It is what ninja keeps in .ninja_log and
// .ninja_deps, in text, in files of smith's own names, so a tree that ninja built is read as never built: the first
// build runs every command, and never trusts a file that smith did not write.
//
//   .smith_log   "# smith log 1", then one line per output and run: <start ms> <end ms> <mtime ns> <hash> <path>.
//                The hash is FNV-1a 64 of the command. mtime is the time the command started, on the file system's
//                clock: an input modified after that, even while the command ran, makes the output out of date. (A
//                restat step that changed an output records the output's time, if later.) The last line of a path
//                wins.
//   .smith_deps  "# smith deps 1", then one record per output and run: "<mtime ns> <count> <path>" and <count> lines,
//                one path each: what the depfile of the command named. mtime is the output's modification time
//                after the command. The last record of a path wins.
//
// Both are appended to as jobs end, and written whole (through a temporary file) when more than two thirds of their
// lines are dead. A line cut short by a crash is not read, and neither is a file of another version.

constexpr size_t smith_state_entries_max = (size_t)1 << 20;
constexpr size_t smith_state_deps_max = (size_t)1 << 23;
constexpr size_t smith_state_compact_ratio = 3;
constexpr size_t smith_state_compact_floor = 100;

// Finds a path in a table whose entries start with their path. Returns the slot, whose value is the entry number plus
// one, or 0 when the path is not there.
static size_t smith_state_slot(const uint32_t *table, size_t capacity, const void *entries, size_t stride,
                               const char *path, size_t length) {
    size_t slot = smith_exec_hash(path, length) & (capacity - 1);
    for (size_t probe = 0; probe < capacity; probe++) {
        uint32_t found = table[slot];
        if (found == 0) return slot;
        const char *other = *(const char *const *)((const unsigned char *)entries + (size_t)(found - 1) * stride);
        if (strlen(other) == length && memcmp(other, path, length) == 0) return slot;
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

static void smith_state_rebuild_table(SmithExec *exec, uint32_t **table, size_t *capacity, const void *entries,
                                      size_t stride, size_t count) {
    size_t bigger = *capacity == 0 ? 4096 : *capacity * 2;
    while (bigger < count * 2 + 2)
        bigger *= 2;
    uint32_t *fresh = smith_exec_push(exec, bigger * sizeof(uint32_t), alignof(uint32_t));
    memset(fresh, 0, bigger * sizeof(uint32_t));
    for (size_t i = 0; i < count; i++) {
        const char *path = *(const char *const *)((const unsigned char *)entries + i * stride);
        size_t slot = smith_state_slot(fresh, bigger, entries, stride, path, strlen(path));
        fresh[slot] = (uint32_t)i + 1;
    }
    *table = fresh;
    *capacity = bigger;
}

// Puts an entry in the log (the last of a path wins). Returns whether the path was there before.
static bool smith_state_log_set(SmithExec *exec, const SmithLogEntry *entry) {
    SmithState *state = &exec->state;
    if ((state->log_count + 1) * 2 > state->log_capacity)
        smith_state_rebuild_table(exec, &state->log_table, &state->log_capacity, state->log, sizeof(SmithLogEntry),
                                  state->log_count);
    size_t slot = smith_state_slot(state->log_table, state->log_capacity, state->log, sizeof(SmithLogEntry),
                                   entry->path, strlen(entry->path));
    if (state->log_table[slot] != 0) {
        state->log[state->log_table[slot] - 1] = *entry;
        return true;
    }
    if (state->log_count >= smith_state_entries_max) smith_exec_fail("%s: too many outputs", state->log_path);
    state->log[state->log_count++] = *entry;
    state->log_table[slot] = (uint32_t)state->log_count;
    return false;
}

static bool smith_state_deps_set(SmithExec *exec, const SmithDepsEntry *entry) {
    SmithState *state = &exec->state;
    if ((state->deps_count + 1) * 2 > state->deps_capacity)
        smith_state_rebuild_table(exec, &state->deps_table, &state->deps_capacity, state->deps, sizeof(SmithDepsEntry),
                                  state->deps_count);
    size_t slot = smith_state_slot(state->deps_table, state->deps_capacity, state->deps, sizeof(SmithDepsEntry),
                                   entry->path, strlen(entry->path));
    if (state->deps_table[slot] != 0) {
        state->deps[state->deps_table[slot] - 1] = *entry;
        return true;
    }
    if (state->deps_count >= smith_state_entries_max) smith_exec_fail("%s: too many outputs", state->deps_path);
    state->deps[state->deps_count++] = *entry;
    state->deps_table[slot] = (uint32_t)state->deps_count;
    return false;
}

static const SmithLogEntry *smith_state_log_find(SmithExec *exec, const char *path) {
    const SmithState *state = &exec->state;
    if (state->log_count == 0) return nullptr;
    size_t slot =
        smith_state_slot(state->log_table, state->log_capacity, state->log, sizeof(SmithLogEntry), path, strlen(path));
    return state->log_table[slot] != 0 ? &state->log[state->log_table[slot] - 1] : nullptr;
}

static const SmithDepsEntry *smith_state_deps_find(SmithExec *exec, const char *path) {
    const SmithState *state = &exec->state;
    if (state->deps_count == 0) return nullptr;
    size_t slot = smith_state_slot(state->deps_table, state->deps_capacity, state->deps, sizeof(SmithDepsEntry), path,
                                   strlen(path));
    return state->deps_table[slot] != 0 ? &state->deps[state->deps_table[slot] - 1] : nullptr;
}

// The next complete line of the text (ended by a newline): its start, ended by a 0 byte. nullptr at the end, and for a
// last line that was cut short.
static char *smith_state_line(char **cursor, const char *end) {
    if (*cursor >= end) return nullptr;
    char *newline = memchr(*cursor, '\n', (size_t)(end - *cursor));
    if (newline == nullptr) return nullptr;
    *newline = '\0';
    char *line = *cursor;
    *cursor = newline + 1;
    return line;
}

// A decimal number at the start of *p, then a space. False if it is not one.
static bool smith_state_number(char **p, int64_t *value) {
    char *end = nullptr;
    errno = 0;
    long long number = strtoll(*p, &end, 10);
    if (end == *p || errno != 0 || *end != ' ') return false;
    *value = number;
    *p = end + 1;
    return true;
}

static void smith_state_load_log(SmithExec *exec, char *text, const char *end) {
    SmithState *state = &exec->state;
    char *cursor = text;
    char *line = smith_state_line(&cursor, end); // the header was checked by the caller
    for (size_t guard = 0; line != nullptr && guard <= (size_t)(end - text); guard++) {
        line = smith_state_line(&cursor, end);
        if (line == nullptr) break;
        int64_t start = 0;
        int64_t finish = 0;
        int64_t mtime = 0;
        char *p = line;
        if (!smith_state_number(&p, &start) || !smith_state_number(&p, &finish) || !smith_state_number(&p, &mtime))
            continue;
        char *stop = nullptr;
        errno = 0;
        unsigned long long hash = strtoull(p, &stop, 16);
        if (stop == p || errno != 0 || *stop != ' ' || stop[1] == '\0') continue;
        state->log_lines += 1;
        (void)smith_state_log_set(exec, &(SmithLogEntry){.path = stop + 1,
                                                         .mtime = mtime,
                                                         .hash = hash,
                                                         .start_ms = (uint32_t)start,
                                                         .end_ms = (uint32_t)finish});
    }
}

static void smith_state_load_deps(SmithExec *exec, char *text, const char *end) {
    SmithState *state = &exec->state;
    char *cursor = text;
    char *line = smith_state_line(&cursor, end);
    for (size_t guard = 0; line != nullptr && guard <= (size_t)(end - text); guard++) {
        line = smith_state_line(&cursor, end);
        if (line == nullptr) break;
        int64_t mtime = 0;
        int64_t count = 0;
        char *p = line;
        if (!smith_state_number(&p, &mtime) || !smith_state_number(&p, &count) || count < 0 || *p == '\0') continue;
        const char **deps =
            (const char **)smith_exec_push(exec, (size_t)(count + 1) * sizeof(const char *), alignof(const char *));
        bool whole = true;
        for (int64_t i = 0; i < count && whole; i++) {
            char *dep = smith_state_line(&cursor, end);
            whole = dep != nullptr;
            deps[i] = dep;
        }
        if (!whole) break; // a record cut short is the last
        state->deps_records += 1;
        (void)smith_state_deps_set(
            exec, &(SmithDepsEntry){.path = p, .mtime = mtime, .deps = deps, .dep_count = (uint32_t)count});
    }
}

static bool smith_state_has_header(const SmithRead *file, const char *header) {
    size_t length = strlen(header);
    return file->error.code == 0 && file->data.count[0] >= length && memcmp(file->data.data, header, length) == 0;
}

// Writes the log as it is in memory, with only live lines.
static void smith_state_compact_log(SmithExec *exec) {
    SmithState *state = &exec->state;
    SmithGrow grow = {.arena = exec->arena};
    smith_grow_append(&grow, "# smith log 1\n", 14);
    char line[smith_exec_name_max + 128];
    for (size_t i = 0; i < state->log_count; i++) {
        const SmithLogEntry *entry = &state->log[i];
        int length = snprintf(line, sizeof line, "%u %u %lld %016llx %s\n", entry->start_ms, entry->end_ms,
                              (long long)entry->mtime, (unsigned long long)entry->hash, entry->path);
        SMITH_ASSERT(length > 0 && (size_t)length < sizeof line);
        smith_grow_append(&grow, line, (size_t)length);
    }
    SmithError error = smith_files_write(
        &exec->io->files, state->log_path,
        (struct smith_array_const_u8_n){.data = (const uint8_t *)(grow.data), .count = {grow.size}}, SmithWrite_Atomic);
    if (error.code != 0) smith_exec_fail("%s", error.message);
    state->log_lines = state->log_count;
}

static void smith_state_compact_deps(SmithExec *exec) {
    SmithState *state = &exec->state;
    SmithGrow grow = {.arena = exec->arena};
    smith_grow_append(&grow, "# smith deps 1\n", 15);
    char line[smith_exec_name_max + 64];
    for (size_t i = 0; i < state->deps_count; i++) {
        const SmithDepsEntry *entry = &state->deps[i];
        int length =
            snprintf(line, sizeof line, "%lld %u %s\n", (long long)entry->mtime, entry->dep_count, entry->path);
        SMITH_ASSERT(length > 0 && (size_t)length < sizeof line);
        smith_grow_append(&grow, line, (size_t)length);
        for (uint32_t d = 0; d < entry->dep_count; d++) {
            smith_grow_append(&grow, entry->deps[d], strlen(entry->deps[d]));
            smith_grow_append(&grow, "\n", 1);
        }
    }
    SmithError error = smith_files_write(
        &exec->io->files, state->deps_path,
        (struct smith_array_const_u8_n){.data = (const uint8_t *)(grow.data), .count = {grow.size}}, SmithWrite_Atomic);
    if (error.code != 0) smith_exec_fail("%s", error.message);
    state->deps_records = state->deps_count;
}

static bool smith_state_wasteful(size_t lines, size_t live) {
    return lines > smith_state_compact_floor && lines > live * smith_state_compact_ratio;
}

// Loads both logs from the builddir, making the folder if it is missing.
static void smith_state_open(SmithExec *exec) {
    SmithState *state = &exec->state;
    const char *builddir = exec->graph.builddir;
    (void)smith_files_make_folder(&exec->io->files, builddir);
    size_t length = strlen(builddir) + 32;
    char *log_path = smith_exec_push(exec, length, 1);
    char *deps_path = smith_exec_push(exec, length, 1);
    snprintf(log_path, length, "%s/.smith_log", builddir);
    snprintf(deps_path, length, "%s/.smith_deps", builddir);
    *state = (SmithState){.log_path = log_path, .deps_path = deps_path, .ready = true};
    state->log = smith_exec_carve(exec, smith_state_entries_max * sizeof(SmithLogEntry));
    state->deps = smith_exec_carve(exec, smith_state_entries_max * sizeof(SmithDepsEntry));
    SmithRead log = smith_files_read(&exec->io->files, exec->arena, log_path);
    if (smith_state_has_header(&log, "# smith log 1\n")) {
        smith_state_load_log(exec, (char *)(uintptr_t)log.data.data, (const char *)log.data.data + log.data.count[0]);
        if (smith_state_wasteful(state->log_lines, state->log_count) && !exec->options.dry_run)
            smith_state_compact_log(exec);
    } else if (log.error.code == 0 && !exec->options.dry_run) {
        (void)smith_files_write(
            &exec->io->files, log_path,
            (struct smith_array_const_u8_n){.data = (const uint8_t *)("# smith log 1\n"), .count = {14}},
            SmithWrite_Atomic);
    }
    SmithRead deps = smith_files_read(&exec->io->files, exec->arena, deps_path);
    if (smith_state_has_header(&deps, "# smith deps 1\n")) {
        smith_state_load_deps(exec, (char *)(uintptr_t)deps.data.data,
                              (const char *)deps.data.data + deps.data.count[0]);
        if (smith_state_wasteful(state->deps_records, state->deps_count) && !exec->options.dry_run)
            smith_state_compact_deps(exec);
    } else if (deps.error.code == 0 && !exec->options.dry_run) {
        (void)smith_files_write(
            &exec->io->files, deps_path,
            (struct smith_array_const_u8_n){.data = (const uint8_t *)("# smith deps 1\n"), .count = {15}},
            SmithWrite_Atomic);
    }
}

// Appends a line of text to a state file, making it with its header when it is not there.
static void smith_state_append(SmithExec *exec, const char *path, const char *header, const char *text, size_t length) {
    if (exec->options.dry_run) return;
    SmithFileInfo info = smith_files_info(&exec->io->files, path);
    if (info.error.code != 0)
        (void)smith_files_write(
            &exec->io->files, path,
            (struct smith_array_const_u8_n){.data = (const uint8_t *)(header), .count = {strlen(header)}},
            SmithWrite_Append);
    SmithError error = smith_files_write(
        &exec->io->files, path, (struct smith_array_const_u8_n){.data = (const uint8_t *)(text), .count = {length}},
        SmithWrite_Append);
    if (error.code != 0) smith_exec_fail("%s", error.message);
}

// Records that a command ran: for each output, its run and its mtime.
static void smith_state_record(SmithExec *exec, const SmithEdge *edge, uint64_t start_ms, uint64_t end_ms,
                               int64_t mtime) {
    SmithState *state = &exec->state;
    SmithGrow grow = {.arena = exec->arena};
    for (uint32_t i = 0; i < edge->output_count; i++) {
        const char *path = exec->graph.nodes[exec->graph.ids[edge->outputs + i]].path;
        SmithLogEntry entry = {.path = smith_exec_copy(exec, path, strlen(path)),
                               .mtime = mtime,
                               .hash = edge->command_hash,
                               .start_ms = (uint32_t)start_ms,
                               .end_ms = (uint32_t)end_ms};
        (void)smith_state_log_set(exec, &entry);
        state->log_lines += 1;
        char line[smith_exec_name_max + 128];
        int length = snprintf(line, sizeof line, "%u %u %lld %016llx %s\n", entry.start_ms, entry.end_ms,
                              (long long)mtime, (unsigned long long)entry.hash, path);
        SMITH_ASSERT(length > 0 && (size_t)length < sizeof line);
        smith_grow_append(&grow, line, (size_t)length);
    }
    smith_state_append(exec, state->log_path, "# smith log 1\n", (const char *)grow.data, grow.size);
}

// Records what a command's depfile named, for each output of the edge.
static void smith_state_record_deps(SmithExec *exec, const SmithEdge *edge, int64_t mtime, const char *const *deps,
                                    uint32_t count) {
    SmithState *state = &exec->state;
    SmithGrow grow = {.arena = exec->arena};
    char line[smith_exec_name_max + 64];
    for (uint32_t i = 0; i < edge->output_count; i++) {
        const char *path = smith_exec_copy(exec, exec->graph.nodes[exec->graph.ids[edge->outputs + i]].path,
                                           strlen(exec->graph.nodes[exec->graph.ids[edge->outputs + i]].path));
        (void)smith_state_deps_set(exec,
                                   &(SmithDepsEntry){.path = path, .mtime = mtime, .deps = deps, .dep_count = count});
        state->deps_records += 1;
        int length = snprintf(line, sizeof line, "%lld %u %s\n", (long long)mtime, count, path);
        SMITH_ASSERT(length > 0 && (size_t)length < sizeof line);
        smith_grow_append(&grow, line, (size_t)length);
        for (uint32_t d = 0; d < count; d++) {
            smith_grow_append(&grow, deps[d], strlen(deps[d]));
            smith_grow_append(&grow, "\n", 1);
        }
    }
    smith_state_append(exec, state->deps_path, "# smith deps 1\n", (const char *)grow.data, grow.size);
}
