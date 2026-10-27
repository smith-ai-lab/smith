// A debug session: the `smith dap` child, DAP's framing (Content-Length headers, then a JSON message), the requests
// sent and their replies, and the queue of events. Included by mcp.c.
//
// Everything that waits runs the server's one event loop (smith_mcp_pump) and looks at what it completed
// (smith_mcp_service): the server's own stdin, every session's reads, writes and exits. A reply is kept as the bytes it
// came in until the call that sent the request takes it, so the adapter may answer in any order, and so may a script
// of the fake io, which has all its output ready before the first request.

// --- the loop -------------------------------------------------------------------------------------------------

static void smith_mcp_input_submit(SmithMcpServer *server) {
    smith_arena_reset(&server->input_arena);
    smith_console_read_async(&server->io->console, &server->input_op, &server->input_arena, (size_t)1 << 16);
    server->input_pending = true;
}

// Adds what the console read to the buffered input. A message longer than the buffer is dropped up to its newline,
// and the client is told.
static void smith_mcp_input_take(SmithMcpServer *server) {
    server->input_pending = false;
    SmithOp *op = &server->input_op;
    if (op->error.code != 0 || op->end || op->data.count[0] == 0) {
        server->input_end = true;
        return;
    }
    const char *data = (const char *)op->data.data;
    size_t size = op->data.count[0];
    for (size_t turn = 0; turn < size + 1 && size > 0; turn++) { // each turn takes at least a byte
        if (server->overlong) {
            const char *newline = memchr(data, '\n', size);
            if (newline == nullptr) return;
            size -= (size_t)(newline - data) + 1;
            data = newline + 1;
            server->overlong = false;
            server->overlong_seen = true; // the answer is sent where the messages are handled
            continue;
        }
        size_t room = smith_mcp_line_max + ((size_t)1 << 16) - server->line_size;
        size_t take = size < room ? size : room;
        memcpy(server->line + server->line_size, data, take);
        server->line_size += take;
        data += take;
        size -= take;
        if (size > 0 || memchr(server->line, '\n', server->line_size) == nullptr) {
            if (server->line_size > smith_mcp_line_max) { // no newline in more than a message can hold
                server->line_size = 0;
                server->overlong = true;
            }
        }
    }
}

static void smith_mcp_session_service(SmithMcpServer *server, SmithMcpSession *session);

// Takes in what the loop completed: input, and each session's reads, writes and exit.
static void smith_mcp_service(SmithMcpServer *server) {
    if (server->input_pending && server->input_op.done) {
        smith_mcp_input_take(server);
        if (!server->input_end) smith_mcp_input_submit(server);
    }
    for (size_t i = 0; i < smith_mcp_sessions_max; i++) { // bounded by the table
        if (server->sessions[i].used) smith_mcp_session_service(server, &server->sessions[i]);
    }
}

static void smith_mcp_pump(SmithMcpServer *server, int64_t timeout_ns) {
    SMITH_ASSERT(timeout_ns > 0 || timeout_ns == smith_io_forever);
    (void)smith_io_run(server->io, timeout_ns);
    smith_mcp_service(server);
}

static uint64_t smith_mcp_deadline(const SmithMcpServer *server, int64_t timeout_ns) {
    SMITH_ASSERT(timeout_ns >= 0);
    return smith_clock_now(&server->io->clock) + (uint64_t)timeout_ns;
}

// Nanoseconds until the deadline; 0 once it has passed.
static int64_t smith_mcp_left(const SmithMcpServer *server, uint64_t deadline) {
    uint64_t now = smith_clock_now(&server->io->clock);
    return now >= deadline ? 0 : (int64_t)(deadline - now);
}

// --- events ---------------------------------------------------------------------------------------------------

static SmithMcpEvent *smith_mcp_event_at(SmithMcpSession *session, size_t index) {
    SMITH_ASSERT(index < session->event_count);
    return &session->events[(session->event_first + index) % smith_mcp_events_max];
}

static void smith_mcp_event_remove(SmithMcpSession *session, size_t index) {
    SMITH_ASSERT(index < session->event_count);
    for (size_t i = index; i + 1 < session->event_count; i++)
        *smith_mcp_event_at(session, i) = *smith_mcp_event_at(session, i + 1);
    session->event_count--;
}

