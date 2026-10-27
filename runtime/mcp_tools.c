// The tools: each takes the tool call's arguments and writes its result as text into `out`, and returns false when the
// result is an error. Included by mcp.c.
//
// A tool that resumes the program (continue, the steps) sends the request, then takes events in order until the program
// stops or ends or the time is up. Every tool first takes in what the program did since the last call (its output, a
// stop, an exit), so nothing is lost between calls.

typedef bool (*SmithMcpToolFunction)(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

// --- arguments and sessions ----------------------------------------------------------------------------------------

// The session the call means (its `session` argument, else the latest). False, with the reason in out, if there is
// none.
static SmithMcpSession *smith_mcp_tool_session(SmithMcpServer *server, const SmithJsonValue *arguments,
                                               SmithMcpText *out) {
    const char *id = smith_mcp_argument_string(&server->scratch, arguments, "session");
    SmithMcpSession *session = id != nullptr ? smith_mcp_session_find(server, id) : smith_mcp_session_latest(server);
    if (session == nullptr) {
        if (id != nullptr)
            smith_mcp_text_add(out, "no debug session %s\n", id);
        else
            smith_mcp_text_add(out, "no debug session: call launch first\n");
    }
    return session;
}

// Takes in what happened since the last call: the program's output goes into the notes, and a stop or an exit that came
// while no call was waiting is reported where the call's own result begins.
static void smith_mcp_tool_begin(SmithMcpServer *server, SmithMcpSession *session, SmithMcpText *out) {
    smith_mcp_pump(server, 1); // looks at what has arrived
    SmithMcpEvent stop = {};
    SmithMcpTake taken = smith_mcp_events_take(session, out, &stop);
    if (taken == SmithMcpTake_Stopped) smith_mcp_show_stop(server, session, &stop, out);
    if (taken == SmithMcpTake_Finished) smith_mcp_show_finished(session, out);
}

// A call that needs the program stopped.
static bool smith_mcp_tool_stopped(const SmithMcpSession *session, SmithMcpText *out) {
    if (session->stopped) return true;
    if (session->finished)
        smith_mcp_text_add(out, "the program has exited; call stop, then launch again\n");
    else
        smith_mcp_text_add(out, "the program is running: call pause (or wait with continue) first\n");
    return false;
}

// A path made absolute against the folder.
static const char *smith_mcp_absolute(SmithArena *arena, const char *folder, const char *path) {
    if (path[0] == '/') return path;
    size_t length = strlen(folder) + strlen(path) + 2;
    char *joined = smith_arena_push(arena, length, 1);
    SMITH_ASSERT(joined != nullptr);
    snprintf(joined, length, "%s/%s", folder, path);
    return joined;
}

static const SmithJsonValue *smith_mcp_json_string(SmithArena *arena, struct smith_array_const_char_n text) {
    SmithJsonValue *value = smith_arena_push(arena, sizeof(SmithJsonValue), alignof(SmithJsonValue));
    SMITH_ASSERT(value != nullptr);
    *value = (SmithJsonValue){.kind = SmithJsonKind_String, .text = text, .span = 1};
    return value;
}

static const SmithJsonValue *smith_mcp_json_true(SmithArena *arena) {
    SmithJsonValue *value = smith_arena_push(arena, sizeof(SmithJsonValue), alignof(SmithJsonValue));
    SMITH_ASSERT(value != nullptr);
    *value = (SmithJsonValue){.kind = SmithJsonKind_True, .span = 1};
    return value;
}

// An array of strings, as the tree a protocol's "any" member holds.
static const SmithJsonValue *smith_mcp_json_strings(SmithArena *arena, const char *const *strings, size_t count) {
    SmithJsonValue *array = smith_arena_push(arena, sizeof(SmithJsonValue), alignof(SmithJsonValue));
    const SmithJsonValue **items = (const SmithJsonValue **)smith_arena_push_array(
        arena, sizeof(const SmithJsonValue *), count + 1, alignof(const SmithJsonValue *));
    SMITH_ASSERT(array != nullptr && items != nullptr);
    for (size_t i = 0; i < count; i++)
        items[i] = smith_mcp_json_string(arena, smith_format_string(strings[i]));
    *array = (SmithJsonValue){.kind = SmithJsonKind_Array, .count = count, .items = items, .span = count + 1};
    return array;
}

// --- breakpoints ---------------------------------------------------------------------------------------------------

// Sends all the breakpoints of one source file (setBreakpoints replaces the file's whole set) and records what the
// adapter says of each: its id, whether it is verified, the line it settled on.
static bool smith_mcp_breakpoints_send(SmithMcpServer *server, SmithMcpSession *session, const char *path,
                                       SmithMcpText *out) {
    SmithDapSourceBreakpoint list[smith_mcp_breakpoints_max] = {};
    size_t index[smith_mcp_breakpoints_max];
    size_t count = 0;
    for (size_t i = 0; i < session->breakpoint_count; i++) { // bounded by the table
        SmithMcpBreakpoint *breakpoint = &session->breakpoints[i];
        if (strcmp(breakpoint->path, path) != 0) continue;
        list[count].line = breakpoint->line;
        if (breakpoint->condition[0] != '\0') {
            list[count].has_condition = true;
            list[count].condition = smith_format_string(breakpoint->condition);
        }
        index[count++] = i;
    }
    SmithDapSetBreakpointsRequest request = {.arguments = {.source = {.has_path = true, .path = smith_format_string(path)},
                                                           .has_breakpoints = true,
                                                           .breakpoints = {list, count}}};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, set_breakpoints_request, request, smith_mcp_request_timeout, reply);
    SmithDapSetBreakpointsResponseBody body;
    SmithProtocolError error;
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "setBreakpoints", out);
        return false;
    }
    if (!smith_dap_set_breakpoints_response_body_parse(&server->scratch, reply.response.body, &body, &error)) {
        smith_mcp_text_add(out, "setBreakpoints: the adapter's answer could not be read\n");
        return false;
    }
    for (size_t i = 0; i < count && i < body.breakpoints.count; i++) { // bounded by the lists
        const SmithDapBreakpoint *answer = &body.breakpoints.items[i];
        SmithMcpBreakpoint *breakpoint = &session->breakpoints[index[i]];
        breakpoint->id = answer->has_id ? answer->id : 0;
        breakpoint->verified = answer->verified;
        if (answer->has_line) breakpoint->line = answer->line;
        breakpoint->message[0] = '\0';
        if (answer->has_message) smith_mcp_copy(breakpoint->message, sizeof breakpoint->message, answer->message);
    }
    return true;
}

