// The reader of the manifest. It is the subset of ninja's language that smith writes: `rule`, `build` (with `|` and
// `||` inputs), `pool`, `default` and top-level variables; `$$`, `$ `, `$:`, `$name` and `${name}`; edge variables;
// continuation lines. `include`, `subninja` and a variable that a rule's text takes from another rule's are not read
// (they are errors): smith writes none of them, and SMITH_NINJA runs a manifest that has them through ninja itself.

typedef struct {
    char *cursor;
    char *end;
    size_t line;
} SmithLexer;

// What a value can name: the edge's own variables, and its $in and $out.
typedef struct {
    const SmithBinding *bindings;
    size_t count;
    const SmithEdge *edge;
} SmithScope;

typedef enum {
    SmithBlock_None,
    SmithBlock_Rule,
    SmithBlock_Build,
    SmithBlock_Pool,
} SmithBlockKind;

// The statement whose indented lines are being read.
typedef struct {
    SmithBlockKind kind;
    SmithRule *rule;
    SmithPool *pool;
    uint32_t edge;
    const SmithRule *edge_rule;
    SmithBinding bindings[smith_exec_bindings_max];
    size_t count;
} SmithBlock;

[[noreturn]] static void smith_manifest_fail(const SmithExec *exec, const SmithLexer *lexer, const char *message,
                                             const char *detail) {
    fflush(stdout);
    fprintf(stderr, "smith: error: %s:%zu: %s%s%s\n", exec->manifest, lexer->line, message,
            detail != nullptr ? ": " : "", detail != nullptr ? detail : "");
    exit(smith_exec_failed);
}

static bool smith_manifest_is_space(char c) {
    return c == ' ' || c == '\t';
}

// The number of `$` just before position `end`, back to `start`.
static size_t smith_manifest_dollars(const char *start, const char *end) {
    size_t count = 0;
    while (end - count > start && end[-(ptrdiff_t)count - 1] == '$')
        count += 1;
    return count;
}

// The next line that holds something: its indent and its text, ended by a 0 byte. A line that ends in an odd number of
// `$` goes on in the next one, whose leading blanks are dropped. False at the end of the manifest.
static bool smith_manifest_next(SmithLexer *lexer, size_t *indent, char **text) {
    for (size_t guard = 0; lexer->cursor < lexer->end && guard <= (size_t)(lexer->end - lexer->cursor); guard++) {
        char *line = lexer->cursor;
        char *newline = memchr(line, '\n', (size_t)(lexer->end - line));
        lexer->line += 1;
        // A continuation moves the rest of the manifest left over the `$` and the newline.
        for (size_t joins = 0; newline != nullptr && joins <= (size_t)(lexer->end - line) &&
                               smith_manifest_dollars(line, newline) % 2 == 1;
             joins++) {
            char *rest = newline + 1;
            while (rest < lexer->end && smith_manifest_is_space(*rest))
                rest += 1;
            memmove(newline - 1, rest, (size_t)(lexer->end - rest) + 1);
            lexer->end -= (rest - (newline - 1));
            lexer->line += 1;
            newline = memchr(newline - 1, '\n', (size_t)(lexer->end - (newline - 1)));
        }
        if (newline != nullptr) {
            *newline = '\0';
            lexer->cursor = newline + 1;
        } else {
            lexer->cursor = lexer->end;
        }
        size_t blanks = 0;
        while (smith_manifest_is_space(line[blanks]))
            blanks += 1;
        if (line[blanks] == '\0' || line[blanks] == '#' || line[blanks] == '\r') continue;
        *indent = blanks;
        *text = line + blanks;
        return true;
    }
    return false;
}

static bool smith_manifest_is_name_byte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

// The end of the word that starts at `p`, where an unescaped byte of `stops` or a blank or the end ends it.
static const char *smith_manifest_word_end(const char *p, const char *stops) {
    for (size_t guard = 0; *p != '\0' && guard < smith_exec_text_max; guard++) {
        if (*p == '$') {
            if (p[1] == '\0') return p + 1;
            if (p[1] == '{') {
                const char *close = strchr(p, '}');
                p = close != nullptr ? close + 1 : p + 2;
            } else {
                p += 2;
            }
            continue;
        }
        if (smith_manifest_is_space(*p) || strchr(stops, *p) != nullptr) break;
        p += 1;
    }
    return p;
}