// Queues an event. Output that follows output of the same kind joins it. When the queue is full the oldest output goes;
// if there is none the event is dropped, and the count says so.
static void smith_mcp_event_push(SmithMcpSession *session, const SmithMcpEvent *event) {
    if (event->kind == SmithMcpEvent_Output && session->event_count > 0) {
        SmithMcpEvent *last = smith_mcp_event_at(session, session->event_count - 1);
        if (last->kind == SmithMcpEvent_Output && strcmp(last->category, event->category) == 0 &&
            last->size + event->size < sizeof last->text) {
            memcpy(last->text + last->size, event->text, event->size);
            last->size += event->size;
            last->text[last->size] = '\0';
            return;
        }
    }
    if (session->event_count == smith_mcp_events_max) {
        size_t output = smith_mcp_events_max;
        for (size_t i = 0; i < session->event_count && output == smith_mcp_events_max; i++) { // bounded by the queue
            if (smith_mcp_event_at(session, i)->kind == SmithMcpEvent_Output) output = i;
        }
        session->events_dropped++;
        if (output == smith_mcp_events_max) return;
        smith_mcp_event_remove(session, output);
    }
    session->event_count++;
    *smith_mcp_event_at(session, session->event_count - 1) = *event;
}

static void smith_mcp_event_stopped(SmithMcpSession *session, SmithArena *arena, const SmithJsonValue *tree) {
    SmithDapStoppedEvent stopped;
    SmithProtocolError error;
    if (!smith_dap_stopped_event_parse(arena, tree, &stopped, &error)) return;
    SmithMcpEvent event = {.kind = SmithMcpEvent_Stopped};
    event.thread = stopped.body.has_threadId ? stopped.body.threadId : 0;
    if (stopped.body.has_hitBreakpointIds && stopped.body.hitBreakpointIds.count > 0)
        event.breakpoint = stopped.body.hitBreakpointIds.items[0];
    smith_mcp_copy(event.reason, sizeof event.reason, stopped.body.reason.text);
    if (stopped.body.has_description) smith_mcp_copy(event.text, sizeof event.text, stopped.body.description);
    smith_mcp_event_push(session, &event);
}

static void smith_mcp_event_output(SmithMcpSession *session, SmithArena *arena, const SmithJsonValue *tree) {
    SmithDapOutputEvent output;
    SmithProtocolError error;
    if (!smith_dap_output_event_parse(arena, tree, &output, &error)) return;
    struct smith_array_const_char_n category =
        output.body.has_category ? output.body.category.text : smith_format_string("console");
    // The program's own output and the adapter's important messages; the debugger's console talk is noise here.
    if (!smith_format_is(category, "stdout") && !smith_format_is(category, "stderr") &&
        !smith_format_is(category, "important"))
        return;
    SmithMcpEvent event = {.kind = SmithMcpEvent_Output};
    smith_mcp_copy(event.category, sizeof event.category, category);
    event.size =
        output.body.output.count[0] < sizeof event.text - 1 ? output.body.output.count[0] : sizeof event.text - 1;
    memcpy(event.text, output.body.output.data, event.size);
    event.text[event.size] = '\0';
    smith_mcp_event_push(session, &event);
}

static void smith_mcp_event_other(SmithMcpSession *session, SmithArena *arena, struct smith_array_const_char_n name,
                                  const SmithJsonValue *tree) {
    SmithProtocolError error;
    SmithMcpEvent event = {};
    if (smith_format_is(name, "exited")) {
        SmithDapExitedEvent exited;
        if (!smith_dap_exited_event_parse(arena, tree, &exited, &error)) return;
        event.kind = SmithMcpEvent_Exited;
        event.code = exited.body.exitCode;
    } else if (smith_format_is(name, "terminated")) {
        event.kind = SmithMcpEvent_Terminated;
    } else if (smith_format_is(name, "initialized")) {
        event.kind = SmithMcpEvent_Initialized;
    } else {
        return; // module, thread, breakpoint, continued, capabilities, process...: nothing an agent acts on
    }
    smith_mcp_event_push(session, &event);
}

// --- frames in -------------------------------------------------------------------------------------------------

static SmithMcpResponse *smith_mcp_response_find(SmithMcpSession *session, int64_t request_seq) {
    for (size_t i = 0; i < smith_mcp_responses_max; i++) { // bounded by the table
        if (session->responses[i].used && session->responses[i].request_seq == request_seq)
            return &session->responses[i];
    }
    return nullptr;
}

