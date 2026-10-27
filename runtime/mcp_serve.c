// MCP over stdio: one JSON-RPC message a line, the version negotiation, tools/list and tools/call, and the loop that
// runs it all. Included by mcp.c.
//
// Two ways to start a connection are served, because clients differ. A client of the 2026-07-28 revision (Claude Code
// 2.1.304 asks first) sends `server/discover`, which the answer to says the revision is supported; every request then
// stands alone. An older client sends `initialize` with the revision it wants and gets that one back if it is one of
// ours, else the newest of ours before 2026-07-28, which is the spec's way of letting the client decide. The generated
// code has the 2026-07-28 messages; the `initialize` request and result are older and are not in its schema, so they
// are read by name and written as a result with the extra members `protocolVersion`, `capabilities` and `serverInfo`.

static const char *const smith_mcp_versions[] = {"2026-07-28", "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
static constexpr char smith_mcp_legacy_newest[] = "2025-11-25";
static constexpr char smith_mcp_name[] = "smith";
static constexpr char smith_mcp_version[] = "0.0";
static constexpr char smith_mcp_instructions[] =
    "A debugger for C, C++ and Objective-C programs (and .smith ones). launch a program, or a test of the project by "
    "name, with breakpoints; then continue, step_over, step_in or step_out; read the stack, variables and evaluate "
    "expressions while it is stopped; stop ends the session. The program must be built with -g. Output and exits come "
    "back "
    "with the next call.";

// The version to answer an `initialize` that asked for `asked`: that one when it is ours, else the newest legacy one.
static const char *smith_mcp_negotiate(struct smith_array_const_char_n asked) {
    for (size_t i = 0; i < sizeof smith_mcp_versions / sizeof smith_mcp_versions[0]; i++) { // bounded by the table
        if (smith_format_is(asked, smith_mcp_versions[i])) return smith_mcp_versions[i];
    }
    return smith_mcp_legacy_newest;
}

// --- writing messages -----------------------------------------------------------------------------------------------

constexpr size_t smith_mcp_message_max = (size_t)2 << 20;

static SmithJsonWriter smith_mcp_message_writer(SmithMcpServer *server) {
    char *buffer = smith_arena_push(&server->scratch, smith_mcp_message_max + 2, 1);
    SMITH_ASSERT(buffer != nullptr);
    return smith_json_writer_make(buffer, smith_mcp_message_max, 0);
}

// Sends the finished message and a newline. A message that did not fit or is not valid cannot be sent: it is reported
// on stderr (stdout carries only protocol) and the client times out, which only a bug of ours can cause.
static void smith_mcp_emit(SmithMcpServer *server, SmithJsonWriter *writer) {
    struct smith_array_const_char_n text;
    if (!smith_json_writer_finish(writer, &text)) {
        (void)smith_console_print_error(&server->io->console, "smith mcp: a message could not be written\n");
        return;
    }
    char *end = (char *)text.data + text.count[0];
    end[0] = '\n';
    end[1] = '\0';
    (void)smith_console_print(&server->io->console, text.data);
}

static void smith_mcp_send_error(SmithMcpServer *server, const SmithMcpRequestId *id, int64_t code,
                                 const char *message) {
    SmithMcpJSONRPCErrorResponse response = {.error = {.code = code, .message = smith_format_string(message)}};
    if (id != nullptr) {
        response.has_id = true;
        response.id = *id;
    }
    SmithJsonWriter writer = smith_mcp_message_writer(server);
    smith_mcp_jsonrpc_error_response_write(&writer, &response);
    smith_mcp_emit(server, &writer);
}

// Writes a value of the generated code to a tree, to put as a member of a result that has no type for it.
static const SmithJsonValue *smith_mcp_tree(SmithMcpServer *server, SmithJsonWriter *writer) {
    struct smith_array_const_char_n text;
    const SmithJsonValue *tree = nullptr;
    SmithJsonError error;
    SmithJsonLimits limits = {.depth = 32, .string_length = (size_t)1 << 16, .count = (size_t)1 << 16};
    bool ok =
        smith_json_writer_finish(writer, &text) && smith_json_parse(&server->scratch, text, limits, &tree, &error);
    SMITH_ASSERT(ok);
    return tree;
}

static void smith_mcp_send_initialize(SmithMcpServer *server, const SmithMcpRequestId *id, const char *version) {
    SmithMcpServerCapabilities capabilities = {.has_tools = true,
                                               .tools = {.has_listChanged = true, .listChanged = false}};
    SmithJsonWriter capabilities_writer = smith_mcp_message_writer(server);
    smith_mcp_server_capabilities_write(&capabilities_writer, &capabilities);
    SmithMcpImplementation info = {.name = smith_format_string(smith_mcp_name),
                                   .version = smith_format_string(smith_mcp_version)};
    SmithJsonWriter info_writer = smith_mcp_message_writer(server);
    smith_mcp_implementation_write(&info_writer, &info);
    struct smith_array_const_char_n keys[] = {smith_format_string("protocolVersion"), smith_format_string("capabilities"),
                                              smith_format_string("serverInfo"), smith_format_string("instructions")};
    SmithProtocolAny values[] = {smith_mcp_json_string(&server->scratch, smith_format_string(version)),
                                 smith_mcp_tree(server, &capabilities_writer), smith_mcp_tree(server, &info_writer),
                                 smith_mcp_json_string(&server->scratch, smith_format_string(smith_mcp_instructions))};
    SmithMcpJSONRPCResultResponse response = {
        .id = *id, .result = {.resultType = smith_format_string("complete"), .additionalProperties = {keys, values, 4}}};
    SmithJsonWriter writer = smith_mcp_message_writer(server);
    smith_mcp_jsonrpc_result_response_write(&writer, &response);
    smith_mcp_emit(server, &writer);
}

static void smith_mcp_send_ping(SmithMcpServer *server, const SmithMcpRequestId *id) {
    SmithMcpJSONRPCResultResponse response = {.id = *id, .result = {.resultType = smith_format_string("complete")}};
    SmithJsonWriter writer = smith_mcp_message_writer(server);
    smith_mcp_jsonrpc_result_response_write(&writer, &response);
    smith_mcp_emit(server, &writer);
}

static void smith_mcp_send_discover(SmithMcpServer *server, const SmithMcpRequestId *id) {
    struct smith_array_const_char_n versions[] = {smith_format_string("2026-07-28")};
    SmithMcpDiscoverResultResponse response = {
        .id = *id,
        .result = {.has__meta = true,
                   ._meta = {.has_io_modelcontextprotocol_serverInfo = true,
                             .io_modelcontextprotocol_serverInfo = {.name = smith_format_string(smith_mcp_name),
                                                                    .version = smith_format_string(smith_mcp_version)}},
                   .capabilities = {.has_tools = true, .tools = {.has_listChanged = true, .listChanged = false}},
                   .has_instructions = true,
                   .instructions = smith_format_string(smith_mcp_instructions),
                   .resultType = smith_format_string("complete"),
                   .supportedVersions = {versions, 1}}};
    SmithJsonWriter writer = smith_mcp_message_writer(server);
    smith_mcp_discover_result_response_write(&writer, &response);
    smith_mcp_emit(server, &writer);
}

// A tool's input schema is JSON Schema, a document of the tool's own: it is read as a tree and its members become the
// extra members of the generated `inputSchema`.
static void smith_mcp_tool_make(SmithMcpServer *server, const SmithMcpToolEntry *entry, SmithMcpTool *tool) {
    const SmithJsonValue *schema = nullptr;
    SmithJsonError error;
    SmithJsonLimits limits = {.depth = 16, .string_length = (size_t)1 << 14, .count = (size_t)1 << 12};
    bool ok = smith_json_parse(&server->scratch, smith_format_string(entry->schema), limits, &schema, &error);
    SMITH_ASSERT(ok && schema->kind == SmithJsonKind_Object);
    struct smith_array_const_char_n *keys =
        smith_arena_push_array(&server->scratch, sizeof(struct smith_array_const_char_n), schema->count,
                               alignof(struct smith_array_const_char_n));
    SmithProtocolAny *values = (SmithProtocolAny *)smith_arena_push_array(&server->scratch, sizeof(SmithProtocolAny),
                                                                          schema->count, alignof(SmithProtocolAny));
    SMITH_ASSERT(keys != nullptr && values != nullptr);
    size_t count = 0;
    for (size_t i = 0; i < schema->count; i++) {                    // bounded by the schema's members
        if (smith_format_is(schema->items[i]->key, "type")) continue; // the generated inputSchema writes "type": "object"
        keys[count] = schema->items[i]->key;
        values[count++] = schema->items[i];
    }
    *tool = (SmithMcpTool){.has_description = true,
                           .description = smith_format_string(entry->description),
                           .inputSchema = {.additionalProperties = {keys, values, count}},
                           .name = smith_format_string(entry->name)};
    if (entry->read_only) {
        tool->has_annotations = true;
        tool->annotations.has_readOnlyHint = true;
        tool->annotations.readOnlyHint = true;
    }
}

static void smith_mcp_send_tools(SmithMcpServer *server, const SmithMcpRequestId *id) {
    SmithMcpTool *tools =
        smith_arena_push_array(&server->scratch, sizeof(SmithMcpTool), smith_mcp_tool_count, alignof(SmithMcpTool));
    SMITH_ASSERT(tools != nullptr);
    for (size_t i = 0; i < smith_mcp_tool_count; i++)
        smith_mcp_tool_make(server, &smith_mcp_tool_table[i], &tools[i]);
    SmithMcpListToolsResultResponse response = {
        .id = *id, .result = {.resultType = smith_format_string("complete"), .tools = {tools, smith_mcp_tool_count}}};
    SmithJsonWriter writer = smith_mcp_message_writer(server);
    smith_mcp_list_tools_result_response_write(&writer, &response);
    smith_mcp_emit(server, &writer);
}

static void smith_mcp_send_tool_result(SmithMcpServer *server, const SmithMcpRequestId *id, const SmithMcpText *text,
                                       bool error) {
    SmithMcpContentBlock block = {.kind = SmithMcpContentBlockKind_TextContent,
                                  .as = {.text_content = {.text = {.data = text->data, .count = {text->size}}}}};
    SmithMcpCallToolResultResponse response = {
        .id = *id,
        .result = {.kind = SmithMcpCallToolResultResponseResultKind_CallToolResult,
                   .as = {.call_tool_result = {.content = {&block, 1},
                                               .has_isError = true,
                                               .isError = error,
                                               .resultType = smith_format_string("complete")}}}};
    SmithJsonWriter writer = smith_mcp_message_writer(server);
    smith_mcp_call_tool_result_response_write(&writer, &response);
    smith_mcp_emit(server, &writer);
}

// --- handling requests ----------------------------------------------------------------------------------------------

static void smith_mcp_call_tool(SmithMcpServer *server, const SmithMcpJSONRPCRequest *request) {
    const SmithJsonValue *name =
        smith_mcp_argument(request->has_params ? request->params : nullptr, "name", SmithJsonKind_String);
    const SmithJsonValue *arguments =
        request->has_params ? smith_mcp_argument(request->params, "arguments", SmithJsonKind_Object) : nullptr;
    if (name == nullptr) {
        smith_mcp_send_error(server, &request->id, -32602, "tools/call needs the name of a tool");
        return;
    }
    const SmithMcpToolEntry *entry = nullptr;
    for (size_t i = 0; i < smith_mcp_tool_count; i++) { // bounded by the table
        if (smith_format_is(name->text, smith_mcp_tool_table[i].name)) entry = &smith_mcp_tool_table[i];
    }
    if (entry == nullptr) {
        smith_mcp_send_error(server, &request->id, -32602, "unknown tool");
        return;
    }
    SmithMcpText text = smith_mcp_text_make(&server->scratch, smith_mcp_text_max);
    bool ok = entry->run(server, arguments, &text);
    if (text.truncated) smith_mcp_text_add(&text, "\n[result truncated]\n");
    if (text.size == 0) smith_mcp_text_add(&text, "ok\n");
    smith_mcp_send_tool_result(server, &request->id, &text, !ok);
}

static void smith_mcp_handle_request(SmithMcpServer *server, const SmithMcpJSONRPCRequest *request) {
    if (smith_format_is(request->method, "initialize")) {
        const SmithJsonValue *asked = smith_mcp_argument(request->has_params ? request->params : nullptr,
                                                         "protocolVersion", SmithJsonKind_String);
        smith_mcp_send_initialize(server, &request->id,
                                  asked != nullptr ? smith_mcp_negotiate(asked->text) : smith_mcp_legacy_newest);
    } else if (smith_format_is(request->method, "server/discover")) {
        smith_mcp_send_discover(server, &request->id);
    } else if (smith_format_is(request->method, "ping")) {
        smith_mcp_send_ping(server, &request->id);
    } else if (smith_format_is(request->method, "tools/list")) {
        smith_mcp_send_tools(server, &request->id);
    } else if (smith_format_is(request->method, "tools/call")) {
        smith_mcp_call_tool(server, request);
    } else {
        smith_mcp_send_error(server, &request->id, -32601, "method not found");
    }
}

// One line: a JSON-RPC message. Requests are answered; notifications and responses (to requests we never send) are not.
static void smith_mcp_handle_line(SmithMcpServer *server, struct smith_array_const_char_n line) {
    SmithArenaScope scope = smith_arena_scope_begin(&server->scratch);
    const SmithJsonValue *tree = nullptr;
    SmithJsonError json_error;
    SmithJsonLimits limits = {.depth = 64, .string_length = smith_mcp_line_max, .count = (size_t)1 << 20};
    SmithMcpJSONRPCMessage message;
    SmithProtocolError error;
    if (!smith_json_parse(&server->scratch, line, limits, &tree, &json_error)) {
        smith_mcp_send_error(server, nullptr, -32700, "parse error");
    } else if (!smith_mcp_jsonrpc_message_parse(&server->scratch, tree, &message, &error)) {
        smith_mcp_send_error(server, nullptr, -32600, "invalid request");
    } else if (message.kind == SmithMcpJSONRPCMessageKind_JSONRPCRequest) {
        smith_mcp_handle_request(server, &message.as.jsonrpc_request);
    }
    smith_arena_scope_end(scope);
}

// Takes the next whole line off the buffered input into server->request. False if there is none yet.
static bool smith_mcp_next_line(SmithMcpServer *server, struct smith_array_const_char_n *line) {
    const char *newline = memchr(server->line, '\n', server->line_size);
    if (newline == nullptr) return false;
    size_t length = (size_t)(newline - server->line);
    size_t kept = length < smith_mcp_line_max ? length : smith_mcp_line_max;
    memcpy(server->request, server->line, kept);
    if (kept > 0 && server->request[kept - 1] == '\r') kept--;
    server->request[kept] = '\0';
    memmove(server->line, server->line + length + 1, server->line_size - length - 1);
    server->line_size -= length + 1;
    *line = (struct smith_array_const_char_n){.data = server->request, .count = {kept}};
    return true;
}

// --- the server ------------------------------------------------------------------------------------------------

// The folder the server runs in, as the shell that started it names it: $PWD when that is the same folder as the
// working one, else the working folder. A compile writes the shell's spelling into its debug info (/var/folders/... for
// /private/var/folders/... on macOS), and the debugger matches a breakpoint's file against that spelling.
static void smith_mcp_folder(const SmithIo *io, char *out, size_t size) {
    const char *shell = smith_args_env(&io->args, "PWD");
    struct stat named;
    struct stat actual;
    if (shell != nullptr && shell[0] == '/' && strlen(shell) < size && stat(shell, &named) == 0 &&
        stat(".", &actual) == 0 && named.st_dev == actual.st_dev && named.st_ino == actual.st_ino) {
        snprintf(out, size, "%s", shell);
    } else if (getcwd(out, size) == nullptr) {
        snprintf(out, size, "/");
    }
}

// Serves MCP on the io's console until its input ends, then closes every session. dap_argv is the debug adapter's
// command (smith dap). The exit status is 0.
int smith_mcp_serve(SmithIo *io, const char *const *dap_argv) {
    SMITH_ASSERT(io != nullptr && dap_argv != nullptr && dap_argv[0] != nullptr);
    size_t remaining = io->arena.capacity - io->arena.used;
    size_t scratch = remaining / 4 < smith_mcp_scratch ? remaining / 4 : smith_mcp_scratch;
    SmithMcpServer server = {.io = io, .dap_argv = dap_argv};
    server.scratch = smith_arena_carve(&io->arena, scratch);
    server.input_arena = smith_arena_carve(&io->arena, (size_t)1 << 17);
    server.line = smith_arena_push(&io->arena, smith_mcp_line_max + ((size_t)1 << 16) + 1, 1);
    server.request = smith_arena_push(&io->arena, smith_mcp_line_max + 1, 1);
    server.sessions =
        smith_arena_push_array(&io->arena, sizeof(SmithMcpSession), smith_mcp_sessions_max, alignof(SmithMcpSession));
    SMITH_ASSERT(server.line != nullptr && server.request != nullptr && server.sessions != nullptr);
    for (size_t i = 0; i < smith_mcp_sessions_max; i++)
        server.sessions[i].used = false;
    smith_mcp_folder(io, server.cwd, sizeof server.cwd);
    smith_mcp_input_submit(&server);
    for (size_t turn = 0; turn < smith_mcp_turns_max; turn++) {
        struct smith_array_const_char_n line;
        while (smith_mcp_next_line(&server, &line)) { // bounded by the buffered input
            if (line.count[0] > 0) smith_mcp_handle_line(&server, line);
        }
        if (server.overlong_seen) {
            server.overlong_seen = false;
            smith_mcp_send_error(&server, nullptr, -32600, "message too large");
        }
        if (server.input_end) break; // every whole line is handled: a call may have read the end meanwhile
        smith_mcp_pump(&server, smith_io_forever);
    }
    for (size_t i = 0; i < smith_mcp_sessions_max; i++) { // bounded by the table
        if (server.sessions[i].used) smith_mcp_session_close(&server, &server.sessions[i], true);
    }
    return 0;
}

// `smith mcp`: the real io, and `smith dap` of this very smith as the adapter.
int smith_mcp_main(int argc, char **argv, const char *self) {
    SmithIo io = smith_io_make(argc, argv);
    const char *const dap_argv[] = {self, "dap", nullptr};
    return smith_mcp_serve(&io, dap_argv);
}
