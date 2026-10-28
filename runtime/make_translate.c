// The translation as a whole: the options, the named arrays, the declarations in order, the default, the header and
// what was not translated. Included by smith.c after make_print.c.

static int smith_make_translate(const char *path, FILE *out, FILE *errors);

// ---- Options

static void smith_make_emit_options(SmithMakeBuffer *out) {
    bool any = false;
    for (size_t i = 0; i < smith_make.var_count; i++) {
        const SmithMakeVar *var = &smith_make.vars[i];
        if (!var->option) continue;
        const char *fallback = var->fallback != nullptr ? var->fallback : "";
        const char *plain = smith_make_unmark(fallback);
        const char *name = smith_make_literal(var->name, strlen(var->name));
        smith_make_unit_begin(smith_make_unit_list);
        if (!any) smith_make_put_text(out, "    // Options: `make NAME=value` is `smith build -DNAME=value`.\n");
        any = true;
        // an option nothing in the targets reads (a wildcard read at its default) is declared all the same
        bool read = strstr(smith_make_output.body->data, smith_make_ident(var->name)) != nullptr;
        smith_make_put_decl(out, "const char *", smith_make_ident(var->name), !read);
        smith_make_put_format(out,
                              "smith_option_value(b, %s, smith_env(b, %s, %s), \"%s, as `make %s=...` (%s:%u)\");\n",
                              name, name, smith_make_render(plain, strlen(plain)), var->name, var->name,
                              var->where.file != nullptr ? var->where.file : "", var->where.line);
        smith_make_unit_end();
    }
    if (any) smith_make_put_char(out, '\n');
}

// ---- Arrays

constexpr size_t smith_make_piece_words = 30; // a list of more words is written in pieces, a statement each

typedef struct {
    size_t var;
    const char *text;  // the list, or its first piece
    const char **more; // the other pieces, each joined to the array with smith_cat
    size_t more_count;
    size_t deps_first; // the arrays it names, in the reference log
    size_t deps_count;
    bool emitted;
} SmithMakeArray;

// The definition of a variable's array: its value as a list, the uses of other variables in it as their arrays.
static void smith_make_array_definition(const SmithMakeVar *var, SmithMakeArray *array) {
    const char *marked = var->simple ? var->marked : smith_make_expand_text(var->value, true);
    SmithMakeArgv argv;
    array->more = nullptr;
    array->more_count = 0;
    // read as the shell reads a command, so that the array holds the words the command had
    if (!smith_make_lex(marked, true, &argv) && !smith_make_lex(marked, false, &argv)) {
        array->text = "(const char *[]){nullptr}";
        return;
    }
    size_t *indices = smith_make_alloc((argv.count + 1) * sizeof(size_t));
    for (size_t i = 0; i < argv.count; i++)
        indices[i] = i;
    size_t pieces = (argv.count + smith_make_piece_words - 1) / smith_make_piece_words;
    if (pieces <= 1) {
        array->text = smith_make_list(&argv, indices, argv.count);
        return;
    }
    array->more = smith_make_new_list(pieces);
    for (size_t k = 0; k < pieces; k++) {
        size_t first = k * smith_make_piece_words;
        size_t count = argv.count - first < smith_make_piece_words ? argv.count - first : smith_make_piece_words;
        const char *piece = smith_make_list(&argv, indices + first, count);
        if (k == 0)
            array->text = piece;
        else
            array->more[array->more_count++] = piece;
    }
}

// Every array that is used, and the arrays those use, until there are no more.
static size_t smith_make_collect_arrays(SmithMakeArray *arrays, size_t capacity) {
    size_t count = 0;
    bool grew = true;
    for (size_t round = 0; grew && round < capacity + 1; round++) {
        grew = false;
        for (size_t v = 0; v < smith_make.var_count && count < capacity; v++) {
            SmithMakeVar *var = &smith_make.vars[v];
            if (!var->used_as_array) continue;
            bool known = false;
            for (size_t k = 0; k < count && !known; k++)
                known = arrays[k].var == v;
            if (known) continue;
            size_t before = smith_make_ref_log_count;
            arrays[count].var = v;
            smith_make_array_definition(var, &arrays[count]);
            arrays[count].deps_first = before;
            arrays[count].deps_count = smith_make_ref_log_count - before;
            arrays[count].emitted = false;
            count++;
            grew = true;
        }
    }
    return count;
}