static void smith_mcp_response_store(SmithMcpSession *session, int64_t request_seq, const unsigned char *bytes,
                                     size_t size) {
    SmithMcpResponse *slot = smith_mcp_response_find(session, request_seq);
    for (size_t i = 0; i < smith_mcp_responses_max && slot == nullptr; i++) {
        if (!session->responses[i].used) slot = &session->responses[i];
    }
    if (slot == nullptr) slot = &session->responses[0]; // all full: the adapter answers what nobody asked
    slot->used = true;
    slot->request_seq = request_seq;
    slot->size = size;
    memcpy(slot->frame, bytes, size);
}

// One message of the adapter: a response is kept, an event queued, a request of the adapter's (runInTerminal...) is not
// answered, since the client does not offer what they ask for.
static void smith_mcp_dap_ingest(SmithMcpServer *server, SmithMcpSession *session, const unsigned char *bytes,
                                 size_t size) {
    SmithArenaScope scope = smith_arena_scope_begin(&server->scratch);
    SmithArena *arena = &server->scratch;
    char *copy = smith_arena_push(arena, size + 1, 1);
    SMITH_ASSERT(copy != nullptr);
    memcpy(copy, bytes, size);
    const SmithJsonValue *tree = nullptr;
    SmithJsonError json_error;
    SmithJsonLimits limits = {.depth = 64, .string_length = smith_mcp_frame_max, .count = (size_t)1 << 20};
    SmithDapProtocolMessage message;
    SmithProtocolError error;
    if (smith_json_parse(arena, (struct smith_array_const_char_n){.data = copy, .count = {size}}, limits, &tree,
                         &json_error) &&
        smith_dap_protocol_message_parse(arena, tree, &message, &error)) {
        if (message.type.known == SmithDapProtocolMessageTypeKnown_Response) {
            SmithDapResponse response;
            if (smith_dap_response_parse(arena, tree, &response, &error))
                smith_mcp_response_store(session, response.request_seq, bytes, size);
        } else if (message.type.known == SmithDapProtocolMessageTypeKnown_Event) {
            SmithDapEvent envelope;
            if (smith_dap_event_parse(arena, tree, &envelope, &error)) {
                if (smith_format_is(envelope.event, "stopped"))
                    smith_mcp_event_stopped(session, arena, tree);
                else if (smith_format_is(envelope.event, "output"))
                    smith_mcp_event_output(session, arena, tree);
                else
                    smith_mcp_event_other(session, arena, envelope.event, tree);
            }
        }
    }
    smith_arena_scope_end(scope);
}

static size_t smith_mcp_find_header_end(const unsigned char *bytes, size_t size) {
    for (size_t i = 0; i + 4 <= size; i++) { // bounded by the bytes
        if (bytes[i] == '\r' && bytes[i + 1] == '\n' && bytes[i + 2] == '\r' && bytes[i + 3] == '\n') return i;
    }
    return size;
}

// The Content-Length of a header, or SIZE_MAX if it has none.
static size_t smith_mcp_header_length(const unsigned char *header, size_t size) {
    static const char name[] = "Content-Length:";
    for (size_t i = 0; i + sizeof name <= size; i++) { // bounded by the header
        if (memcmp(header + i, name, sizeof name - 1) != 0) continue;
        struct smith_array_const_char_n digits = {.data = (const char *)header + i + sizeof name - 1,
                                                  .count = {size - i - (sizeof name - 1)}};
        while (digits.count[0] > 0 && digits.data[0] == ' ') {
            digits.data++;
            digits.count[0]--;
        }
        size_t used = 0;
        while (used < digits.count[0] && digits.data[used] >= '0' && digits.data[used] <= '9')
            used++;
        if (used == 0) return SIZE_MAX;
        smith_format_U64Result parsed =
            smith_format_parse_u64((struct smith_array_const_char_n){.data = digits.data, .count = {used}});
        if (parsed.tag != smith_enum_result_u64__SmithFormatError_tag_ok) return SIZE_MAX;
        uint64_t value = parsed.ok;
        return value < SIZE_MAX ? (size_t)value : SIZE_MAX;
    }
    return SIZE_MAX;
}

static void smith_mcp_input_drop(SmithMcpSession *session, size_t size) {
    SMITH_ASSERT(size <= session->input_size);
    memmove(session->input, session->input + size, session->input_size - size);
    session->input_size -= size;
}

