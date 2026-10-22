// Printing the declarations of build.c. Included by smith.c after make_class.c.

#define SMITH_MAKE_BODY (smith_make_output.body)

constexpr size_t smith_make_comment_width = 112;

// A one-line summary of text for a comment: newlines and runs of blanks become one space, long text is cut.
static const char *smith_make_summary(const char *text, size_t width) {
    const char *clean = smith_make_unmark(text);
    size_t length = strlen(clean);
    char *out = smith_make_alloc(length + 8);
    size_t used = 0;
    bool blank = false;
    for (size_t i = 0; i < length && used < width; i++) {
        unsigned char c = (unsigned char)clean[i];
        if (c == smith_make_hole_begin) {
            size_t number = 0;
            i = smith_make_hole_at(clean, length, i, &number) - 1;
            out[used++] = '?';
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!blank && used > 0) out[used++] = ' ';
            blank = true;
        } else {
            out[used++] = (char)c;
            blank = false;
        }
    }
    if (used >= width) {
        out[used - 3] = '.';
        out[used - 2] = '.';
        out[used - 1] = '.';
    }
    out[used] = '\0';
    return out;
}

static const char *smith_make_rule_text(const SmithMakeNode *node) {
    SmithMakeBuffer text;
    smith_make_buffer_init(&text, 65536);
    smith_make_put_format(&text, "%s:", node->name);
    for (size_t i = 0; i < node->prerequisite_count; i++)
        smith_make_put_format(&text, " %s", node->prerequisites[i]);
    if (node->order_count > 0) {
        smith_make_put_text(&text, " |");
        for (size_t i = 0; i < node->order_count; i++)
            smith_make_put_format(&text, " %s", node->order_only[i]);
    }
    return smith_make_summary(smith_make_take(&text, 0), smith_make_comment_width);
}

static void smith_make_comment(const SmithMakeNode *node, const char *note) {
    SmithMakeBuffer *out = SMITH_MAKE_BODY;
    if (node->where.line == 0)
        smith_make_put_format(out, "\n    // %s  %s\n", node->where.file, smith_make_rule_text(node));
    else
        smith_make_put_format(out, "\n    // %s:%u  %s\n", node->where.file, node->where.line,
                              smith_make_rule_text(node));
    if (note != nullptr) smith_make_put_format(out, "    // %s\n", note);
}

// A field of a struct whose value is a list: `.name = LIST,`, left out for an empty list.
static void smith_make_field_list(const char *field, const char *const *names, size_t count) {
    if (count == 0) return;
    smith_make_put_format(SMITH_MAKE_BODY, "        .%s = %s,\n", field, smith_make_names(names, count));
}

// A command's text as the script runs it: its compiler is smith-cc or smith-c++, whatever the Makefile called it.
static const char *smith_make_command_text(const SmithMakeCommand *command) {
    const char *text = smith_make_unmark(smith_make_trim(command->text));
    size_t first = 0;
    while (text[first] != '\0' && !smith_make_space(text[first]))
        first++;
    const char *compiler = smith_make_compiler_name(smith_make_dup(text, first));
    return compiler != nullptr ? smith_make_format("%s%s", compiler, text + first) : text;
}

static const char *smith_make_script(const SmithMakeNode *node, const SmithMakeInfo *info) {
    SmithMakeBuffer script;
    smith_make_buffer_init(&script, 262144);
    if (info->kept_count > 1) smith_make_put_text(&script, "set -e\n");
    for (size_t k = 0; k < info->kept_count; k++) {
        const SmithMakeCommand *command = &node->commands[info->kept[k]];
        if (k > 0) smith_make_put_char(&script, '\n');
        const char *text = smith_make_command_text(command);
        if (node->cwd != nullptr) text = smith_make_format("(cd '%s' && %s)", node->cwd, text);
        smith_make_put_text(&script, text);
        if (command->ignore) smith_make_put_text(&script, " || true");
    }
    return smith_make_take(&script, 0);
}