// Adds a breakpoint to the session's table (or finds the one at that place, whose condition is replaced).
static SmithMcpBreakpoint *smith_mcp_breakpoint_add(SmithMcpSession *session, const char *path, int64_t line,
                                                    const char *condition) {
    SmithMcpBreakpoint *found = nullptr;
    for (size_t i = 0; i < session->breakpoint_count; i++) { // bounded by the table
        if (session->breakpoints[i].line == line && strcmp(session->breakpoints[i].path, path) == 0)
            found = &session->breakpoints[i];
    }
    if (found == nullptr) {
        if (session->breakpoint_count == smith_mcp_breakpoints_max) return nullptr;
        found = &session->breakpoints[session->breakpoint_count++];
        *found = (SmithMcpBreakpoint){};
        snprintf(found->path, sizeof found->path, "%s", path);
        found->line = line;
    }
    snprintf(found->condition, sizeof found->condition, "%s", condition != nullptr ? condition : "");
    return found;
}

static void smith_mcp_breakpoint_show(const SmithMcpSession *session, const SmithMcpBreakpoint *breakpoint,
                                      SmithMcpText *out) {
    smith_mcp_text_add(out, "breakpoint %lld: ", (long long)breakpoint->id);
    smith_mcp_path_add(out, session, smith_format_string(breakpoint->path));
    smith_mcp_text_add(out, ":%lld", (long long)breakpoint->line);
    if (breakpoint->condition[0] != '\0') smith_mcp_text_add(out, " if %s", breakpoint->condition);
    if (breakpoint->verified)
        smith_mcp_text_add(out, " (verified)\n");
    else
        smith_mcp_text_add(out, " (not verified%s%s)\n", breakpoint->message[0] != '\0' ? ": " : "",
                           breakpoint->message);
}

static bool smith_mcp_tool_breakpoint(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    SmithMcpSession *session = smith_mcp_tool_session(server, arguments, out);
    if (session == nullptr) return false;
    const char *file = smith_mcp_argument_string(&server->scratch, arguments, "file");
    int64_t line = smith_mcp_argument_integer(arguments, "line", 0);
    if (file == nullptr || line < 1) {
        smith_mcp_text_add(out, "breakpoint needs a file and a line (1 or more)\n");
        return false;
    }
    smith_mcp_tool_begin(server, session, out);
    const char *path = smith_mcp_absolute(&server->scratch, session->cwd, file);
    const char *condition = smith_mcp_argument_string(&server->scratch, arguments, "condition");
    bool remove = smith_mcp_argument_bool(arguments, "remove", false);
    if (remove) {
        size_t kept = 0;
        for (size_t i = 0; i < session->breakpoint_count; i++) { // bounded by the table
            if (session->breakpoints[i].line == line && strcmp(session->breakpoints[i].path, path) == 0) continue;
            session->breakpoints[kept++] = session->breakpoints[i];
        }
        bool removed = kept < session->breakpoint_count;
        session->breakpoint_count = kept;
        if (!removed) {
            smith_mcp_text_add(out, "no breakpoint at %s:%lld\n", file, (long long)line);
            return false;
        }
        if (!smith_mcp_breakpoints_send(server, session, path, out)) return false;
        smith_mcp_text_add(out, "removed the breakpoint at %s:%lld\n", file, (long long)line);
        return true;
    }
    SmithMcpBreakpoint *breakpoint = smith_mcp_breakpoint_add(session, path, line, condition);
    if (breakpoint == nullptr) {
        smith_mcp_text_add(out, "too many breakpoints (%zu)\n", smith_mcp_breakpoints_max);
        return false;
    }
    if (!smith_mcp_breakpoints_send(server, session, path, out)) return false;
    smith_mcp_breakpoint_show(session, breakpoint, out);
    return true;
}

// --- launch --------------------------------------------------------------------------------------------------------

constexpr size_t smith_mcp_program_arguments_max = 64;
constexpr size_t smith_mcp_launch_breakpoints_max = 16;

static const char *const smith_mcp_runners[] = {"build/tests", "build/debug/tests", "build/sanitize/tests",
                                                "build/test/tests"};