// Takes every whole message off the bytes read. A message over smith_mcp_frame_max is skipped as it arrives.
static void smith_mcp_dap_frames(SmithMcpServer *server, SmithMcpSession *session) {
    for (size_t turn = 0; turn < smith_mcp_turns_max; turn++) {
        if (session->skip > 0) {
            size_t drop = session->skip < session->input_size ? session->skip : session->input_size;
            smith_mcp_input_drop(session, drop);
            session->skip -= drop;
            if (session->skip > 0) return;
            continue;
        }
        size_t end = smith_mcp_find_header_end(session->input, session->input_size);
        if (end == session->input_size) {
            if (session->input_size > 1024)
                session->input_size = 0; // no header that long: noise on the adapter's stdout
            return;
        }
        size_t length = smith_mcp_header_length(session->input, end);
        if (length == SIZE_MAX) {
            smith_mcp_input_drop(session, end + 4);
            continue;
        }
        if (length > smith_mcp_frame_max) {
            smith_mcp_input_drop(session, end + 4);
            session->skip = length;
            continue;
        }
        if (session->input_size < end + 4 + length) return;
        smith_mcp_dap_ingest(server, session, session->input + end + 4, length);
        smith_mcp_input_drop(session, end + 4 + length);
    }
}

static void smith_mcp_session_read_submit(SmithMcpSession *session) {
    smith_child_read_async(
        &session->child, &session->read,
        (struct smith_array_u8_n){.data = (uint8_t *)(session->chunk), .count = {sizeof session->chunk}});
    session->read_pending = true;
}

static void smith_mcp_session_service(SmithMcpServer *server, SmithMcpSession *session) {
    if (session->read_pending && session->read.done) {
        session->read_pending = false;
        if (session->read.error.code != 0 || session->read.end) {
            session->output_ended = true;
        } else if (session->input_size + session->read.data.count[0] > sizeof session->input) {
            session->input_size = 0; // cannot happen while whole messages are taken off at once; never grow past it
            smith_mcp_session_read_submit(session);
        } else {
            memcpy(session->input + session->input_size, session->read.data.data, session->read.data.count[0]);
            session->input_size += session->read.data.count[0];
            smith_mcp_dap_frames(server, session);
            smith_mcp_session_read_submit(session);
        }
    }
    if (session->write_pending && session->write.done) {
        session->write_pending = false;
        if (session->write.error.code != 0) session->input_closed = true; // EPIPE: the adapter is gone
    }
    if (session->wait_pending && session->wait.done) {
        session->wait_pending = false;
        session->process_gone = true;
    }
}

// The adapter will say nothing more: its stdout is closed and everything it wrote has been taken in.
static bool smith_mcp_session_dead(const SmithMcpSession *session) {
    return session->output_ended;
}

// --- requests out and their replies ---------------------------------------------------------------------------------

static int64_t smith_mcp_dap_seq(SmithMcpSession *session) {
    return session->next_seq++;
}

static SmithJsonWriter smith_mcp_dap_writer(SmithMcpSession *session) {
    return smith_json_writer_make((char *)session->frame_out + 64, sizeof session->frame_out - 64, 0);
}

// Writes the message with its header and waits until the adapter has taken it (or is gone).
static bool smith_mcp_dap_transmit(SmithMcpServer *server, SmithMcpSession *session, SmithJsonWriter *writer) {
    struct smith_array_const_char_n body;
    if (!smith_json_writer_finish(writer, &body)) return false;
    char header[48];
    int length = snprintf(header, sizeof header, "Content-Length: %zu\r\n\r\n", body.count[0]);
    SMITH_ASSERT(length > 0 && (size_t)length < sizeof header && (size_t)length <= 64);
    unsigned char *start = session->frame_out + 64 - (size_t)length;
    memcpy(start, header, (size_t)length);
    if (session->input_closed) return false;
    smith_child_write_async(
        &session->child, &session->write,
        (struct smith_array_const_u8_n){.data = (const uint8_t *)(start), .count = {(size_t)length + body.count[0]}});
    session->write_pending = true;
    uint64_t deadline = smith_mcp_deadline(server, smith_mcp_request_timeout);
    for (size_t turn = 0; turn < smith_mcp_turns_max && session->write_pending; turn++) {
        int64_t left = smith_mcp_left(server, deadline);
        if (left <= 0 || smith_mcp_session_dead(session)) return false;
        smith_mcp_pump(server, left);
    }
    return !session->input_closed;
}