// The command of a step or task: the words of its one command, or `sh -c` of the commands.
static const char *smith_make_command_list(const SmithMakeNode *node, const SmithMakeInfo *info) {
    SmithMakeArgv argv;
    if (info->kept_count == 1 && !node->commands[info->kept[0]].ignore && node->cwd == nullptr &&
        smith_make_lex(node->commands[info->kept[0]].text, true, &argv) && argv.count > 0) {
        const char *compiler = smith_make_compiler_name(argv.args[0].text);
        if (compiler != nullptr) argv.args[0] = (SmithMakeArg){compiler, -1};
        return smith_make_list_all(&argv);
    }
    const char *script = smith_make_script(node, info);
    return smith_make_format("(const char *[]){\"sh\", \"-c\", %s, nullptr}",
                             smith_make_render(script, strlen(script)));
}

static void smith_make_emit_inputs(const SmithMakeNode *node, const SmithMakeInfo *info) {
    size_t count = 0;
    const char **inputs = smith_make_inputs(info->prerequisites, info->prerequisite_count, &count);
    smith_make_field_list("inputs", inputs, count);
    size_t order_count = 0;
    const char **order = smith_make_inputs(node->order_only, node->order_count, &order_count);
    smith_make_field_list("order_only_inputs", order, order_count);
}

// ---- One declaration for each kind

static void smith_make_emit_step(const SmithMakeNode *node, const SmithMakeInfo *info) {
    SmithMakeBuffer *out = SMITH_MAKE_BODY;
    smith_make_comment(node, info->note);
    smith_make_put_format(out, "    SmithStep %s = {\n", info->c_name);
    smith_make_put_format(out, "        .command = %s,\n", smith_make_command_list(node, info));
    smith_make_emit_inputs(node, info);
    if (info->kind == SmithMakeKind_Step) {
        const char *outputs[] = {node->name};
        smith_make_field_list("outputs", outputs, 1);
    }
    if (info->always) smith_make_put_text(out, "        .always = true,\n");
    if (info->test) smith_make_put_text(out, "        .test = true,\n");
    smith_make_put_text(out, "    };\n");
    const char *call =
        info->kind == SmithMakeKind_Task
            ? smith_make_format("smith_task(b, %s, &%s);", smith_make_literal(info->emit_name, strlen(info->emit_name)),
                                info->c_name)
            : smith_make_format("smith_step(b, &%s);", info->c_name);
    if (node->guard != nullptr)
        smith_make_put_format(out, "    if (%s) {\n        %s\n    }\n", node->guard, call);
    else
        smith_make_put_format(out, "    %s\n", call);
}

static void smith_make_emit_alias(const SmithMakeNode *node, const SmithMakeInfo *info) {
    SmithMakeBuffer *out = SMITH_MAKE_BODY;
    size_t count = 0;
    const char **targets = smith_make_inputs(info->prerequisites, info->prerequisite_count, &count);
    smith_make_comment(node, info->note);
    if (count == 0) return; // a target that groups nothing
    smith_make_put_format(out, "    smith_alias(b, %s, %s);\n",
                          smith_make_literal(info->emit_name, strlen(info->emit_name)),
                          smith_make_names(targets, count));
}