// The runner `.smith/build.info` of the project names (`runner<TAB>path`, relative to the folder) that is a file, or
// nullptr when there is no build.info or it names none.
static const char *smith_mcp_runner_of_build(SmithMcpServer *server, const char *folder) {
    const char *info_path = smith_mcp_absolute(&server->scratch, folder, ".smith/build.info");
    if (smith_files_info(&server->io->files, info_path).error.code != 0) return nullptr;
    SmithRead read = smith_files_read(&server->io->files, &server->scratch, info_path);
    if (read.error.code != 0 || read.data.data == nullptr) return nullptr;
    const char *cursor = (const char *)read.data.data;
    const char *end = cursor + read.data.count[0];
    while (cursor < end) { // bounded by the file
        const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
        const char *stop = newline != nullptr ? newline : end;
        if ((size_t)(stop - cursor) > sizeof "runner\t" - 1 && memcmp(cursor, "runner\t", sizeof "runner\t" - 1) == 0) {
            const char *path = smith_mcp_cstring(
                &server->scratch,
                (struct smith_array_const_char_n){.data = cursor + sizeof "runner\t" - 1,
                                                  .count = {(size_t)(stop - cursor) - (sizeof "runner\t" - 1)}});
            const char *absolute = smith_mcp_absolute(&server->scratch, folder, path);
            SmithFileInfo info = smith_files_info(&server->io->files, absolute);
            if (info.error.code == 0 && !info.folder) return absolute;
        }
        cursor = stop + 1;
    }
    return nullptr;
}

// The test runner of the project: the `runner` argument, else the one `.smith/build.info` names, else (a project
// built before it said) the first of the usual places that is there.
static const char *smith_mcp_runner(SmithMcpServer *server, const SmithJsonValue *arguments, const char *folder,
                                    SmithMcpText *out) {
    const char *named = smith_mcp_argument_string(&server->scratch, arguments, "runner");
    if (named != nullptr) return smith_mcp_absolute(&server->scratch, folder, named);
    const char *built = smith_mcp_runner_of_build(server, folder);
    if (built != nullptr) return built;
    for (size_t i = 0; i < sizeof smith_mcp_runners / sizeof smith_mcp_runners[0]; i++) {
        const char *path = smith_mcp_absolute(&server->scratch, folder, smith_mcp_runners[i]);
        SmithFileInfo info = smith_files_info(&server->io->files, path);
        if (info.error.code == 0 && !info.folder) return path;
    }
    smith_mcp_text_add(out,
                       "launch: no test runner found in .smith/build.info, build/tests, build/debug/tests, "
                       "build/sanitize/tests or build/test/tests under %s;\n"
                       "build the tests, or name the runner's path in `runner`\n",
                       folder);
    return nullptr;
}

// The arguments of the program: for a test, `--test <name> --in-process` (the runner's own process, so that a
// breakpoint in the test is hit), then the `args` array.
static size_t smith_mcp_program_arguments(SmithMcpServer *server, const SmithJsonValue *arguments, const char *test,
                                          const char **list) {
    size_t count = 0;
    if (test != nullptr) {
        list[count++] = "--test";
        list[count++] = test;
        list[count++] = "--in-process";
    }
    const SmithJsonValue *array = smith_mcp_argument(arguments, "args", SmithJsonKind_Array);
    for (size_t i = 0; array != nullptr && i < array->count && count < smith_mcp_program_arguments_max;
         i++) { // bounded
        if (array->items[i]->kind == SmithJsonKind_String)
            list[count++] = smith_mcp_cstring(&server->scratch, array->items[i]->text);
    }
    return count;
}

// The breakpoints named at launch: [{file, line, condition?}], recorded in the session. False for one that is no
// object.
static bool smith_mcp_launch_breakpoints(SmithMcpServer *server, SmithMcpSession *session,
                                         const SmithJsonValue *arguments, const char **paths, size_t *path_count,
                                         SmithMcpText *out) {
    const SmithJsonValue *array = smith_mcp_argument(arguments, "breakpoints", SmithJsonKind_Array);
    for (size_t i = 0; array != nullptr && i < array->count && i < smith_mcp_launch_breakpoints_max; i++) { // bounded
        const SmithJsonValue *item = array->items[i];
        const char *file = smith_mcp_argument_string(&server->scratch, item, "file");
        int64_t line = smith_mcp_argument_integer(item, "line", 0);
        if (file == nullptr || line < 1) {
            smith_mcp_text_add(out, "launch: each breakpoint needs a file and a line\n");
            return false;
        }
        const char *path = smith_mcp_absolute(&server->scratch, session->cwd, file);
        const char *condition = smith_mcp_argument_string(&server->scratch, item, "condition");
        if (smith_mcp_breakpoint_add(session, path, line, condition) == nullptr) return false;
        bool known = false;
        for (size_t j = 0; j < *path_count; j++)
            known = known || strcmp(paths[j], path) == 0;
        if (!known) paths[(*path_count)++] = path;
    }
    return true;
}

static bool smith_mcp_launch_initialize(SmithMcpServer *server, SmithMcpSession *session, SmithMcpText *out) {
    SmithDapInitializeRequest request = {
        .arguments = {.has_clientID = true,
                      .clientID = smith_format_string("smith-mcp"),
                      .has_clientName = true,
                      .clientName = smith_format_string("smith mcp"),
                      .adapterID = smith_format_string("smith"),
                      .has_linesStartAt1 = true,
                      .linesStartAt1 = true,
                      .has_columnsStartAt1 = true,
                      .columnsStartAt1 = true,
                      .has_pathFormat = true,
                      .pathFormat = {.known = SmithDapInitializeRequestArgumentsPathFormatKnown_Path},
                      .has_supportsVariableType = true,
                      .supportsVariableType = true}};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, initialize_request, request, smith_mcp_start_timeout, reply);
    if (reply.status == SmithMcpReply_Ok) return true;
    smith_mcp_reply_error(&reply, "initialize", out);
    return false;
}