typedef enum {
    SmithMcpReply_Ok,
    SmithMcpReply_Failed,  // the adapter answered success: false; message says why
    SmithMcpReply_Timeout, // no answer in time
    SmithMcpReply_Dead,    // the adapter is gone, or would not take the request
    SmithMcpReply_Bad,     // an answer that is not a DAP response
} SmithMcpReplyStatus;

typedef struct {
    SmithMcpReplyStatus status;
    SmithDapResponse response; // in the server's scratch arena: valid until the caller's scope ends
    char message[256];
} SmithMcpReply;

// Takes the response to the request with that seq, waiting for it up to the timeout.
static SmithMcpReply smith_mcp_dap_await(SmithMcpServer *server, SmithMcpSession *session, int64_t seq,
                                         int64_t timeout_ns) {
    SmithMcpReply reply = {};
    uint64_t deadline = smith_mcp_deadline(server, timeout_ns);
    SmithMcpResponse *found = smith_mcp_response_find(session, seq);
    for (size_t turn = 0; turn < smith_mcp_turns_max && found == nullptr; turn++) {
        int64_t left = smith_mcp_left(server, deadline);
        if (smith_mcp_session_dead(session)) {
            reply.status = SmithMcpReply_Dead;
            return reply;
        }
        if (left <= 0) {
            reply.status = SmithMcpReply_Timeout;
            return reply;
        }
        smith_mcp_pump(server, left);
        found = smith_mcp_response_find(session, seq);
    }
    char *copy = smith_arena_push(&server->scratch, found->size + 1, 1);
    SMITH_ASSERT(copy != nullptr);
    memcpy(copy, found->frame, found->size);
    struct smith_array_const_char_n text = {.data = copy, .count = {found->size}};
    found->used = false;
    const SmithJsonValue *tree = nullptr;
    SmithJsonError json_error;
    SmithProtocolError error;
    SmithJsonLimits limits = {.depth = 64, .string_length = smith_mcp_frame_max, .count = (size_t)1 << 20};
    reply.status = SmithMcpReply_Bad;
    if (!smith_json_parse(&server->scratch, text, limits, &tree, &json_error) ||
        !smith_dap_response_parse(&server->scratch, tree, &reply.response, &error))
        return reply;
    reply.status = reply.response.success ? SmithMcpReply_Ok : SmithMcpReply_Failed;
    if (reply.response.has_message) smith_mcp_copy(reply.message, sizeof reply.message, reply.response.message.text);
    return reply;
}

static SmithMcpReply smith_mcp_reply_dead(void) {
    return (SmithMcpReply){.status = SmithMcpReply_Dead};
}

// Sends a typed request: the macro stamps its seq and calls the type's own write. A function cannot, since the
// generated writers take their own types.
#define SMITH_MCP_DAP_SEND(server, session, name, request, sent) \
    do { \
        (request).seq = smith_mcp_dap_seq(session); \
        SmithJsonWriter writer_ = smith_mcp_dap_writer(session); \
        smith_dap_##name##_write(&writer_, &(request)); \
        (sent) = smith_mcp_dap_transmit((server), (session), &writer_); \
    } while (false)

// Sends a request and takes its reply.
#define SMITH_MCP_DAP_CALL(server, session, name, request, timeout_ns, reply) \
    do { \
        bool sent_ = false; \
        SMITH_MCP_DAP_SEND(server, session, name, request, sent_); \
        (reply) = \
            sent_ ? smith_mcp_dap_await((server), (session), (request).seq, (timeout_ns)) : smith_mcp_reply_dead(); \
    } while (false)

// Why a request did not succeed, as one line.
static void smith_mcp_reply_error(const SmithMcpReply *reply, const char *what, SmithMcpText *out) {
    switch (reply->status) {
    case SmithMcpReply_Ok:
        return;
    case SmithMcpReply_Failed:
        smith_mcp_text_add(out, "%s failed: %s\n", what,
                           reply->message[0] != '\0' ? reply->message : "(the adapter gave no reason)");
        return;
    case SmithMcpReply_Timeout:
        smith_mcp_text_add(out, "%s: the debug adapter did not answer in time\n", what);
        return;
    case SmithMcpReply_Dead:
        smith_mcp_text_add(out, "%s: the debug adapter has exited; stop this session and launch again\n", what);
        return;
    case SmithMcpReply_Bad:
        smith_mcp_text_add(out, "%s: the debug adapter's answer could not be read\n", what);
        return;
    }
}

// --- taking events -------------------------------------------------------------------------------------------------