static const char *smith_manifest_variable(const SmithExec *exec, const char *name, size_t length) {
    const SmithGraph *graph = &exec->graph;
    for (size_t i = graph->variable_count; i > 0; i--) { // the latest assignment wins
        const SmithBinding *variable = &graph->variables[i - 1];
        if (strlen(variable->key) == length && memcmp(variable->key, name, length) == 0) return variable->value;
    }
    return nullptr;
}

static const char *smith_manifest_binding(const SmithScope *scope, const char *name, size_t length) {
    for (size_t i = scope->count; i > 0; i--) {
        const SmithBinding *binding = &scope->bindings[i - 1];
        if (strlen(binding->key) == length && memcmp(binding->key, name, length) == 0) return binding->value;
    }
    return nullptr;
}

// Appends to out (at *used) one path, quoted for sh unless every byte is plain.
static void smith_manifest_put_path(SmithExec *exec, char *out, size_t *used, const char *path) {
    bool plain = *path != '\0';
    for (const char *c = path; *c != '\0'; c++)
        plain = plain && (smith_manifest_is_name_byte(*c) || strchr("+,./:=@", *c) != nullptr);
    size_t length = strlen(path);
    if (*used + 2 * length + 3 > smith_exec_text_max)
        smith_exec_fail("%s: a value is longer than it can be", exec->manifest);
    if (!plain) out[(*used)++] = '\'';
    for (size_t i = 0; i < length; i++) {
        if (path[i] == '\'') {
            memcpy(out + *used, "'\\''", 4);
            *used += 4;
        } else {
            out[(*used)++] = path[i];
        }
    }
    if (!plain) out[(*used)++] = '\'';
}

// $in or $out: the edge's explicit inputs or outputs, with a space between them.
static void smith_manifest_put_paths(SmithExec *exec, char *out, size_t *used, const SmithEdge *edge, bool inputs) {
    uint32_t start = inputs ? edge->inputs : edge->outputs;
    uint32_t count = inputs ? edge->explicit_count : edge->explicit_outputs;
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) out[(*used)++] = ' ';
        smith_manifest_put_path(exec, out, used, exec->graph.nodes[exec->graph.ids[start + i]].path);
    }
}

// Appends the value of the variable `name`: $in and $out, then the edge's variables, then the manifest's. One that
// is not set is empty.
static void smith_manifest_put_variable(SmithExec *exec, const SmithScope *scope, const char *name, size_t length,
                                        char *out, size_t *used) {
    if (scope->edge != nullptr && length == 2 && memcmp(name, "in", 2) == 0) {
        smith_manifest_put_paths(exec, out, used, scope->edge, true);
        return;
    }
    if (scope->edge != nullptr && length == 3 && memcmp(name, "out", 3) == 0) {
        smith_manifest_put_paths(exec, out, used, scope->edge, false);
        return;
    }
    const char *value = smith_manifest_binding(scope, name, length);
    if (value == nullptr) value = smith_manifest_variable(exec, name, length);
    if (value == nullptr) return;
    size_t size = strlen(value);
    if (*used + size + 1 > smith_exec_text_max) smith_exec_fail("%s: a value is longer than it can be", exec->manifest);
    memcpy(out + *used, value, size);
    *used += size;
}

// Expands the text of one value into out, which is NUL-terminated. Returns the length.
static size_t smith_manifest_eval(SmithExec *exec, const char *text, size_t length, const SmithScope *scope,
                                  char *out) {
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        if (used + 2 > smith_exec_text_max) smith_exec_fail("%s: a value is longer than it can be", exec->manifest);
        if (text[i] != '$' || i + 1 >= length) {
            out[used++] = text[i++];
        } else if (text[i + 1] == '$' || text[i + 1] == ' ' || text[i + 1] == ':') {
            out[used++] = text[i + 1];
            i += 2;
        } else if (text[i + 1] == '{') {
            const char *close = memchr(text + i, '}', length - i);
            if (close == nullptr) smith_exec_fail("%s: a ${ without a }", exec->manifest);
            smith_manifest_put_variable(exec, scope, text + i + 2, (size_t)(close - (text + i + 2)), out, &used);
            i = (size_t)(close - text) + 1;
        } else {
            size_t name = i + 1;
            size_t end = name;
            while (end < length && smith_manifest_is_name_byte(text[end]))
                end += 1;
            if (end == name) smith_exec_fail("%s: a bad $ escape in '%.*s'", exec->manifest, (int)length, text);
            smith_manifest_put_variable(exec, scope, text + name, end - name, out, &used);
            i = end;
        }
    }
    out[used] = '\0';
    return used;
}