// Sends the launch request: the program, its arguments, the folder, and stopOnEntry unless breakpoints were given.
static bool smith_mcp_launch_send(SmithMcpServer *server, SmithMcpSession *session, const char *program,
                                  const char *const *list, size_t count, bool stop_on_entry, int64_t *seq,
                                  SmithMcpText *out) {
    struct smith_array_const_char_n keys[] = {smith_format_string("program"), smith_format_string("args"),
                                              smith_format_string("cwd"), smith_format_string("stopOnEntry")};
    SmithProtocolAny values[] = {smith_mcp_json_string(&server->scratch, smith_format_string(program)),
                                 smith_mcp_json_strings(&server->scratch, list, count),
                                 smith_mcp_json_string(&server->scratch, smith_format_string(session->cwd)),
                                 stop_on_entry ? smith_mcp_json_true(&server->scratch) : nullptr};
    SmithDapLaunchRequest request = {.arguments = {.additionalProperties = {keys, values, stop_on_entry ? 4 : 3}}};
    bool sent = false;
    SMITH_MCP_DAP_SEND(server, session, launch_request, request, sent);
    *seq = request.seq;
    if (!sent) smith_mcp_text_add(out, "launch: the debug adapter would not take the request\n");
    return sent;
}

// The configuration the adapter asks for after `initialized`: the breakpoints, then configurationDone, then the answer
// to the launch request itself (a refusal arrives as its failure).
static bool smith_mcp_launch_configure(SmithMcpServer *server, SmithMcpSession *session, int64_t seq,
                                       const char **paths, size_t path_count, SmithMcpText *out) {
    for (size_t i = 0; i < path_count; i++) { // bounded by the breakpoints
        if (!smith_mcp_breakpoints_send(server, session, paths[i], out)) return false;
    }
    SmithDapConfigurationDoneRequest done = {};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, configuration_done_request, done, smith_mcp_request_timeout, reply);
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "configurationDone", out);
        return false;
    }
    reply = smith_mcp_dap_await(server, session, seq, smith_mcp_start_timeout);
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "launch", out);
        return false;
    }
    return true;
}

static bool smith_mcp_tool_launch(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    const char *program = smith_mcp_argument_string(&server->scratch, arguments, "program");
    const char *test = smith_mcp_argument_string(&server->scratch, arguments, "test");
    const char *folder = smith_mcp_argument_string(&server->scratch, arguments, "cwd");
    if (folder == nullptr) folder = server->cwd;
    if ((program == nullptr) == (test == nullptr)) {
        smith_mcp_text_add(
            out, "launch needs `program` (a program to run) or `test` (the name of a test of the project), not both\n");
        return false;
    }
    program = test != nullptr ? smith_mcp_runner(server, arguments, folder, out)
                              : smith_mcp_absolute(&server->scratch, folder, program);
    if (program == nullptr) return false;
    SmithFileInfo info = smith_files_info(&server->io->files, program);
    if (info.error.code != 0 || info.folder) {
        smith_mcp_text_add(out, "launch: %s is not a file; build the program first (with -g)\n", program);
        return false;
    }
    const char *list[smith_mcp_program_arguments_max];
    size_t count = smith_mcp_program_arguments(server, arguments, test, list);
    SmithMcpSession *session = smith_mcp_session_start(server, out);
    if (session == nullptr) return false;
    snprintf(session->cwd, sizeof session->cwd, "%s", folder);
    snprintf(session->program, sizeof session->program, "%s", program);
    const char *paths[smith_mcp_launch_breakpoints_max];
    size_t path_count = 0;
    int64_t seq = 0;
    bool ok = smith_mcp_launch_breakpoints(server, session, arguments, paths, &path_count, out) &&
              smith_mcp_launch_initialize(server, session, out) &&
              smith_mcp_launch_send(server, session, program, list, count, path_count == 0, &seq, out);
    if (ok && !smith_mcp_wait_initialized(server, session, seq, smith_mcp_start_timeout)) {
        SmithMcpReply reply = smith_mcp_dap_await(server, session, seq, smith_mcp_request_timeout);
        smith_mcp_reply_error(&reply, "launch", out);
        ok = false;
    }
    ok = ok && smith_mcp_launch_configure(server, session, seq, paths, path_count, out);
    if (!ok) {
        SmithMcpEvent ignored = {};
        (void)smith_mcp_events_take(session, out, &ignored); // what the adapter said before it failed
        smith_mcp_session_close(server, session, false);
        return false;
    }
    smith_mcp_text_add(out, "session %s: %s", session->id, program);
    for (size_t i = 0; i < count; i++)
        smith_mcp_text_add(out, " %s", list[i]);
    smith_mcp_text_add(out, "\n");
    for (size_t i = 0; i < session->breakpoint_count; i++)
        smith_mcp_breakpoint_show(session, &session->breakpoints[i], out);
    SmithMcpEvent stop = {};
    SmithMcpTake taken = smith_mcp_wait_stop(
        server, session, smith_mcp_argument_timeout(arguments, smith_mcp_resume_timeout), out, &stop);
    if (taken == SmithMcpTake_Stopped) smith_mcp_show_stop(server, session, &stop, out);
    if (taken == SmithMcpTake_Finished) smith_mcp_show_finished(session, out);
    if (taken == SmithMcpTake_Nothing)
        smith_mcp_text_add(out, "the program is running (no stop yet): call pause, or a call that waits\n");
    return true;
}