typedef enum {
    SmithMcpTake_Nothing,
    SmithMcpTake_Stopped,  // the program stopped: stop says where
    SmithMcpTake_Finished, // the program exited or the adapter ended
} SmithMcpTake;

static void smith_mcp_note_output(SmithMcpText *notes, const SmithMcpEvent *event) {
    if (notes == nullptr) return;
    const char *tag = "[stdout]";
    if (strcmp(event->category, "important") == 0)
        tag = "[adapter]";
    else if (strcmp(event->category, "stderr") == 0)
        tag = "[stderr]";
    if (notes->tag != tag) { // the tags are the literals above, so the pointers compare
        smith_mcp_text_add(notes, "%s\n", tag);
        notes->tag = tag;
    }
    for (size_t i = 0; i < event->size; i++) { // the terminal's line ends are \r\n
        if (event->text[i] != '\r') smith_mcp_text_bytes(notes, event->text + i, 1);
    }
    if (event->size == 0 || event->text[event->size - 1] != '\n') smith_mcp_text_add(notes, "\n");
}

// Takes events off the front of the queue, in order, up to and including the first that changes whether the program
// runs. Output goes into the notes. While the program is stopped a stop, exit or termination further on in the queue is
// not taken: it cannot have happened yet, and a script that has it ready is waiting for the call that resumes.
static SmithMcpTake smith_mcp_events_take(SmithMcpSession *session, SmithMcpText *notes, SmithMcpEvent *stop) {
    for (size_t turn = 0; turn < smith_mcp_events_max * 2 && session->event_count > 0; turn++) {
        SmithMcpEvent *event = smith_mcp_event_at(session, 0);
        switch (event->kind) {
        case SmithMcpEvent_Output:
            smith_mcp_note_output(notes, event);
            break;
        case SmithMcpEvent_Initialized:
            break;
        case SmithMcpEvent_Stopped:
            if (session->stopped) return SmithMcpTake_Nothing;
            session->stopped = true;
            session->thread = event->thread;
            *stop = *event;
            smith_mcp_event_remove(session, 0);
            return SmithMcpTake_Stopped;
        case SmithMcpEvent_Exited:
        case SmithMcpEvent_Terminated:
            if (session->stopped) return SmithMcpTake_Nothing;
            bool exited = event->kind == SmithMcpEvent_Exited;
            if (exited) {
                session->has_exit_code = true;
                session->exit_code = event->code;
            }
            bool news = !session->finished;
            session->finished = true;
            smith_mcp_event_remove(session, 0);
            if (news) return SmithMcpTake_Finished;
            continue;
        }
        smith_mcp_event_remove(session, 0);
    }
    return SmithMcpTake_Nothing;
}

// Waits for the program to stop or finish, taking the events in order. Nothing means the time ran out with the
// program still running.
static SmithMcpTake smith_mcp_wait_stop(SmithMcpServer *server, SmithMcpSession *session, int64_t timeout_ns,
                                        SmithMcpText *notes, SmithMcpEvent *stop) {
    uint64_t deadline = smith_mcp_deadline(server, timeout_ns);
    for (size_t turn = 0; turn < smith_mcp_turns_max; turn++) {
        SmithMcpTake taken = smith_mcp_events_take(session, notes, stop);
        if (taken != SmithMcpTake_Nothing) return taken;
        if (smith_mcp_session_dead(session)) {
            session->finished = true;
            session->stopped = false;
            return SmithMcpTake_Finished;
        }
        int64_t left = smith_mcp_left(server, deadline);
        if (left <= 0) return SmithMcpTake_Nothing;
        smith_mcp_pump(server, left);
    }
    return SmithMcpTake_Nothing;
}

// Waits for the adapter's `initialized` event, which says it takes the configuration (breakpoints) now; the launch
// request's own reply, if it comes first, is a refusal.
static bool smith_mcp_wait_initialized(SmithMcpServer *server, SmithMcpSession *session, int64_t launch_seq,
                                       int64_t timeout_ns) {
    uint64_t deadline = smith_mcp_deadline(server, timeout_ns);
    for (size_t turn = 0; turn < smith_mcp_turns_max; turn++) {
        for (size_t i = 0; i < session->event_count; i++) { // bounded by the queue
            if (smith_mcp_event_at(session, i)->kind != SmithMcpEvent_Initialized) continue;
            smith_mcp_event_remove(session, i);
            return true;
        }
        if (smith_mcp_response_find(session, launch_seq) != nullptr || smith_mcp_session_dead(session)) return false;
        int64_t left = smith_mcp_left(server, deadline);
        if (left <= 0) return false;
        smith_mcp_pump(server, left);
    }
    return false;
}