// ---------------------------------------------------------------- nodes

static void smith_manifest_grow_table(SmithExec *exec) {
    SmithGraph *graph = &exec->graph;
    size_t capacity = graph->table_capacity == 0 ? 4096 : graph->table_capacity * 2;
    uint32_t *table = smith_exec_push(exec, capacity * sizeof(uint32_t), alignof(uint32_t));
    memset(table, 0, capacity * sizeof(uint32_t));
    for (size_t i = 0; i < graph->node_count; i++) {
        const char *path = graph->nodes[i].path;
        size_t slot = smith_exec_hash(path, strlen(path)) & (capacity - 1);
        for (size_t probe = 0; table[slot] != 0 && probe < capacity; probe++)
            slot = (slot + 1) & (capacity - 1);
        table[slot] = (uint32_t)i + 1;
    }
    graph->table = table;
    graph->table_capacity = capacity;
}

// The node for a canonical path, made if there is none. `created` says whether it is new.
static uint32_t smith_exec_node(SmithExec *exec, const char *path, size_t length, bool create, bool *created) {
    SmithGraph *graph = &exec->graph;
    if (created != nullptr) *created = false;
    if (graph->table == nullptr || (graph->node_count + 1) * 2 > graph->table_capacity) {
        if (!create && graph->table == nullptr) return UINT32_MAX;
        smith_manifest_grow_table(exec);
    }
    size_t slot = smith_exec_hash(path, length) & (graph->table_capacity - 1);
    for (size_t probe = 0; probe < graph->table_capacity; probe++) {
        uint32_t found = graph->table[slot];
        if (found == 0) break;
        const char *other = graph->nodes[found - 1].path;
        if (strlen(other) == length && memcmp(other, path, length) == 0) return found - 1;
        slot = (slot + 1) & (graph->table_capacity - 1);
    }
    if (!create) return UINT32_MAX;
    if (graph->node_count >= smith_exec_nodes_max) smith_exec_fail("%s: too many files", exec->manifest);
    uint32_t number = (uint32_t)graph->node_count++;
    graph->nodes[number] = (SmithNode){.path = smith_exec_copy(exec, path, length), .mtime = -1};
    graph->table[slot] = number + 1;
    if (created != nullptr) *created = true;
    return number;
}

// The node of a path that is not canonical yet: it is folded in the scratch buffer first.
static uint32_t smith_exec_node_of(SmithExec *exec, const char *path, size_t length) {
    if (length >= smith_exec_name_max) smith_exec_fail("%s: a path is longer than it can be", exec->manifest);
    char folded[smith_exec_name_max];
    memcpy(folded, path, length);
    folded[length] = '\0';
    length = smith_exec_canonical(folded, length);
    return smith_exec_node(exec, folded, length, true, nullptr);
}

// ---------------------------------------------------------------- statements

static uint32_t smith_manifest_read_path(SmithExec *exec, const char *start, const char *end) {
    size_t length = smith_manifest_eval(exec, start, (size_t)(end - start), &(SmithScope){}, exec->scratch);
    return smith_exec_node_of(exec, exec->scratch, length);
}

static uint32_t smith_manifest_push_id(SmithExec *exec, uint32_t node) {
    SmithGraph *graph = &exec->graph;
    if (graph->id_count >= smith_exec_ids_max) smith_exec_fail("%s: too many inputs and outputs", exec->manifest);
    graph->ids[graph->id_count] = node;
    return (uint32_t)graph->id_count++;
}