// --- resuming ------------------------------------------------------------------------------------------------------

typedef enum {
    SmithMcpResume_Continue,
    SmithMcpResume_Over,
    SmithMcpResume_In,
    SmithMcpResume_Out,
} SmithMcpResume;

// Sends the resuming request of the kind. The program is marked running first, so a stop already queued (a script's, or
// one the adapter sent at once) is the answer.
static SmithMcpReply smith_mcp_resume_send(SmithMcpServer *server, SmithMcpSession *session, SmithMcpResume kind) {
    SmithMcpReply reply;
    int64_t thread = session->thread;
    session->stopped = false;
    if (kind == SmithMcpResume_Continue) {
        SmithDapContinueRequest request = {.arguments = {.threadId = thread}};
        SMITH_MCP_DAP_CALL(server, session, continue_request, request, smith_mcp_request_timeout, reply);
    } else if (kind == SmithMcpResume_Over) {
        SmithDapNextRequest request = {.arguments = {.threadId = thread}};
        SMITH_MCP_DAP_CALL(server, session, next_request, request, smith_mcp_request_timeout, reply);
    } else if (kind == SmithMcpResume_In) {
        SmithDapStepInRequest request = {.arguments = {.threadId = thread}};
        SMITH_MCP_DAP_CALL(server, session, step_in_request, request, smith_mcp_request_timeout, reply);
    } else {
        SmithDapStepOutRequest request = {.arguments = {.threadId = thread}};
        SMITH_MCP_DAP_CALL(server, session, step_out_request, request, smith_mcp_request_timeout, reply);
    }
    return reply;
}

static bool smith_mcp_resume(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out,
                             SmithMcpResume kind) {
    static const char *const names[] = {"continue", "next", "stepIn", "stepOut"};
    SmithMcpSession *session = smith_mcp_tool_session(server, arguments, out);
    if (session == nullptr) return false;
    smith_mcp_tool_begin(server, session, out);
    if (session->finished) {
        smith_mcp_text_add(out, "the program has exited; call stop, then launch again\n");
        return false;
    }
    if (session->stopped) {
        SmithMcpReply reply = smith_mcp_resume_send(server, session, kind);
        if (reply.status != SmithMcpReply_Ok) {
            session->stopped = reply.status == SmithMcpReply_Failed; // a refusal leaves it stopped
            smith_mcp_reply_error(&reply, names[kind], out);
            return false;
        }
    }
    SmithMcpEvent stop = {};
    int64_t timeout = smith_mcp_argument_timeout(arguments, smith_mcp_resume_timeout);
    SmithMcpTake taken = smith_mcp_wait_stop(server, session, timeout, out, &stop);
    if (taken == SmithMcpTake_Stopped) smith_mcp_show_stop(server, session, &stop, out);
    if (taken == SmithMcpTake_Finished) smith_mcp_show_finished(session, out);
    if (taken == SmithMcpTake_Nothing)
        smith_mcp_text_add(out, "still running after %lld ms: call pause to stop it, or continue to wait again\n",
                           (long long)(timeout / 1'000'000));
    return true;
}

static bool smith_mcp_tool_continue(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    return smith_mcp_resume(server, arguments, out, SmithMcpResume_Continue);
}

static bool smith_mcp_tool_step_over(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    return smith_mcp_resume(server, arguments, out, SmithMcpResume_Over);
}

static bool smith_mcp_tool_step_in(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    return smith_mcp_resume(server, arguments, out, SmithMcpResume_In);
}

static bool smith_mcp_tool_step_out(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    return smith_mcp_resume(server, arguments, out, SmithMcpResume_Out);
}

static bool smith_mcp_tool_pause(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    SmithMcpSession *session = smith_mcp_tool_session(server, arguments, out);
    if (session == nullptr) return false;
    smith_mcp_tool_begin(server, session, out);
    if (session->stopped || session->finished) {
        smith_mcp_text_add(out, session->stopped ? "the program is already stopped\n" : "the program has exited\n");
        return session->stopped;
    }
    SmithDapPauseRequest request = {.arguments = {.threadId = session->thread > 0 ? session->thread : 1}};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, pause_request, request, smith_mcp_request_timeout, reply);
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "pause", out);
        return false;
    }
    SmithMcpEvent stop = {};
    SmithMcpTake taken =
        smith_mcp_wait_stop(server, session, smith_mcp_argument_timeout(arguments, 10 * smith_mcp_second), out, &stop);
    if (taken == SmithMcpTake_Stopped) smith_mcp_show_stop(server, session, &stop, out);
    if (taken == SmithMcpTake_Finished) smith_mcp_show_finished(session, out);
    if (taken == SmithMcpTake_Nothing) smith_mcp_text_add(out, "the program did not stop in time\n");
    return taken != SmithMcpTake_Nothing;
}

// --- looking ------------------------------------------------------------------------------------------------------

static bool smith_mcp_tool_stack(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    SmithMcpSession *session = smith_mcp_tool_session(server, arguments, out);
    if (session == nullptr) return false;
    smith_mcp_tool_begin(server, session, out);
    if (!smith_mcp_tool_stopped(session, out)) return false;
    int64_t levels = smith_mcp_argument_integer(arguments, "levels", 20);
    if (levels < 1) levels = 1;
    if (levels > 200) levels = 200;
    SmithDapStackTraceResponseBody body;
    if (!smith_mcp_stack_get(server, session, 0, levels, &body, out)) return false;
    smith_mcp_text_add(out, "thread %lld, %zu frames\n", (long long)session->thread, body.stackFrames.count);
    for (size_t i = 0; i < body.stackFrames.count; i++) { // bounded by the levels asked for
        smith_mcp_frame_add(out, session, i, &body.stackFrames.items[i]);
        smith_mcp_text_add(out, "\n");
    }
    if (body.has_totalFrames && body.totalFrames > (int64_t)body.stackFrames.count)
        smith_mcp_text_add(out, "... %lld more frames (raise levels)\n",
                           (long long)(body.totalFrames - (int64_t)body.stackFrames.count));
    return true;
}

