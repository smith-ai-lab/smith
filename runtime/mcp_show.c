// What a stop, a stack, variables and an evaluation look like as text, and the requests that fetch them. Included by
// mcp.c. A result is for an agent to read: a stopped location with its source line, frames as `#n function file:line`,
// variables as `name = value (type)` with what is inside a struct indented under it.

constexpr size_t smith_mcp_depth_max = 6;                // how deep `variables` may expand
constexpr size_t smith_mcp_nodes_max = 2000;             // how many variables one result lists
constexpr size_t smith_mcp_source_max = (size_t)8 << 20; // a source file longer than this shows no line

// A path as short as it can be told: relative to the session's folder when it is inside it.
static void smith_mcp_path_add(SmithMcpText *out, const SmithMcpSession *session,
                               struct smith_array_const_char_n path) {
    size_t folder = strlen(session->cwd);
    if (folder > 0 && path.count[0] > folder + 1 && memcmp(path.data, session->cwd, folder) == 0 &&
        path.data[folder] == '/') {
        path.data += folder + 1;
        path.count[0] -= folder + 1;
    }
    smith_mcp_text_add_text(out, path);
}

// `#0 function file:line` (the line alone, no newline).
static void smith_mcp_frame_add(SmithMcpText *out, const SmithMcpSession *session, size_t index,
                                const SmithDapStackFrame *frame) {
    smith_mcp_text_add(out, "#%zu ", index);
    smith_mcp_text_add_text(out, frame->name);
    if (frame->has_source && frame->source.has_path) {
        smith_mcp_text_add(out, " ");
        smith_mcp_path_add(out, session, frame->source.path);
        smith_mcp_text_add(out, ":%lld", (long long)frame->line);
    } else if (frame->has_instructionPointerReference) {
        smith_mcp_text_add(out, " (no source, at ");
        smith_mcp_text_add_text(out, frame->instructionPointerReference);
        smith_mcp_text_add(out, ")");
    }
}

// The frames of the stopped thread, from `start`, at most `levels`. False, with the reason in out, if they cannot be
// had.
static bool smith_mcp_stack_get(SmithMcpServer *server, SmithMcpSession *session, int64_t start, int64_t levels,
                                SmithDapStackTraceResponseBody *body, SmithMcpText *out) {
    SmithDapStackTraceRequest request = {.arguments = {.threadId = session->thread,
                                                       .has_startFrame = true,
                                                       .startFrame = start,
                                                       .has_levels = true,
                                                       .levels = levels}};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, stack_trace_request, request, smith_mcp_request_timeout, reply);
    SmithProtocolError error;
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "stackTrace", out);
        return false;
    }
    if (!smith_dap_stack_trace_response_body_parse(&server->scratch, reply.response.body, body, &error)) {
        smith_mcp_text_add(out, "stackTrace: the adapter's answer could not be read\n");
        return false;
    }
    return true;
}

// The line of a source file, as `  > 12: text`; nothing if the file or the line is not there.
static void smith_mcp_source_line(SmithMcpServer *server, struct smith_array_const_char_n path, int64_t line,
                                  SmithMcpText *out) {
    if (line < 1) return;
    SmithArenaScope scope = smith_arena_scope_begin(&server->scratch);
    const char *name = smith_mcp_cstring(&server->scratch, path);
    SmithFileInfo info = smith_files_info(&server->io->files, name);
    if (info.error.code == 0 && !info.folder && info.size <= smith_mcp_source_max) {
        SmithRead read = smith_files_read(&server->io->files, &server->scratch, name);
        const char *cursor = (const char *)read.data.data;
        const char *end = cursor != nullptr ? cursor + read.data.count[0] : nullptr;
        for (int64_t number = 1; read.error.code == 0 && cursor != nullptr && cursor < end;
             number++) { // bounded by the file
            const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
            const char *stop = newline != nullptr ? newline : end;
            if (number == line) {
                size_t length = (size_t)(stop - cursor);
                while (length > 0 && (cursor[length - 1] == '\r' || cursor[length - 1] == ' '))
                    length--;
                if (length > 200) length = 200;
                smith_mcp_text_add(out, "  > %lld: %.*s\n", (long long)line, (int)length, cursor);
                break;
            }
            cursor = stop + 1;
        }
    }
    smith_arena_scope_end(scope);
}

// `stopped: breakpoint (breakpoint 1)`, then the top frame and its source line.
static void smith_mcp_show_stop(SmithMcpServer *server, SmithMcpSession *session, const SmithMcpEvent *stop,
                                SmithMcpText *out) {
    smith_mcp_text_add(out, "stopped: %s", stop->reason);
    if (stop->breakpoint > 0) smith_mcp_text_add(out, " (breakpoint %lld)", (long long)stop->breakpoint);
    // The adapter's description of a breakpoint or step stop repeats the reason ("breakpoint 1.1", "step over").
    if (stop->text[0] != '\0' && strcmp(stop->reason, "breakpoint") != 0 && strcmp(stop->reason, "step") != 0)
        smith_mcp_text_add(out, ": %s", stop->text);
    smith_mcp_text_add(out, "\n");
    SmithArenaScope scope = smith_arena_scope_begin(&server->scratch);
    SmithDapStackTraceResponseBody body;
    SmithMcpText ignored = smith_mcp_text_make(&server->scratch, 512);
    if (smith_mcp_stack_get(server, session, 0, 1, &body, &ignored) && body.stackFrames.count > 0) {
        const SmithDapStackFrame *frame = &body.stackFrames.items[0];
        smith_mcp_text_add(out, "  ");
        smith_mcp_frame_add(out, session, 0, frame);
        smith_mcp_text_add(out, "\n");
        if (frame->has_source && frame->source.has_path)
            smith_mcp_source_line(server, frame->source.path, frame->line, out);
    }
    smith_arena_scope_end(scope);
}