// Reads the paths that follow in the line until one of `stops`, appending their nodes to the ids.
static uint32_t smith_manifest_read_paths(SmithExec *exec, const char **p, const char *stops) {
    uint32_t count = 0;
    for (size_t guard = 0; guard < smith_exec_ids_max; guard++) {
        while (smith_manifest_is_space(**p))
            *p += 1;
        if (**p == '\0' || strchr(stops, **p) != nullptr) break;
        const char *end = smith_manifest_word_end(*p, stops);
        if (end == *p) break;
        smith_manifest_push_id(exec, smith_manifest_read_path(exec, *p, end));
        *p = end;
        count += 1;
    }
    return count;
}

static SmithRule *smith_manifest_find_rule(const SmithGraph *graph, const char *name, size_t length) {
    for (size_t i = 0; i < graph->rule_count; i++) {
        if (strlen(graph->rules[i].name) == length && memcmp(graph->rules[i].name, name, length) == 0)
            return &graph->rules[i];
    }
    return nullptr;
}

// Splits `name = value`: the name, and the value's text after the blanks.
static bool smith_manifest_assignment(char *text, char **name, size_t *name_length, char **value) {
    char *p = text;
    while (smith_manifest_is_name_byte(*p) || *p == '.')
        p += 1;
    if (p == text) return false;
    char *after = p;
    while (smith_manifest_is_space(*p))
        p += 1;
    if (*p != '=') return false;
    *name = text;
    *name_length = (size_t)(after - text);
    p += 1;
    while (smith_manifest_is_space(*p))
        p += 1;
    *value = p;
    return true;
}

static void smith_manifest_start_rule(SmithExec *exec, SmithLexer *lexer, SmithBlock *block, const char *p) {
    SmithGraph *graph = &exec->graph;
    while (smith_manifest_is_space(*p))
        p += 1;
    const char *end = smith_manifest_word_end(p, "");
    if (end == p) smith_manifest_fail(exec, lexer, "rule needs a name", nullptr);
    if (smith_manifest_find_rule(graph, p, (size_t)(end - p)) != nullptr ||
        (end - p == 5 && memcmp(p, "phony", 5) == 0))
        smith_manifest_fail(exec, lexer, "a rule is defined twice", p);
    if (graph->rule_count >= smith_exec_rules_max) smith_manifest_fail(exec, lexer, "too many rules", nullptr);
    SmithRule *rule = &graph->rules[graph->rule_count++];
    *rule = (SmithRule){.name = smith_exec_copy(exec, p, (size_t)(end - p))};
    *block = (SmithBlock){.kind = SmithBlock_Rule, .rule = rule};
}

static void smith_manifest_start_pool(SmithExec *exec, SmithLexer *lexer, SmithBlock *block, const char *p) {
    SmithGraph *graph = &exec->graph;
    while (smith_manifest_is_space(*p))
        p += 1;
    const char *end = smith_manifest_word_end(p, "");
    if (end == p) smith_manifest_fail(exec, lexer, "pool needs a name", nullptr);
    if (graph->pool_count >= smith_exec_pools_max) smith_manifest_fail(exec, lexer, "too many pools", nullptr);
    for (size_t i = 0; i < graph->pool_count; i++) {
        if (strlen(graph->pools[i].name) == (size_t)(end - p) &&
            memcmp(graph->pools[i].name, p, (size_t)(end - p)) == 0)
            smith_manifest_fail(exec, lexer, "a pool is defined twice", p);
    }
    SmithPool *pool = &graph->pools[graph->pool_count++];
    *pool = (SmithPool){.name = smith_exec_copy(exec, p, (size_t)(end - p)), .depth = 0};
    *block = (SmithBlock){.kind = SmithBlock_Pool, .pool = pool};
}