// The id of the frame at that index of the stopped thread's stack.
static bool smith_mcp_frame_id(SmithMcpServer *server, SmithMcpSession *session, int64_t index, int64_t *id,
                               SmithMcpText *out) {
    SmithDapStackTraceResponseBody body;
    if (!smith_mcp_stack_get(server, session, 0, index + 1, &body, out)) return false;
    if ((size_t)index >= body.stackFrames.count) {
        smith_mcp_text_add(out, "the stack has %zu frames; there is no frame %lld\n", body.stackFrames.count,
                           (long long)index);
        return false;
    }
    *id = body.stackFrames.items[index].id;
    return true;
}

// The variables reference of the named scope (Locals, Globals, Registers...) of a frame.
static bool smith_mcp_scope(SmithMcpServer *server, SmithMcpSession *session, int64_t frame, const char *name,
                            int64_t *reference, SmithMcpText *out) {
    SmithDapScopesRequest request = {.arguments = {.frameId = frame}};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, scopes_request, request, smith_mcp_request_timeout, reply);
    SmithDapScopesResponseBody body;
    SmithProtocolError error;
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "scopes", out);
        return false;
    }
    if (!smith_dap_scopes_response_body_parse(&server->scratch, reply.response.body, &body, &error)) {
        smith_mcp_text_add(out, "scopes: the adapter's answer could not be read\n");
        return false;
    }
    for (size_t i = 0; i < body.scopes.count; i++) { // bounded by the scopes
        const SmithDapScope *scope = &body.scopes.items[i];
        if (scope->name.count[0] == strlen(name) && strncasecmp(scope->name.data, name, scope->name.count[0]) == 0) {
            *reference = scope->variablesReference;
            return true;
        }
    }
    smith_mcp_text_add(out, "no scope %s; the frame has:", name);
    for (size_t i = 0; i < body.scopes.count; i++) {
        smith_mcp_text_add(out, " ");
        smith_mcp_text_add_text(out, body.scopes.items[i].name);
    }
    smith_mcp_text_add(out, "\n");
    return false;
}

static bool smith_mcp_tool_variables(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    SmithMcpSession *session = smith_mcp_tool_session(server, arguments, out);
    if (session == nullptr) return false;
    smith_mcp_tool_begin(server, session, out);
    if (!smith_mcp_tool_stopped(session, out)) return false;
    int64_t frame_index = smith_mcp_argument_integer(arguments, "frame", 0);
    int64_t depth = smith_mcp_argument_integer(arguments, "depth", 1);
    int64_t max = smith_mcp_argument_integer(arguments, "max", 50);
    const char *scope = smith_mcp_argument_string(&server->scratch, arguments, "scope");
    if (frame_index < 0) frame_index = 0;
    if (depth < 0) depth = 0;
    if (depth > (int64_t)smith_mcp_depth_max) depth = (int64_t)smith_mcp_depth_max;
    if (max < 1) max = 1;
    if (max > 500) max = 500;
    int64_t frame = 0;
    int64_t reference = 0;
    if (!smith_mcp_frame_id(server, session, frame_index, &frame, out) ||
        !smith_mcp_scope(server, session, frame, scope != nullptr ? scope : "locals", &reference, out))
        return false;
    smith_mcp_text_add(out, "frame #%lld, scope %s\n", (long long)frame_index, scope != nullptr ? scope : "locals");
    smith_mcp_show_variables(server, session, reference, depth, max, out);
    return true;
}

// Whether the expression looks as if it changes the program: an assignment, ++ or --, a call of a function, or a
// debugger command (a backtick). A lexical test, so it errs towards refusing; allow_side_effects lifts it.
static bool smith_mcp_expression_mutates(const char *text) {
    static const char *const operators[] = {
        "sizeof", "alignof", "_Alignof", "_Countof", "offsetof", "__builtin_offsetof", "typeof", "__typeof__"};
    size_t length = strlen(text);
    for (size_t i = 0; i < length; i++) { // bounded by the text
        char c = text[i];
        char before = i > 0 ? text[i - 1] : '\0';
        char after = i + 1 < length ? text[i + 1] : '\0';
        if (c == '`') return true;
        if (c == '=' && after != '=' && before != '=' && before != '!' && before != '<' && before != '>') return true;
        if (c == '=' && (before == '<' || before == '>') && i > 1 && text[i - 2] == before) return true; // <<= and >>=
        if ((c == '+' || c == '-') && after == c) return true;
        if (c != '(') continue;
        size_t end = i;
        while (end > 0 && text[end - 1] == ' ')
            end--;
        if (end == 0) continue;
        char last = text[end - 1];
        if (last == ')' || last == ']') return true; // (*f)(x), a[i](x)
        bool name =
            (last >= 'a' && last <= 'z') || (last >= 'A' && last <= 'Z') || (last >= '0' && last <= '9') || last == '_';
        if (!name) continue;
        size_t start = end;
        while (start > 0 && ((text[start - 1] >= 'a' && text[start - 1] <= 'z') ||
                             (text[start - 1] >= 'A' && text[start - 1] <= 'Z') ||
                             (text[start - 1] >= '0' && text[start - 1] <= '9') || text[start - 1] == '_'))
            start--;
        bool known = false;
        for (size_t j = 0; j < sizeof operators / sizeof operators[0]; j++)
            known =
                known || (strlen(operators[j]) == end - start && memcmp(operators[j], text + start, end - start) == 0);
        if (!known) return true;
    }
    return false;
}