static bool smith_make_array_ready(const SmithMakeArray *arrays, size_t count, const SmithMakeArray *array) {
    for (size_t d = 0; d < array->deps_count; d++) {
        size_t dep = smith_make_ref_log[array->deps_first + d];
        for (size_t k = 0; k < count; k++) {
            if (arrays[k].var == dep && !arrays[k].emitted) return false;
        }
    }
    return true;
}

static void smith_make_emit_array(SmithMakeBuffer *out, const SmithMakeArray *array, const char *intro) {
    const SmithMakeVar *var = &smith_make.vars[array->var];
    smith_make_unit_begin(smith_make_unit_list);
    smith_make_put_text(out, intro);
    const char *value =
        smith_make_summary(var->source != nullptr ? var->source : var->value, smith_make_comment_width - 12);
    smith_make_put_format(out, "    // %s:%u  %s %s %s\n", var->where.file != nullptr ? var->where.file : "",
                          var->where.line, var->name, var->simple ? ":=" : "=", value);
    smith_make_put_decl(out, "const char *const *", smith_make_array_name(var), false);
    smith_make_put_format(out, "%s;\n", array->text);
    smith_make_unit_end();
    for (size_t k = 0; k < array->more_count;
         k++) { // a piece is a statement, so that a long list can be several functions
        smith_make_unit_begin(smith_make_unit_list);
        smith_make_put_format(out, "    %s = smith_cat(b, %s, %s, nullptr);\n", smith_make_array_name(var),
                              smith_make_array_name(var), array->more[k]);
        smith_make_unit_end();
    }
}

// The arrays in the order of the Makefile, each after the arrays it names.
static void smith_make_emit_arrays(SmithMakeBuffer *out) {
    constexpr size_t capacity = 4096;
    SmithMakeArray *arrays = smith_make_alloc(capacity * sizeof(SmithMakeArray));
    size_t count = smith_make_collect_arrays(arrays, capacity);
    if (count == 0) return;
    for (size_t done = 0; done < count; done++) {
        SmithMakeArray *next = nullptr;
        for (size_t k = 0; k < count; k++) {
            SmithMakeArray *array = &arrays[k];
            if (array->emitted || !smith_make_array_ready(arrays, count, array)) continue;
            if (next == nullptr || smith_make.vars[array->var].order < smith_make.vars[next->var].order) next = array;
        }
        if (next == nullptr) { // a cycle: the Makefile's variables name each other
            for (size_t k = 0; k < count && next == nullptr; k++) {
                if (!arrays[k].emitted) next = &arrays[k];
            }
        }
        smith_make_emit_array(out, next,
                              done == 0 ? "    // Variables the commands below use as whole lists, named as in the "
                                          "Makefile.\n"
                                        : "");
        next->emitted = true;
    }
    smith_make_put_char(out, '\n');
}

// ---- The default

static const SmithMakeNode *smith_make_alias_named(const char *name) {
    const SmithMakeGraph *g = &smith_make_graph;
    for (size_t i = 0; i < g->node_count; i++) {
        const SmithMakeInfo *info = &smith_make_infos[i];
        if (info->kind == SmithMakeKind_Alias && strcmp(info->emit_name, name) == 0) return &g->nodes[i];
    }
    return nullptr;
}

// The default goal as what build.c builds: an alias is replaced by what it groups.
static void smith_make_emit_default(SmithMakeBuffer *out) {
    const char *goal = smith_make_var_value(".DEFAULT_GOAL");
    if (goal == nullptr || goal[0] == '\0') goal = smith_make_reader.default_goal;
    if (goal == nullptr) return;
    size_t count = 1;
    const char **names = smith_make_inputs(&goal, 1, &count);
    for (size_t round = 0; round < 8; round++) { // aliases of aliases
        bool changed = false;
        const char **flat = smith_make_new_list((smith_make_inputs_max + 1));
        size_t flat_count = 0;
        for (size_t i = 0; i < count; i++) {
            const SmithMakeNode *alias = smith_make_alias_named(names[i]);
            if (alias == nullptr) {
                flat[flat_count++] = names[i];
                continue;
            }
            const SmithMakeInfo *info = smith_make_info_of(alias);
            size_t more_count = 0;
            const char **more = smith_make_inputs(info->prerequisites, info->prerequisite_count, &more_count);
            for (size_t k = 0; k < more_count && flat_count < smith_make_inputs_max; k++)
                flat[flat_count++] = more[k];
            changed = true;
        }
        names = flat;
        count = flat_count;
        if (!changed) break;
    }
    if (count == 0) return;
    smith_make_put_format(out, "\n    // The default goal: %s.\n    smith_default(b, %s);\n", goal,
                          smith_make_names(names, count));
}