// `build outputs [| implicit outputs] : rule inputs [| implicit] [|| order-only]`: makes the edge; its variables and
// the evaluation of its rule wait for the lines that follow.
static void smith_manifest_start_build(SmithExec *exec, SmithLexer *lexer, SmithBlock *block, const char *p) {
    SmithGraph *graph = &exec->graph;
    if (graph->edge_count + 1 >= smith_exec_edges_max)
        smith_manifest_fail(exec, lexer, "too many build statements", nullptr);
    uint32_t number = (uint32_t)++graph->edge_count;
    SmithEdge *edge = &graph->edges[number];
    *edge = (SmithEdge){};
    edge->outputs = (uint32_t)graph->id_count;
    edge->explicit_outputs = smith_manifest_read_paths(exec, &p, ":|");
    edge->output_count = edge->explicit_outputs;
    if (*p == '|') {
        p += 1;
        edge->output_count += smith_manifest_read_paths(exec, &p, ":");
    }
    if (*p != ':' || edge->output_count == 0)
        smith_manifest_fail(exec, lexer, "a build statement needs outputs and a ':'", nullptr);
    p += 1;
    while (smith_manifest_is_space(*p))
        p += 1;
    const char *end = smith_manifest_word_end(p, "");
    SmithRule *rule = smith_manifest_find_rule(graph, p, (size_t)(end - p));
    edge->phony = end - p == 5 && memcmp(p, "phony", 5) == 0;
    if (rule == nullptr && !edge->phony) smith_manifest_fail(exec, lexer, "unknown build rule", p);
    edge->rule = edge->phony ? "phony" : rule->name;
    p = end;
    edge->inputs = (uint32_t)graph->id_count;
    edge->explicit_count = smith_manifest_read_paths(exec, &p, "|");
    if (*p == '|' && p[1] != '|') {
        p += 1;
        edge->implicit_count = smith_manifest_read_paths(exec, &p, "|");
    }
    if (p[0] == '|' && p[1] == '|') {
        p += 2;
        edge->order_count = smith_manifest_read_paths(exec, &p, "");
    }
    if (*p != '\0') smith_manifest_fail(exec, lexer, "unexpected text in a build statement", p);
    *block = (SmithBlock){.kind = SmithBlock_Build, .edge = number, .edge_rule = rule};
}

static bool smith_manifest_is_set(const char *value) {
    return value != nullptr && *value != '\0';
}

// A variable of the edge: its own, else the rule's text evaluated for the edge. nullptr when neither has it.
static const char *smith_manifest_edge_variable(SmithExec *exec, const SmithBlock *block, const char *name) {
    const SmithScope scope = {
        .bindings = block->bindings, .count = block->count, .edge = &exec->graph.edges[block->edge]};
    const char *own = smith_manifest_binding(&scope, name, strlen(name));
    if (own != nullptr) return own;
    const SmithRule *rule = block->edge_rule;
    for (size_t i = 0; rule != nullptr && i < rule->count; i++) {
        if (strcmp(rule->bindings[i].key, name) != 0) continue;
        const char *raw = rule->bindings[i].value;
        size_t length = smith_manifest_eval(exec, raw, strlen(raw), &scope, exec->scratch);
        return smith_exec_copy(exec, exec->scratch, length);
    }
    return nullptr;
}

static uint32_t smith_manifest_pool_number(SmithExec *exec, const SmithLexer *lexer, const char *name) {
    const SmithGraph *graph = &exec->graph;
    for (size_t i = 0; i < graph->pool_count; i++) {
        if (strcmp(graph->pools[i].name, name) == 0) return (uint32_t)i + 1;
    }
    smith_manifest_fail(exec, lexer, "an unknown pool", name);
}

// The end of an edge's block: the command, the description and the rest, evaluated.
static void smith_manifest_finish_edge(SmithExec *exec, const SmithLexer *lexer, const SmithBlock *block) {
    SmithEdge *edge = &exec->graph.edges[block->edge];
    if (edge->phony) return;
    edge->command = smith_manifest_edge_variable(exec, block, "command");
    if (!smith_manifest_is_set(edge->command)) smith_manifest_fail(exec, lexer, "a rule needs a command", edge->rule);
    edge->description = smith_manifest_edge_variable(exec, block, "description");
    if (!smith_manifest_is_set(edge->description)) edge->description = nullptr;
    const char *deps = smith_manifest_edge_variable(exec, block, "deps");
    if (smith_manifest_is_set(deps) && strcmp(deps, "gcc") != 0)
        smith_manifest_fail(exec, lexer, "only deps = gcc is read", deps);
    edge->has_deps = smith_manifest_is_set(deps);
    edge->depfile = smith_manifest_edge_variable(exec, block, "depfile");
    if (!smith_manifest_is_set(edge->depfile)) edge->depfile = nullptr;
    if (edge->has_deps && edge->depfile == nullptr)
        smith_manifest_fail(exec, lexer, "deps needs a depfile", edge->rule);
    edge->restat = smith_manifest_is_set(smith_manifest_edge_variable(exec, block, "restat"));
    edge->generator = smith_manifest_is_set(smith_manifest_edge_variable(exec, block, "generator"));
    const char *pool = smith_manifest_edge_variable(exec, block, "pool");
    if (smith_manifest_is_set(pool)) edge->pool = smith_manifest_pool_number(exec, lexer, pool);
    edge->command_hash = smith_exec_hash(edge->command, strlen(edge->command));
}