static bool smith_mcp_tool_evaluate(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    SmithMcpSession *session = smith_mcp_tool_session(server, arguments, out);
    if (session == nullptr) return false;
    const char *expression = smith_mcp_argument_string(&server->scratch, arguments, "expression");
    if (expression == nullptr || expression[0] == '\0') {
        smith_mcp_text_add(out, "evaluate needs an expression\n");
        return false;
    }
    if (!smith_mcp_argument_bool(arguments, "allow_side_effects", false) && smith_mcp_expression_mutates(expression)) {
        smith_mcp_text_add(
            out,
            "refused: the expression may change the program (an assignment, ++ or --, a call, or a debugger command).\n"
            "Evaluation is read-only by default; pass allow_side_effects: true to run it.\n");
        return false;
    }
    smith_mcp_tool_begin(server, session, out);
    if (!smith_mcp_tool_stopped(session, out)) return false;
    int64_t depth = smith_mcp_argument_integer(arguments, "depth", 0);
    if (depth < 0) depth = 0;
    if (depth > (int64_t)smith_mcp_depth_max) depth = (int64_t)smith_mcp_depth_max;
    int64_t frame = 0;
    if (!smith_mcp_frame_id(server, session, smith_mcp_argument_integer(arguments, "frame", 0), &frame, out))
        return false;
    SmithDapEvaluateRequest request = {
        .arguments = {.expression = smith_format_string(expression),
                      .has_frameId = true,
                      .frameId = frame,
                      .has_context = true,
                      .context = {.known = SmithDapEvaluateArgumentsContextKnown_Watch}}};
    SmithMcpReply reply;
    SMITH_MCP_DAP_CALL(server, session, evaluate_request, request, smith_mcp_request_timeout, reply);
    SmithDapEvaluateResponseBody body;
    SmithProtocolError error;
    if (reply.status != SmithMcpReply_Ok) {
        smith_mcp_reply_error(&reply, "evaluate", out);
        return false;
    }
    if (!smith_dap_evaluate_response_body_parse(&server->scratch, reply.response.body, &body, &error)) {
        smith_mcp_text_add(out, "evaluate: the adapter's answer could not be read\n");
        return false;
    }
    smith_mcp_text_add(out, "%s = ", expression);
    smith_mcp_text_add_text(out, body.result);
    if (body.has_type && body.type.count[0] > 0) {
        smith_mcp_text_add(out, " (");
        smith_mcp_text_add_text(out, body.type);
        smith_mcp_text_add(out, ")");
    }
    smith_mcp_text_add(out, "\n");
    if (depth > 0 && body.variablesReference > 0)
        smith_mcp_show_variables(server, session, body.variablesReference, depth - 1, 50, out);
    return true;
}

static bool smith_mcp_tool_stop(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out) {
    SmithMcpSession *session = smith_mcp_tool_session(server, arguments, out);
    if (session == nullptr) return false;
    smith_mcp_pump(server, 1);
    SmithMcpEvent ignored = {};
    (void)smith_mcp_events_take(session, out, &ignored);
    char id[sizeof session->id];
    snprintf(id, sizeof id, "%s", session->id);
    bool exited = session->has_exit_code;
    int64_t code = session->exit_code;
    smith_mcp_session_close(server, session, true);
    smith_mcp_text_add(out, "session %s stopped", id);
    if (exited) smith_mcp_text_add(out, " (the program had exited with code %lld)", (long long)code);
    smith_mcp_text_add(out, "\n");
    return true;
}

// --- the table ----------------------------------------------------------------------------------------------------

typedef struct {
    const char *name;
    const char *description;
    const char *schema; // the tool's input, as JSON Schema
    bool read_only;
    SmithMcpToolFunction run;
} SmithMcpToolEntry;

#define SMITH_MCP_SESSION_PROPERTY \
    "\"session\":{\"type\":\"string\",\"description\":\"The session id (s1, s2...). Default: the latest.\"}"
#define SMITH_MCP_TIMEOUT_PROPERTY \
    "\"timeout_ms\":{\"type\":\"integer\",\"description\":\"How long to wait for the program to stop or end, in ms. " \
    "Default 30000.\"}"