// --- sessions -------------------------------------------------------------------------------------------------

static SmithMcpSession *smith_mcp_session_find(SmithMcpServer *server, const char *id) {
    for (size_t i = 0; i < smith_mcp_sessions_max; i++) { // bounded by the table
        if (server->sessions[i].used && strcmp(server->sessions[i].id, id) == 0) return &server->sessions[i];
    }
    return nullptr;
}

// The session a call without an id means: the one started last that is still there.
static SmithMcpSession *smith_mcp_session_latest(SmithMcpServer *server) {
    SmithMcpSession *latest = nullptr;
    for (size_t i = 0; i < smith_mcp_sessions_max; i++) { // bounded by the table
        SmithMcpSession *session = &server->sessions[i];
        if (session->used && (latest == nullptr || session->serial > latest->serial)) latest = session;
    }
    return latest;
}

// Starts `smith dap` in a free slot. nullptr, with the reason in out, if there is none or it will not start.
static SmithMcpSession *smith_mcp_session_start(SmithMcpServer *server, SmithMcpText *out) {
    SmithMcpSession *session = nullptr;
    for (size_t i = 0; i < smith_mcp_sessions_max && session == nullptr; i++) {
        if (!server->sessions[i].used) session = &server->sessions[i];
    }
    if (session == nullptr) {
        smith_mcp_text_add(out, "too many debug sessions (%zu); stop one first\n", smith_mcp_sessions_max);
        return nullptr;
    }
    memset(session, 0, sizeof *session);
    SmithError error = smith_processes_start_piped(&server->io->processes, server->dap_argv, &session->child);
    if (error.code != 0) {
        smith_mcp_text_add(out, "cannot start the debug adapter (%s): %s\n", server->dap_argv[0], error.message);
        return nullptr;
    }
    server->sessions_made++;
    session->used = true;
    session->serial = server->sessions_made;
    snprintf(session->id, sizeof session->id, "s%zu", server->sessions_made);
    session->next_seq = 1;
    snprintf(session->cwd, sizeof session->cwd, "%s", server->cwd);
    smith_mcp_session_read_submit(session);
    smith_processes_wait_async(&server->io->processes, &session->wait, session->child.process);
    session->wait_pending = true;
    return session;
}

// Waits until the adapter has exited and its output is read to the end, signalling it if it is slow: first a
// moment, then SIGTERM, then SIGKILL to the whole group.
static void smith_mcp_session_reap(SmithMcpServer *server, SmithMcpSession *session) {
    static const int signals[] = {0, SIGTERM, SIGKILL};
    static const int64_t waits[] = {2 * smith_mcp_second, smith_mcp_second, 2 * smith_mcp_second};
    for (size_t step = 0; step < sizeof signals / sizeof signals[0]; step++) {
        if (signals[step] != 0) (void)smith_child_kill(&session->child, signals[step]);
        uint64_t deadline = smith_mcp_deadline(server, waits[step]);
        for (size_t turn = 0; turn < smith_mcp_turns_max; turn++) {
            if (!session->wait_pending && !session->read_pending && !session->write_pending) return;
            int64_t left = smith_mcp_left(server, deadline);
            if (left <= 0) break;
            smith_mcp_pump(server, left);
        }
    }
}

// Ends the session. A polite one asks the adapter to disconnect and kill the program first.
static void smith_mcp_session_close(SmithMcpServer *server, SmithMcpSession *session, bool polite) {
    if (polite && !smith_mcp_session_dead(session) && !session->input_closed) {
        SmithArenaScope scope = smith_arena_scope_begin(&server->scratch);
        SmithDapDisconnectRequest request = {.has_arguments = true,
                                             .arguments = {.has_terminateDebuggee = true, .terminateDebuggee = true}};
        SmithMcpReply reply;
        SMITH_MCP_DAP_CALL(server, session, disconnect_request, request, 3 * smith_mcp_second, reply);
        smith_arena_scope_end(scope);
    }
    if (!session->input_closed) {
        smith_child_close_input(&session->child);
        session->input_closed = true;
    }
    smith_mcp_session_reap(server, session);
    smith_child_close(&session->child);
    session->used = false;
}