// ---- The whole file

// Whether a target's name is the name, once a hole in it is taken as empty (`app$(EXT)` and `app`).
static bool smith_make_named_with_holes(const char *name) {
    const SmithMakeGraph *g = &smith_make_graph;
    for (size_t i = 0; i < g->node_count; i++) {
        const char *node = g->nodes[i].name;
        if (!smith_make_has_hole(node, strlen(node))) continue;
        SmithMakeBuffer plain;
        smith_make_buffer_init(&plain, strlen(node) + 1);
        for (const char *c = node; *c != '\0'; c++) {
            if (*c == smith_make_hole_begin) {
                while (*c != '\0' && *c != smith_make_hole_end)
                    c++;
                if (*c == '\0') break;
            } else {
                smith_make_put_char(&plain, *c);
            }
        }
        plain.data[plain.length] = '\0';
        if (strcmp(plain.data, name) == 0) return true;
    }
    return false;
}

// The nodes in the order of the Makefile: by file, then line.
static size_t *smith_make_nodes_in_order(void) {
    const SmithMakeGraph *g = &smith_make_graph;
    size_t *order = smith_make_alloc((g->node_count + 1) * sizeof(size_t));
    for (size_t i = 0; i < g->node_count; i++) { // an insertion sort: stable, and a Makefile has some thousand targets
        size_t at = i;
        while (at > 0) {
            const SmithMakeWhere *before = &g->nodes[order[at - 1]].where;
            const SmithMakeWhere *now = &g->nodes[i].where;
            bool later = (uintptr_t)before->file > (uintptr_t)now->file ||
                         (before->file == now->file && before->line > now->line);
            if (!later) break;
            order[at] = order[at - 1];
            at--;
        }
        order[at] = i;
    }
    return order;
}

static const char *smith_make_words_helper =
    "// Splits an option or an environment variable into words as the shell splits a make variable: at blanks, with\n"
    "// quotes and backslashes removed.\n"
    "static const char *const *make_words(SmithBuild *b, const char *text) {\n"
    "    const char *const *words = smith_list(b, nullptr);\n"
    "    while (*text != '\\0') {\n"
    "        while (*text == ' ' || *text == '\\t' || *text == '\\n') text++;\n"
    "        if (*text == '\\0') break;\n"
    "        const char *word = \"\";\n"
    "        char quote = '\\0';\n"
    "        while (*text != '\\0' && (quote != '\\0' || (*text != ' ' && *text != '\\t' && *text != '\\n'))) {\n"
    "            if (quote == '\\0' && (*text == '\\'' || *text == '\"')) {\n"
    "                quote = *text++;\n"
    "            } else if (quote != '\\0' && *text == quote) {\n"
    "                quote = '\\0';\n"
    "                text++;\n"
    "            } else {\n"
    "                if (*text == '\\\\' && text[1] != '\\0' && quote != '\\'') text++;\n"
    "                word = smith_fmt(b, \"%s%c\", word, *text++);\n"
    "            }\n"
    "        }\n"
    "        words = smith_cat(b, words, smith_list(b, word, nullptr), nullptr);\n"
    "    }\n"
    "    return words;\n"
    "}\n\n";

static void smith_make_emit_report(FILE *out) {
    size_t notes = 0;
    size_t warnings = 0;
    for (size_t i = 0; i < smith_make.diag_count; i++) {
        if (strncmp(smith_make.diags[i].message, "note: ", 6) == 0)
            notes++;
        else
            warnings++;
    }
    if (warnings > 0) {
        fprintf(
            out,
            "//\n// NOT TRANSLATED (%zu): what follows is left out or approximated, and the Makefile still has it.\n",
            warnings);
        for (size_t i = 0; i < smith_make.diag_count; i++) {
            const SmithMakeDiag *diag = &smith_make.diags[i];
            if (strncmp(diag->message, "note: ", 6) != 0)
                fprintf(out, "//   %s:%u: %s\n", diag->where.file, diag->where.line, diag->message);
        }
    }
    if (notes > 0) {
        fprintf(out, "//\n// Notes:\n");
        for (size_t i = 0; i < smith_make.diag_count; i++) {
            const SmithMakeDiag *diag = &smith_make.diags[i];
            if (strncmp(diag->message, "note: ", 6) == 0)
                fprintf(out, "//   %s:%u: %s\n", diag->where.file, diag->where.line, diag->message + 6);
        }
    }
}