// What the call learned of the program ending.
static void smith_mcp_show_finished(const SmithMcpSession *session, SmithMcpText *out) {
    if (session->has_exit_code)
        smith_mcp_text_add(out, "exited: the program exited with code %lld\n", (long long)session->exit_code);
    else
        smith_mcp_text_add(out, "exited: the program has ended (the debug adapter is gone)\n");
    smith_mcp_text_add(out, "the session is over: call stop, then launch to debug again\n");
}

// --- variables ----------------------------------------------------------------------------------------------------

typedef struct {
    SmithDapVariableList list;
    size_t next;
} SmithMcpVariableLevel;

// The children of a variable reference, at most `max` (one more is asked for, to know whether there are others).
static bool smith_mcp_variables_get(SmithMcpServer *server, SmithMcpSession *session, int64_t reference, int64_t max,
                                    SmithDapVariableList *list, SmithMcpText *out) {
    SmithDapVariablesRequest request = {
        .arguments = {.variablesReference = reference, .has_count = true, .count = max + 1}};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, variables_request, request, smith_mcp_request_timeout, reply);
    SmithDapVariablesResponseBody body;
    SmithProtocolError error;
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "variables", out);
        return false;
    }
    if (!smith_dap_variables_response_body_parse(&server->scratch, reply.response.body, &body, &error)) {
        smith_mcp_text_add(out, "variables: the adapter's answer could not be read\n");
        return false;
    }
    *list = body.variables;
    return true;
}

// `name = value (type)`, on one line: newlines in the value become spaces, and a long value is cut.
static void smith_mcp_variable_add(SmithMcpText *out, size_t indent, const SmithDapVariable *variable) {
    char value[320];
    size_t length = variable->value.count[0] < sizeof value - 4 ? variable->value.count[0] : sizeof value - 4;
    for (size_t i = 0; i < length; i++)
        value[i] = variable->value.data[i] == '\n' ? ' ' : variable->value.data[i];
    value[length] = '\0';
    smith_mcp_text_add(out, "%*s", (int)(indent * 2), "");
    smith_mcp_text_add_text(out, variable->name);
    // An aggregate's value is its type's name; the type says it once.
    bool same = variable->has_type && variable->type.count[0] == variable->value.count[0] &&
                memcmp(variable->type.data, variable->value.data, variable->type.count[0]) == 0;
    if (!same) smith_mcp_text_add(out, " = %s%s", value, length < variable->value.count[0] ? "..." : "");
    if (variable->has_type && variable->type.count[0] > 0) {
        smith_mcp_text_add(out, " (");
        smith_mcp_text_add_text(out, variable->type);
        smith_mcp_text_add(out, ")");
    }
    smith_mcp_text_add(out, "\n");
}

// Lists the variables of a reference and, to `depth` levels, what is inside them, each level indented under its
// parent. Nothing recurses: the levels are a stack of at most smith_mcp_depth_max + 1, and the count of variables
// listed is bounded. A level shows at most `max` variables and says how many it left out.
static void smith_mcp_show_variables(SmithMcpServer *server, SmithMcpSession *session, int64_t reference, int64_t depth,
                                     int64_t max, SmithMcpText *out) {
    SMITH_ASSERT(depth >= 0 && depth <= (int64_t)smith_mcp_depth_max && max > 0);
    SmithMcpVariableLevel stack[smith_mcp_depth_max + 1] = {};
    size_t top = 0;
    if (!smith_mcp_variables_get(server, session, reference, max, &stack[0].list, out)) return;
    top = 1;
    size_t listed = 0;
    for (size_t turn = 0; turn < smith_mcp_nodes_max * 2 && top > 0; turn++) {
        SmithMcpVariableLevel *level = &stack[top - 1];
        size_t shown = level->list.count < (size_t)max ? level->list.count : (size_t)max;
        if (level->next >= shown) {
            if (level->list.count > shown)
                smith_mcp_text_add(out, "%*s... more not shown (raise max)\n", (int)(top * 2 - 2), "");
            top--;
            continue;
        }
        const SmithDapVariable *variable = &level->list.items[level->next++];
        smith_mcp_variable_add(out, top - 1, variable);
        if (++listed >= smith_mcp_nodes_max) {
            smith_mcp_text_add(out, "... too many variables; narrow the request\n");
            return;
        }
        if (variable->variablesReference > 0 && (int64_t)top <= depth) {
            stack[top] = (SmithMcpVariableLevel){};
            if (smith_mcp_variables_get(server, session, variable->variablesReference, max, &stack[top].list, out))
                top++;
        }
    }
}
