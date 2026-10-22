// The translation as a whole: the options, the named arrays, the declarations in order, the default, the header and
// what was not translated. Included by smith.c after make_print.c.

static int smith_make_translate(const char *path, FILE *out);

// ---- Options

static void smith_make_emit_options(SmithMakeBuffer *out) {
    bool any = false;
    for (size_t i = 0; i < smith_make.var_count; i++) {
        const SmithMakeVar *var = &smith_make.vars[i];
        if (!var->option) continue;
        if (!any) smith_make_put_text(out, "    // Options: `make NAME=value` is `smith build -DNAME=value`.\n");
        any = true;
        const char *fallback = var->fallback != nullptr ? var->fallback : "";
        const char *plain = smith_make_unmark(fallback);
        const char *name = smith_make_literal(var->name, strlen(var->name));
        smith_make_put_format(out,
                              "    const char *option_%s = smith_option_value(b, %s, smith_env(b, %s, %s), "
                              "\"%s, as `make %s=...` (%s:%u)\");\n",
                              smith_make_c_name(var->name), name, name, smith_make_render(plain, strlen(plain)),
                              var->name, var->name, var->where.file != nullptr ? var->where.file : "", var->where.line);
    }
    if (any) smith_make_put_char(out, '\n');
}

// ---- Arrays

typedef struct {
    size_t var;
    const char *text;
    size_t deps_first; // the arrays it names, in the reference log
    size_t deps_count;
    bool emitted;
} SmithMakeArray;

// The definition of a variable's array: its value as a list, the uses of other variables in it as their arrays.
static const char *smith_make_array_definition(const SmithMakeVar *var) {
    const char *marked = var->simple ? var->marked : smith_make_expand_text(var->value, true);
    SmithMakeArgv argv;
    // read as the shell reads a command, so that the array holds the words the command had
    if (!smith_make_lex(marked, true, &argv) && !smith_make_lex(marked, false, &argv))
        return "(const char *[]){nullptr}";
    return smith_make_list_all(&argv);
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
            arrays[count].text = smith_make_array_definition(var);
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

static void smith_make_emit_array(SmithMakeBuffer *out, const SmithMakeArray *array) {
    const SmithMakeVar *var = &smith_make.vars[array->var];
    const char *value =
        smith_make_summary(var->source != nullptr ? var->source : var->value, smith_make_comment_width - 12);
    smith_make_put_format(out, "    // %s:%u  %s %s %s\n", var->where.file != nullptr ? var->where.file : "",
                          var->where.line, var->name, var->simple ? ":=" : "=", value);
    smith_make_put_format(out, "    const char *const *%s = %s;\n", smith_make_array_name(var), array->text);
}

// The arrays in the order of the Makefile, each after the arrays it names.
static void smith_make_emit_arrays(SmithMakeBuffer *out) {
    constexpr size_t capacity = 4096;
    SmithMakeArray *arrays = smith_make_alloc(capacity * sizeof(SmithMakeArray));
    size_t count = smith_make_collect_arrays(arrays, capacity);
    if (count == 0) return;
    smith_make_put_text(out, "    // Variables the commands below use as whole lists, named as in the Makefile.\n");
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
        smith_make_emit_array(out, next);
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

// `smith build --dump-config` in a folder with a Makefile and no build file prints the Makefile as a build.c: whether
// this was that. The folder's Makefile is the first of GNUmakefile, makefile and Makefile, as GNU make reads them.
static bool smith_make_dump_config(int count, char **args) {
    if (count != 1 || strcmp(args[0], "--dump-config") != 0) return false;
    // `smith build --dump-config > build.c` makes the file before it runs: an empty build file is no build file.
    static const char *const build_files[] = {"build.c", "build.smith"};
    for (size_t i = 0; i < sizeof build_files / sizeof build_files[0]; i++) {
        struct stat info;
        if (stat(build_files[i], &info) == 0 && info.st_size > 0) return false;
    }
    static const char *const names[] = {"GNUmakefile", "makefile", "Makefile"};
    DIR *folder = opendir(".");
    if (folder == nullptr) return false;
    const char *found = nullptr;
    for (size_t i = 0; i < sizeof names / sizeof names[0] && found == nullptr; i++) { // case matters, as in make
        rewinddir(folder);
        for (const struct dirent *entry = readdir(folder); entry != nullptr; entry = readdir(folder)) {
            if (strcmp(entry->d_name, names[i]) == 0) found = names[i];
        }
    }
    closedir(folder);
    if (found == nullptr) return false;
    smith_make_translate(found, stdout);
    return true;
}

// Prints the translation of the Makefile at path to out. Warnings about what is not translated go to stderr.
static int smith_make_translate(const char *path, FILE *out) {
    smith_make.file = path;
    smith_make_read(path);
    smith_make_graph_build();
    smith_make_import_all();
    smith_make_classify();
    SmithMakeBuffer body;
    smith_make_buffer_init(&body, smith_make_body_size);
    smith_make_output.body = &body;
    SmithMakeGraph *g = &smith_make_graph;
    size_t *order = smith_make_nodes_in_order();
    for (size_t pass = 0; pass < 2; pass++) { // the objects first, so that a program can name them
        for (size_t k = 0; k < g->node_count; k++) {
            size_t i = order[k];
            bool object = smith_make_infos[i].kind == SmithMakeKind_Object;
            if (object == (pass == 0)) smith_make_emit_node(&g->nodes[i], &smith_make_infos[i]);
        }
    }
    smith_make_emit_default(&body);
    SmithMakeBuffer head;
    smith_make_buffer_init(&head, smith_make_body_size);
    smith_make_emit_options(&head);
    smith_make_emit_arrays(&head);
    for (size_t i = 0; i < g->missing_count; i++)
        smith_make_report_at(smith_make.here, "note: no rule makes %s, and no such file is here", g->missing[i]);
    smith_make_emit_header(out, path);
    if (smith_make_output.words_used) fputs(smith_make_words_helper, out);
    fputs("void build(SmithBuild *b) {\n", out);
    fwrite(head.data, 1, head.length, out);
    fwrite(body.data + (body.length > 0 && body.data[0] == '\n' ? 1 : 0), 1,
           body.length - (body.length > 0 && body.data[0] == '\n' ? 1 : 0), out);
    fputs("}\n", out);
    for (size_t i = 0; i < smith_make.diag_count; i++) {
        const SmithMakeDiag *diag = &smith_make.diags[i];
        if (strncmp(diag->message, "note: ", 6) != 0)
            fprintf(stderr, "smith: %s:%u: not translated: %s\n", diag->where.file, diag->where.line, diag->message);
    }
    return 0;
}