static void smith_make_emit_header(FILE *out, const char *path) {
    fprintf(
        out,
        "// build.c, translated from %s by `smith build --dump-config` (the Makefile is read as GNU make reads it,\n"
        "// with the files that are here now: a $(wildcard) is the files it matched). Edit it freely.\n",
        path);
    size_t listed = 0;
    size_t options = 0;
    for (size_t i = 0; i < smith_make.var_count; i++) {
        const SmithMakeVar *var = &smith_make.vars[i];
        if (!var->option) continue;
        options++;
        if (listed >= 16) continue;
        if (listed == 0)
            fprintf(out, "//\n// Options the Makefile reads from the command line (`make NAME=value` is `smith build "
                         "-DNAME=value`):\n");
        listed++;
        fprintf(out, "//   -D%s=...  (Makefile:%u, default %s)\n", var->name, var->where.line,
                smith_make_summary(var->source != nullptr && var->source[0] != '\0' ? var->source : "empty", 70));
    }
    if (options > listed) fprintf(out, "//   and %zu more, declared at the top of build()\n", options - listed);
    smith_make_emit_report(out);
    fprintf(out, "#include <smith/build.h>\n#include <string.h>\n\n");
}

// ---- Formatting: build.c is written the way `smith fmt` writes it

// Runs `smith fmt` (smith's .clang-format, or the project's) on a file. False where it cannot run: the stage0 without a
// clang-format, then the text stays as it is written.
static bool smith_make_format_files(FILE *in, FILE *out) {
    char self[PATH_MAX];
    if (!smith_self_path(self)) return false;
    fflush(in);
    SMITH_ASSERT(fseek(in, 0, SEEK_SET) == 0);
    char *args[] = {self, (char *)"fmt", (char *)"--assume-filename=build.c", nullptr};
    posix_spawn_file_actions_t actions;
    SMITH_ASSERT(posix_spawn_file_actions_init(&actions) == 0);
    SMITH_ASSERT(posix_spawn_file_actions_adddup2(&actions, fileno(in), 0) == 0);
    SMITH_ASSERT(posix_spawn_file_actions_adddup2(&actions, fileno(out), 1) == 0);
    SMITH_ASSERT(posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0);
    pid_t pid = 0;
    bool done = false;
    if (posix_spawn(&pid, self, &actions, nullptr, args, environ) == 0) {
        int status = 0;
        int waited = -1;
        for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
            waited = waitpid(pid, &status, 0);
        done = waited >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    posix_spawn_file_actions_destroy(&actions);
    return done;
}

// The text, formatted, in out; false where the formatter cannot run.
static bool smith_make_format_text(const char *text, size_t length, SmithMakeBuffer *out) {
    FILE *in = tmpfile();
    FILE *formatted = tmpfile();
    bool done = in != nullptr && formatted != nullptr && fwrite(text, 1, length, in) == length &&
                smith_make_format_files(in, formatted);
    if (done) {
        SMITH_ASSERT(fseek(formatted, 0, SEEK_SET) == 0);
        char chunk[65536];
        for (size_t n = fread(chunk, 1, sizeof chunk, formatted); n > 0; n = fread(chunk, 1, sizeof chunk, formatted))
            smith_make_put(out, chunk, n);
    }
    if (in != nullptr) fclose(in);
    if (formatted != nullptr) fclose(formatted);
    return done;
}

// ---- Functions: one when build.c is small, else one for each run of units

// A function may hold 68 lines between its braces (readability-function-size in smith's .clang-tidy, LineThreshold 69).
constexpr size_t smith_make_function_lines =
    62; // with room for what the formatter does to the lines of a variable moved

// The lines a unit takes once formatted, estimated: a long line is wrapped at about a hundred characters.
static size_t smith_make_unit_estimate(const SmithMakeBuffer *buffer, const SmithMakeUnit *unit) {
    size_t lines = 0;
    size_t length = 0;
    for (size_t i = unit->start; i < unit->end; i++) {
        if (buffer->data[i] != '\n') {
            length++;
            continue;
        }
        lines += 1 + (length > 100 ? (length - 1) / 100 : 0);
        length = 0;
    }
    return lines + (length > 0 ? 1 + (length > 100 ? (length - 1) / 100 : 0) : 0);
}

typedef struct {
    const SmithMakeUnits *lists[2]; // the options and arrays, then what the targets make
    size_t *lines[2];               // the lines each unit takes, a blank line before it included
} SmithMakeSizes;

// How many lines each unit takes: counted in the formatter's output, with every unit as a function of its own; where
// the formatter cannot run, estimated.
static void smith_make_measure(SmithMakeSizes *sizes) {
    SmithMakeBuffer text;
    smith_make_buffer_init(&text, smith_make_body_size);
    size_t number = 0;
    for (size_t l = 0; l < 2; l++) {
        const SmithMakeUnits *list = sizes->lists[l];
        sizes->lines[l] = smith_make_alloc((list->count + 1) * sizeof(size_t));
        for (size_t k = 0; k < list->count; k++, number++) {
            smith_make_put_format(&text, "static void unit_%zu(SmithBuild *b) {\n", number);
            smith_make_put(&text, list->buffer->data + list->units[k].start, list->units[k].end - list->units[k].start);
            smith_make_put_text(&text, "}\n\n");
        }
    }
    SmithMakeBuffer formatted;
    smith_make_buffer_init(&formatted, smith_make_body_size);
    bool measured = smith_make_format_text(text.data, text.length, &formatted);
    number = 0;
    const char *from = formatted.data;
    for (size_t l = 0; l < 2; l++) {
        const SmithMakeUnits *list = sizes->lists[l];
        for (size_t k = 0; k < list->count; k++, number++) {
            const SmithMakeUnit *unit = &list->units[k];
            size_t lines = smith_make_unit_estimate(list->buffer, unit);
            const char *open =
                measured ? strstr(from, smith_make_format("static void unit_%zu(SmithBuild *b) {\n", number)) : nullptr;
            const char *close = open != nullptr ? strstr(open, "\n}\n") : nullptr;
            if (close != nullptr) {
                lines = 0;
                for (const char *c = strchr(open, '\n') + 1; c <= close; c++)
                    lines += *c == '\n';
                from = close;
            }
            sizes->lines[l][k] = lines + 1;
        }
    }
}

// A unit's text in a function of its own: the variable it declares is the file's, so its declaration is an assignment.
static void smith_make_put_unit(FILE *out, const SmithMakeUnits *list, const SmithMakeUnit *unit, bool moved) {
    const char *data = list->buffer->data;
    if (!moved || unit->type == nullptr) {
        fwrite(data + unit->start, 1, unit->end - unit->start, out);
        return;
    }
    fwrite(data + unit->start, 1, unit->decl_at - unit->start, out);
    bool object = strcmp(unit->type, "SmithObject") == 0;
    bool array = strcmp(unit->type, "const char *const *") == 0;
    fprintf(out, "    %s = %s", unit->name, object ? "(SmithObject)" : array ? "smith_cat(b, " : "");
    size_t after = unit->decl_at + unit->decl_length;
    size_t end = unit->end;
    if (array) end -= 2; // the `;` and the newline: a list in a literal ends with its block, smith_cat's copy does not
    fwrite(data + after, 1, end - after, out);
    if (array) fputs(", nullptr);\n", out);
}

// Whether the units use the parameter b.
static bool smith_make_units_use_b(const SmithMakeUnits *list, size_t first, size_t last) {
    for (size_t k = first; k < last; k++) {
        const SmithMakeUnit *unit = &list->units[k];
        if (unit->type != nullptr && strcmp(unit->type, "const char *const *") == 0) return true;
        for (size_t i = unit->start; i + 3 < unit->end; i++) {
            const char *at = list->buffer->data + i;
            if (strncmp(at, "(b,", 3) == 0 || strncmp(at, "(b)", 3) == 0 || strncmp(at, "b->", 3) == 0) return true;
        }
    }
    return false;
}

// The variables the units declare, at the top of the file.
static void smith_make_put_statics(FILE *out, const SmithMakeUnits *list) {
    bool any = false;
    for (size_t i = 0; i < list->count; i++) {
        const SmithMakeUnit *unit = &list->units[i];
        if (unit->type == nullptr) continue;
        any = true;
        fprintf(out, "%sstatic %s%s%s;\n", unit->maybe_unused ? "[[maybe_unused]] " : "", unit->type,
                unit->type[strlen(unit->type) - 1] == '*' ? "" : " ", unit->name);
    }
    if (any) fputc('\n', out);
}

// The units of a list, in functions of at most smith_make_function_lines lines; part counts the functions so far.
static size_t smith_make_put_parts(FILE *out, const SmithMakeUnits *list, const size_t *lines, size_t part) {
    size_t i = 0;
    while (i < list->count) {
        size_t used = 0;
        size_t first = i;
        while (i < list->count && (i == first || used + lines[i] <= smith_make_function_lines)) {
            used += lines[i];
            i++;
        }
        fprintf(out, "static void part_%zu(SmithBuild *b) {\n", ++part);
        if (!smith_make_units_use_b(list, first, i)) fputs("    (void)b;\n", out);
        for (size_t k = first; k < i; k++)
            smith_make_put_unit(out, list, &list->units[k], true);
        fputs("}\n\n", out);
    }
    return part;
}

// The functions of build.c: build() alone while it fits, else the variables as statics, a function for each run of
// units and a build() that calls them in order and sets the default.
static void smith_make_put_functions(FILE *out, const SmithMakeUnits *head, const SmithMakeUnits *body,
                                     const SmithMakeBuffer *tail) {
    SmithMakeSizes sizes = {{head, body}, {nullptr, nullptr}};
    smith_make_measure(&sizes);
    size_t lines = 4; // the blank lines the head keeps between its sections
    for (size_t l = 0; l < 2; l++) {
        for (size_t k = 0; k < sizes.lists[l]->count; k++)
            lines += sizes.lines[l][k];
    }
    for (size_t i = 0; i < tail->length; i++)
        lines += tail->data[i] == '\n';
    if (lines <= smith_make_function_lines) {
        fputs("void build(SmithBuild *b) {\n", out);
        fwrite(head->buffer->data, 1, head->buffer->length, out);
        size_t skip = body->buffer->length > 0 && body->buffer->data[0] == '\n' ? 1 : 0;
        fwrite(body->buffer->data + skip, 1, body->buffer->length - skip, out);
        fwrite(tail->data, 1, tail->length, out);
        fputs("}\n", out);
        return;
    }
    smith_make_put_statics(out, head);
    smith_make_put_statics(out, body);
    size_t parts = smith_make_put_parts(out, head, sizes.lines[0], 0);
    parts = smith_make_put_parts(out, body, sizes.lines[1], parts);
    constexpr size_t calls_max = 60; // more functions than build() holds calls are called from groups
    size_t groups = parts > calls_max ? (parts + calls_max - 1) / calls_max : 0;
    for (size_t g = 0; g < groups; g++) {
        fprintf(out, "static void group_%zu(SmithBuild *b) {\n", g + 1);
        for (size_t i = g * calls_max + 1; i <= parts && i <= (g + 1) * calls_max; i++)
            fprintf(out, "    part_%zu(b);\n", i);
        fputs("}\n\n", out);
    }
    fputs("void build(SmithBuild *b) {\n", out);
    for (size_t i = 1; groups == 0 && i <= parts; i++)
        fprintf(out, "    part_%zu(b);\n", i);
    for (size_t g = 1; g <= groups; g++)
        fprintf(out, "    group_%zu(b);\n", g);
    fwrite(tail->data, 1, tail->length, out);
    fputs("}\n", out);
}

// Forgets what the last translation left: the pool of memory and every table, so that a translation starts as the first
// of the process does. The unit tests translate many Makefiles in one process (include/smith_core.h).
static void smith_make_reset(void) {
    if (smith_make.pool != nullptr) munmap(smith_make.pool, smith_make_memory_size);
    memset(&smith_make, 0, sizeof smith_make);
    memset(&smith_make_reader, 0, sizeof smith_make_reader);
    memset(&smith_make_graph, 0, sizeof smith_make_graph);
    memset(&smith_make_machine, 0, sizeof smith_make_machine);
    memset(&smith_make_auto, 0, sizeof smith_make_auto);
    memset(&smith_make_output, 0, sizeof smith_make_output);
    memset(&smith_make_idents, 0, sizeof smith_make_idents);
    memset(&smith_make_saved_make, 0, sizeof smith_make_saved_make);
    memset(&smith_make_saved_reader, 0, sizeof smith_make_saved_reader);
    memset(&smith_make_saved_graph, 0, sizeof smith_make_saved_graph);
    memset(&smith_make_saved_auto, 0, sizeof smith_make_saved_auto);
    memset(smith_make_subs, 0, sizeof smith_make_subs);
    memset(smith_make_ref_log, 0, sizeof smith_make_ref_log);
    memset((void *)smith_make_reported, 0, sizeof smith_make_reported);
    memset(&smith_make_joined, 0, sizeof smith_make_joined);
    memset(&smith_make_define_body, 0, sizeof smith_make_define_body);
    smith_make_infos = nullptr;
    smith_make_object_count = 0;
    smith_make_program_count = 0;
    smith_make_step_count = 0;
    smith_make_ref_log_count = 0;
    smith_make_reported_count = 0;
    smith_make_sub_count = 0;
    smith_make_unit_list = nullptr;
}

// Prints the translation of the Makefile at path to out. Warnings about what is not translated go to errors.
static int smith_make_translate(const char *path, FILE *out, FILE *errors) {
    smith_make_reset();
    smith_make.file = path;
    smith_make_read(path);
    smith_make_graph_build();
    smith_make_import_all();
    smith_make_classify();
    SmithMakeBuffer body;
    smith_make_buffer_init(&body, smith_make_body_size);
    smith_make_output.body = &body;
    SmithMakeUnits body_units;
    smith_make_units_init(&body_units, &body);
    smith_make_unit_list = &body_units;
    SmithMakeGraph *g = &smith_make_graph;
    size_t *order = smith_make_nodes_in_order();
    for (size_t pass = 0; pass < 2; pass++) { // the objects first, so that a program can name them
        for (size_t k = 0; k < g->node_count; k++) {
            size_t i = order[k];
            bool object = smith_make_infos[i].kind == SmithMakeKind_Object;
            if (object == (pass == 0)) smith_make_emit_node(&g->nodes[i], &smith_make_infos[i]);
        }
    }
    body.data[body.length] = '\0';
    SmithMakeBuffer tail;
    smith_make_buffer_init(&tail, 65536);
    smith_make_emit_default(&tail);
    SmithMakeBuffer head;
    smith_make_buffer_init(&head, smith_make_body_size);
    SmithMakeUnits head_units;
    smith_make_units_init(&head_units, &head);
    smith_make_unit_list = &head_units;
    smith_make_emit_options(&head);
    smith_make_emit_arrays(&head);
    for (size_t i = 0; i < g->missing_count; i++) {
        if (!smith_make_named_with_holes(g->missing[i]))
            smith_make_report_at(smith_make.here, "note: no rule makes %s, and no such file is here", g->missing[i]);
    }
    smith_make_emit_header(out, path);
    if (smith_make_output.words_used) fputs(smith_make_words_helper, out);
    smith_make_put_functions(out, &head_units, &body_units, &tail);
    for (size_t i = 0; i < smith_make.diag_count; i++) {
        const SmithMakeDiag *diag = &smith_make.diags[i];
        if (strncmp(diag->message, "note: ", 6) != 0)
            fprintf(errors, "smith: %s:%u: not translated: %s\n", diag->where.file, diag->where.line, diag->message);
    }
    return 0;
}

#ifdef SMITH_CORE
// include/smith_core.h: the translation in process, for the units runner.
bool smith_core_make_translate(const char *folder, const char *makefile, char *build, size_t build_size, char *errors,
                               size_t errors_size) {
    int home = open(".", O_RDONLY);
    FILE *out = fmemopen(build, build_size, "w");
    FILE *err = fmemopen(errors, errors_size, "w");
    bool ok = home >= 0 && out != nullptr && err != nullptr && chdir(folder) == 0;
    if (ok) {
        const char *file = makefile != nullptr ? makefile : smith_make_find_makefile();
        if (file != nullptr) smith_make_translate(file, out, err);
        ok = file != nullptr;
        ok = fchdir(home) == 0 && ok;
    }
    if (out != nullptr) fclose(out);
    if (err != nullptr) fclose(err);
    if (home >= 0) close(home);
    return ok;
}
#endif