// The implicit inputs of a compile: its prerequisites and order-only prerequisites other than the source.
static void smith_make_emit_object(const SmithMakeNode *node, const SmithMakeInfo *info) {
    SmithMakeBuffer *out = SMITH_MAKE_BODY;
    SmithMakeArgv argv;
    SmithMakeCc cc;
    bool ok = smith_make_lex(node->commands[info->kept[0]].text, true, &argv) && smith_make_parse_cc(&argv, &cc);
    SMITH_ASSERT(ok);
    size_t *flags = cc.others;
    const char *source = argv.args[cc.source].text;
    size_t count = 0;
    const char **inputs = smith_make_inputs(info->prerequisites, info->prerequisite_count, &count);
    size_t order_count = 0;
    const char **order = smith_make_inputs(node->order_only, node->order_count, &order_count);
    const char **implicit = smith_make_new_list((count + order_count + 1));
    size_t implicit_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(inputs[i], source) != 0) implicit[implicit_count++] = inputs[i];
    }
    for (size_t i = 0; i < order_count; i++)
        implicit[implicit_count++] = order[i];
    const char *note = info->note;
    if (cc.dropped_dependency_flags) note = "(-MMD and -MP are dropped: smith_object writes the depfile itself)";
    if (order_count > 0) note = "(its order-only prerequisites are implicit inputs: smith_object has no other kind)";
    smith_make_comment(node, note);
    smith_make_put_format(out, "    SmithObject %s = {\n", info->c_name);
    smith_make_put_format(out, "        .compiler = \"%s\",\n", cc.compiler);
    smith_make_put_format(out, "        .source = %s,\n", smith_make_render(source, strlen(source)));
    smith_make_put_format(out, "        .output = %s,\n", smith_make_render(node->name, strlen(node->name)));
    if (cc.other_count > 0)
        smith_make_put_format(out, "        .flags = %s,\n", smith_make_list(&argv, flags, cc.other_count));
    smith_make_field_list("implicit_inputs", implicit, implicit_count);
    smith_make_put_format(out, "    };\n    smith_object(b, &%s);\n", info->c_name);
}

static void smith_make_emit_program(const SmithMakeNode *node, const SmithMakeInfo *info) {
    SmithMakeBuffer *out = SMITH_MAKE_BODY;
    SmithMakeArgv argv;
    SmithMakeCc cc;
    bool ok = smith_make_lex(node->commands[info->kept[0]].text, true, &argv) && smith_make_parse_cc(&argv, &cc);
    SMITH_ASSERT(ok);
    size_t *before = smith_make_alloc((cc.other_count + 1) * sizeof(size_t));
    size_t *after = smith_make_alloc((cc.other_count + 1) * sizeof(size_t));
    size_t before_count = 0;
    size_t after_count = 0;
    for (size_t i = 0; i < cc.other_count; i++) {
        if (cc.others[i] < cc.objects[0])
            before[before_count++] = cc.others[i];
        else
            after[after_count++] = cc.others[i];
    }
    smith_make_comment(node, info->note);
    smith_make_put_format(out, "    SmithProgram %s = {\n", info->c_name);
    smith_make_put_format(out, "        .linker = \"%s\",\n", cc.compiler);
    smith_make_put_format(out, "        .output = %s,\n", smith_make_render(node->name, strlen(node->name)));
    if (before_count > 0)
        smith_make_put_format(out, "        .flags = %s,\n", smith_make_list(&argv, before, before_count));
    smith_make_put_text(out, "        .objects = (const SmithObject *const[]){");
    for (size_t i = 0; i < cc.object_count; i++) {
        const SmithMakeNode *object = smith_make_node_find(argv.args[cc.objects[i]].text);
        smith_make_put_format(out, "&%s, ", smith_make_info_of(object)->c_name);
    }
    smith_make_put_text(out, "nullptr},\n");
    if (after_count > 0)
        smith_make_put_format(out, "        .libraries = %s,\n", smith_make_list(&argv, after, after_count));
    smith_make_put_format(out, "    };\n    smith_program(b, &%s);\n", info->c_name);
}

static void smith_make_emit_node(const SmithMakeNode *node, const SmithMakeInfo *info) {
    switch (info->kind) {
    case SmithMakeKind_Object:
        smith_make_emit_object(node, info);
        break;
    case SmithMakeKind_Program:
        smith_make_emit_program(node, info);
        break;
    case SmithMakeKind_Step:
    case SmithMakeKind_Task:
        smith_make_emit_step(node, info);
        break;
    case SmithMakeKind_Alias:
        smith_make_emit_alias(node, info);
        break;
    case SmithMakeKind_Drop:
        if (info->note != nullptr && !info->directory) smith_make_comment(node, info->note);
        break;
    }
}