// A `name = value` line inside the block that is open.
static void smith_manifest_binding_line(SmithExec *exec, SmithLexer *lexer, SmithBlock *block, char *text) {
    char *name = nullptr;
    char *value = nullptr;
    size_t name_length = 0;
    if (!smith_manifest_assignment(text, &name, &name_length, &value))
        smith_manifest_fail(exec, lexer, "expected a variable assignment", text);
    const char *key = smith_exec_copy(exec, name, name_length);
    switch (block->kind) {
    case SmithBlock_Rule:
        if (block->rule->count >= smith_exec_bindings_max)
            smith_manifest_fail(exec, lexer, "too many variables in a rule", nullptr);
        block->rule->bindings[block->rule->count++] = (SmithBinding){key, smith_exec_copy(exec, value, strlen(value))};
        break;
    case SmithBlock_Build: {
        if (block->count >= smith_exec_bindings_max)
            smith_manifest_fail(exec, lexer, "too many variables in a build statement", nullptr);
        size_t length = smith_manifest_eval(exec, value, strlen(value), &(SmithScope){}, exec->scratch);
        block->bindings[block->count++] = (SmithBinding){key, smith_exec_copy(exec, exec->scratch, length)};
        break;
    }
    case SmithBlock_Pool:
        if (strcmp(key, "depth") != 0) smith_manifest_fail(exec, lexer, "a pool has only a depth", key);
        block->pool->depth = (int)strtol(value, nullptr, 10);
        break;
    case SmithBlock_None:
        smith_manifest_fail(exec, lexer, "an indented line outside a statement", text);
    }
}

static void smith_manifest_default(SmithExec *exec, const char *p) {
    SmithGraph *graph = &exec->graph;
    uint32_t start = (uint32_t)graph->id_count;
    uint32_t count = smith_manifest_read_paths(exec, &p, "");
    for (uint32_t i = 0; i < count; i++) {
        if (graph->default_count >= smith_exec_defaults_max) smith_exec_fail("%s: too many defaults", exec->manifest);
        graph->defaults[graph->default_count++] = graph->ids[start + i];
    }
    graph->id_count = start; // the ids were only a way to read them
}

static void smith_manifest_variable_line(SmithExec *exec, SmithLexer *lexer, char *text) {
    SmithGraph *graph = &exec->graph;
    char *name = nullptr;
    char *value = nullptr;
    size_t name_length = 0;
    if (!smith_manifest_assignment(text, &name, &name_length, &value))
        smith_manifest_fail(exec, lexer, "unknown statement", text);
    if (graph->variable_count >= smith_exec_variables_max)
        smith_manifest_fail(exec, lexer, "too many variables", nullptr);
    size_t length = smith_manifest_eval(exec, value, strlen(value), &(SmithScope){}, exec->scratch);
    SmithBinding *variable = &graph->variables[graph->variable_count++];
    *variable = (SmithBinding){smith_exec_copy(exec, name, name_length), smith_exec_copy(exec, exec->scratch, length)};
    if (name_length == 8 && memcmp(name, "builddir", 8) == 0) graph->builddir = variable->value;
}