static const SmithMcpToolEntry smith_mcp_tool_table[] = {
    {"launch",
     "Start a program under the debugger and run it to its first stop. Give `program` (with `args`), or `test`: the "
     "name of a "
     "test of the project, run in its test runner's own process (--in-process) so a breakpoint in the test is hit. "
     "With "
     "`breakpoints` the program runs to the first one; without, it stops at its entry point and you set breakpoints, "
     "then "
     "continue. Build the program with -g first (a smith project's debug build).",
     "{\"type\":\"object\",\"properties\":{"
     "\"program\":{\"type\":\"string\",\"description\":\"Path of the program to debug (built with -g).\"},"
     "\"args\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"The program's arguments.\"},"
     "\"test\":{\"type\":\"string\",\"description\":\"Name of a test of the project (smith_tests); instead of "
     "program.\"},"
     "\"runner\":{\"type\":\"string\",\"description\":\"The test runner, if it is not build/tests, build/debug/tests "
     "or build/sanitize/tests.\"},"
     "\"cwd\":{\"type\":\"string\",\"description\":\"The program's working folder, and the base of relative paths. "
     "Default: the server's.\"},"
     "\"breakpoints\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\"}"
     ",\"line\":{\"type\":\"integer\"},"
     "\"condition\":{\"type\":\"string\"}},\"required\":[\"file\",\"line\"]},\"description\":\"Breakpoints to set "
     "before the program runs.\"}," SMITH_MCP_TIMEOUT_PROPERTY "}}",
     false, smith_mcp_tool_launch},
    {"breakpoint",
     "Set a breakpoint at a file and line of the session's program, with an optional condition (a C expression), or "
     "remove it "
     "with remove: true. Set it on a line of a function's body, not its first line, so arguments are valid when it "
     "stops.",
     "{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\",\"description\":\"Source file (absolute, or "
     "relative to the session's folder).\"},"
     "\"line\":{\"type\":\"integer\"},\"condition\":{\"type\":\"string\",\"description\":\"Stop only when this is "
     "true.\"},"
     "\"remove\":{\"type\":\"boolean\",\"description\":\"Remove the breakpoint at "
     "file:line.\"}," SMITH_MCP_SESSION_PROPERTY "},"
     "\"required\":[\"file\",\"line\"]}",
     false, smith_mcp_tool_breakpoint},
    {"continue",
     "Resume the program and wait until it stops (a breakpoint, a signal) or ends. Also waits again for a program a "
     "previous call left running.",
     "{\"type\":\"object\",\"properties\":{" SMITH_MCP_SESSION_PROPERTY "," SMITH_MCP_TIMEOUT_PROPERTY "}}", false,
     smith_mcp_tool_continue},
    {"step_over", "Run to the next line of the current function, stepping over calls, and report where it stopped.",
     "{\"type\":\"object\",\"properties\":{" SMITH_MCP_SESSION_PROPERTY "," SMITH_MCP_TIMEOUT_PROPERTY "}}", false,
     smith_mcp_tool_step_over},
    {"step_in", "Run to the next line, stepping into a call, and report where it stopped.",
     "{\"type\":\"object\",\"properties\":{" SMITH_MCP_SESSION_PROPERTY "," SMITH_MCP_TIMEOUT_PROPERTY "}}", false,
     smith_mcp_tool_step_in},
    {"step_out", "Run until the current function returns, and report where it stopped.",
     "{\"type\":\"object\",\"properties\":{" SMITH_MCP_SESSION_PROPERTY "," SMITH_MCP_TIMEOUT_PROPERTY "}}", false,
     smith_mcp_tool_step_out},
    {"pause", "Stop a running program and report where it stopped.",
     "{\"type\":\"object\",\"properties\":{" SMITH_MCP_SESSION_PROPERTY "," SMITH_MCP_TIMEOUT_PROPERTY "}}", false,
     smith_mcp_tool_pause},
    {"stack", "The call stack of the stopped program: one line per frame, `#n function file:line`, #0 the innermost.",
     "{\"type\":\"object\",\"properties\":{\"levels\":{\"type\":\"integer\",\"description\":\"How many frames. Default "
     "20.\"}," SMITH_MCP_SESSION_PROPERTY "}}",
     true, smith_mcp_tool_stack},
    {"variables",
     "The variables of a frame of the stopped program, one per line as `name = value (type)`; what is inside a struct "
     "or "
     "array is indented under it, down to `depth` levels.",
     "{\"type\":\"object\",\"properties\":{\"frame\":{\"type\":\"integer\",\"description\":\"Frame number from stack. "
     "Default 0.\"},"
     "\"scope\":{\"type\":\"string\",\"description\":\"Locals (default), Globals or Registers.\"},"
     "\"depth\":{\"type\":\"integer\",\"description\":\"Levels of nesting to expand, 0 to 6. Default 1.\"},"
     "\"max\":{\"type\":\"integer\",\"description\":\"Most variables listed per level. Default "
     "50.\"}," SMITH_MCP_SESSION_PROPERTY "}}",
     true, smith_mcp_tool_variables},
    {"evaluate",
     "Evaluate a C expression in a frame of the stopped program (`p.x + 1`, `*node`, `sizeof(Point)`). Read-only by "
     "default: "
     "an assignment, ++, --, a call or a debugger command is refused unless allow_side_effects is true.",
     "{\"type\":\"object\",\"properties\":{\"expression\":{\"type\":\"string\"},"
     "\"frame\":{\"type\":\"integer\",\"description\":\"Frame number from stack. Default 0.\"},"
     "\"depth\":{\"type\":\"integer\",\"description\":\"Levels of a struct result to expand. Default 0.\"},"
     "\"allow_side_effects\":{\"type\":\"boolean\",\"description\":\"Allow assignments and calls. Default "
     "false.\"}," SMITH_MCP_SESSION_PROPERTY "},\"required\":[\"expression\"]}",
     true, smith_mcp_tool_evaluate},
    {"stop", "End the debug session: the program is killed and the debugger closed. Returns the program's last output.",
     "{\"type\":\"object\",\"properties\":{" SMITH_MCP_SESSION_PROPERTY "}}", false, smith_mcp_tool_stop},
};

constexpr size_t smith_mcp_tool_count = sizeof smith_mcp_tool_table / sizeof smith_mcp_tool_table[0];