// One statement that starts at the left margin. The block before it is finished by the caller.
static void smith_manifest_statement(SmithExec *exec, SmithLexer *lexer, SmithBlock *block, char *text) {
    const char *end = smith_manifest_word_end(text, "");
    size_t length = (size_t)(end - text);
    *block = (SmithBlock){};
    if (length == 4 && memcmp(text, "rule", 4) == 0) {
        smith_manifest_start_rule(exec, lexer, block, end);
    } else if (length == 5 && memcmp(text, "build", 5) == 0) {
        smith_manifest_start_build(exec, lexer, block, end);
    } else if (length == 4 && memcmp(text, "pool", 4) == 0) {
        smith_manifest_start_pool(exec, lexer, block, end);
    } else if (length == 7 && memcmp(text, "default", 7) == 0) {
        smith_manifest_default(exec, end);
    } else if ((length == 7 && memcmp(text, "include", 7) == 0) || (length == 8 && memcmp(text, "subninja", 8) == 0)) {
        smith_manifest_fail(exec, lexer, "include and subninja are not read", nullptr);
    } else {
        smith_manifest_variable_line(exec, lexer, text);
    }
}

// Each edge's inputs are consumers' links on their nodes; each output is made by its edge.
static void smith_manifest_connect(SmithExec *exec) {
    SmithGraph *graph = &exec->graph;
    for (uint32_t number = 1; number <= graph->edge_count; number++) {
        SmithEdge *edge = &graph->edges[number];
        for (uint32_t i = 0; i < edge->output_count; i++) {
            SmithNode *node = &graph->nodes[graph->ids[edge->outputs + i]];
            if (node->edge != 0) smith_exec_fail("%s: multiple rules generate %s", exec->manifest, node->path);
            node->edge = number;
        }
        uint32_t inputs = edge->explicit_count + edge->implicit_count + edge->order_count;
        for (uint32_t i = 0; i < inputs; i++) {
            SmithNode *node = &graph->nodes[graph->ids[edge->inputs + i]];
            graph->links[graph->link_count] = (SmithLink){.edge = number, .next = node->consumer};
            node->consumer = (uint32_t)++graph->link_count;
        }
    }
}

static void smith_manifest_allocate(SmithExec *exec) {
    SmithGraph *graph = &exec->graph;
    *graph = (SmithGraph){};
    graph->nodes = smith_exec_carve(exec, smith_exec_nodes_max * sizeof(SmithNode));
    graph->edges = smith_exec_carve(exec, smith_exec_edges_max * sizeof(SmithEdge));
    graph->ids = smith_exec_carve(exec, smith_exec_ids_max * sizeof(uint32_t));
    graph->links = smith_exec_carve(exec, smith_exec_ids_max * sizeof(SmithLink));
    graph->defaults = smith_exec_carve(exec, smith_exec_defaults_max * sizeof(uint32_t));
    graph->pools = smith_exec_carve(exec, smith_exec_pools_max * sizeof(SmithPool));
    graph->rules = smith_exec_carve(exec, smith_exec_rules_max * sizeof(SmithRule));
    graph->variables = smith_exec_carve(exec, smith_exec_variables_max * sizeof(SmithBinding));
    graph->pools[graph->pool_count++] = (SmithPool){.name = "console", .depth = 1};
    graph->builddir = ".";
}

// Reads the manifest into exec->graph. Any problem is a message and the end of the build.
static void smith_manifest_read(SmithExec *exec) {
    SmithRead file = smith_files_read(&exec->io->files, exec->arena, exec->manifest);
    if (file.error.code != 0) smith_exec_fail("loading '%s': %s", exec->manifest, strerror(file.error.code));
    smith_manifest_allocate(exec);
    SmithLexer lexer = {.cursor = (char *)(uintptr_t)file.data, .end = (char *)(uintptr_t)file.data + file.size};
    SmithBlock block = {};
    size_t indent = 0;
    char *text = nullptr;
    for (size_t statements = 0; smith_manifest_next(&lexer, &indent, &text); statements++) {
        if (statements > file.size) smith_exec_fail("%s: the reader did not advance", exec->manifest);
        if (indent > 0) {
            smith_manifest_binding_line(exec, &lexer, &block, text);
            continue;
        }
        if (block.kind == SmithBlock_Build) smith_manifest_finish_edge(exec, &lexer, &block);
        smith_manifest_statement(exec, &lexer, &block, text);
    }
    if (block.kind == SmithBlock_Build) smith_manifest_finish_edge(exec, &lexer, &block);
    smith_manifest_connect(exec);
}
