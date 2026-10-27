// Tests of `smith mcp` on the fake io (issue #22): a scripted client on the console and a scripted `smith dap` child,
// so that the whole server runs in one process, with no debugger, no clock and no network. The server is runtime/mcp.c
// included here as smith.c includes it, with the libraries and the protocol code it stands on (the text module is
// linked, as in smith: tests/mcp_test.smith compiles lib/smith/format.smith with this file, and the other libraries
// too); tests/mcp_test.smith builds this file under the sanitizers and runs it. The real thing, a real adapter and a
// real program, is tests/mcp_test.smith's other half.
//
// A case is the lines the client sends (`input`), the frames the adapter writes (`dap`, with the requests the server
// sends numbered from 1, so a frame answers the request of that number), and files. The server's output is one JSON
// message a line; `reply` finds the one answering an id and gives the text of its result. In JSON written here an
// apostrophe stands for a double quote.
#include <smith/arena.h>
#include <smith/contract.h>
#include <smith/io.h>
#include <smith/random.h>
#include "../lib/json.c"
#include "../lib/trace.c"
#include "../runtime/protocol/protocol.c"
#include "dap.c"
#include "mcp.c"

// The server's own checks stay on in every build, as smith's do (assert would go with NDEBUG).
#define SMITH_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #condition); \
            abort(); \
        } \
    } while (false)

#include "../runtime/mcp.c"

#include <sys/mman.h>

constexpr size_t test_memory = (size_t)1 << 30;
constexpr size_t test_buffer = (size_t)1 << 20;

static int test_total;
static int test_failures;
static const char *test_name;

static void test_report(bool ok, const char *what, const char *expected, const char *actual) {
    test_total++;
    if (ok) return;
    test_failures++;
    printf("FAIL  %s: %s\n  expected: %s\n  actual:   %s\n", test_name, what, expected, actual);
}

static void test_equal(const char *what, const char *expected, const char *actual) {
    test_report(strcmp(expected, actual) == 0, what, expected, actual);
}

static void test_has(const char *what, const char *actual, const char *needle) {
    test_report(strstr(actual, needle) != nullptr, what, needle, actual);
}

static void test_lacks(const char *what, const char *actual, const char *needle) {
    test_report(strstr(actual, needle) == nullptr, what, needle, actual);
}

// --- building a case -----------------------------------------------------------------------------------------------

typedef struct {
    char *data;
    size_t size;
} TestBuffer;

static TestBuffer test_buffer_make(void) {
    void *memory = mmap(nullptr, test_buffer, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    SMITH_ASSERT(memory != MAP_FAILED);
    return (TestBuffer){.data = memory};
}

static void test_buffer_free(TestBuffer *buffer) {
    munmap(buffer->data, test_buffer);
    *buffer = (TestBuffer){};
}

static void test_add(TestBuffer *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void test_add(TestBuffer *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(buffer->data + buffer->size, test_buffer - buffer->size, format, arguments);
    va_end(arguments);
    SMITH_ASSERT(written >= 0 && buffer->size + (size_t)written < test_buffer);
    for (int i = 0; i < written; i++) { // an apostrophe stands for a double quote
        if (buffer->data[buffer->size + (size_t)i] == '\'') buffer->data[buffer->size + (size_t)i] = '"';
    }
    buffer->size += (size_t)written;
}

// One frame of the adapter: the header, then the JSON.
static void test_frame(TestBuffer *dap, const char *body) {
    char text[16384];
    size_t length = strlen(body);
    SMITH_ASSERT(length < sizeof text);
    for (size_t i = 0; i <= length; i++)
        text[i] = body[i] == '\'' ? '"' : body[i];
    test_add(dap, "Content-Length: %zu\r\n\r\n%s", length, text);
}

static int test_dap_seq;

static void test_response(TestBuffer *dap, int request, const char *command, const char *body) {
    char json[8192];
    snprintf(json, sizeof json, "{'seq':%d,'type':'response','request_seq':%d,'success':true,'command':'%s'%s%s}",
             ++test_dap_seq, request, command, body[0] != '\0' ? ",'body':" : "", body);
    test_frame(dap, json);
}

static void test_failure(TestBuffer *dap, int request, const char *command, const char *message) {
    char json[1024];
    snprintf(json, sizeof json,
             "{'seq':%d,'type':'response','request_seq':%d,'success':false,'command':'%s','message':'%s'}",
             ++test_dap_seq, request, command, message);
    test_frame(dap, json);
}

static void test_event(TestBuffer *dap, const char *event, const char *body) {
    char json[4096];
    snprintf(json, sizeof json, "{'seq':%d,'type':'event','event':'%s'%s%s}", ++test_dap_seq, event,
             body[0] != '\0' ? ",'body':" : "", body);
    test_frame(dap, json);
}

static int test_id;

// A line of the client: tools/call, with the arguments as JSON.
static int test_call(TestBuffer *input, const char *tool, const char *arguments) {
    test_add(input, "{'jsonrpc':'2.0','id':%d,'method':'tools/call','params':{'name':'%s','arguments':%s}}\n",
             ++test_id, tool, arguments);
    return test_id;
}

static int test_request(TestBuffer *input, const char *method, const char *params) {
    test_add(input, "{'jsonrpc':'2.0','id':%d,'method':'%s'%s%s}\n", ++test_id, method,
             params[0] != '\0' ? ",'params':" : "", params);
    return test_id;
}

// --- running a case ---------------------------------------------------------------------------------------------

typedef struct {
    const char *name;
    TestBuffer input;
    TestBuffer dap;
    bool has_adapter;
    SmithChildExit exit_on;
    size_t chunk;
} TestCase;

static TestBuffer test_output; // what the server wrote
static TestBuffer test_sent;   // what it sent the adapter
static SmithArena test_view;   // the arena that reads the output

static TestCase test_begin(const char *name, bool has_adapter) {
    test_name = name;
    test_id = 0;
    test_dap_seq = 0;
    return (TestCase){.name = name,
                      .input = test_buffer_make(),
                      .dap = test_buffer_make(),
                      .has_adapter = has_adapter,
                      .exit_on = SmithChildExit_InputClosed,
                      .chunk = 200};
}

static constexpr char test_source[] = "#include <stdio.h>\n"                  // 1
                                      "\n"                                    // 2
                                      "typedef struct { int x, y; } Point;\n" // 3
                                      "\n"                                    // 4
                                      "static int add(Point p) {\n"           // 5
                                      "    int unused = 0;\n"                 // 6
                                      "    (void)unused;\n"                   // 7
                                      "    int total = 0;\n"                  // 8
                                      "    for (int i = 0; i < 1; i++)\n"     // 9
                                      "    int sum = p.x + p.y;\n"            // 10
                                      "    total += sum;\n"                   // 11
                                      "    return total;\n"                   // 12
                                      "}\n";

// Runs the server on the case; the transcript is test_output, and the requests it sent the adapter test_sent. The
// alarm (a wall backstop, large: a blocked test spends no CPU time) ends a test that hangs, which a wrong script can
// make the server do (it waits for a reply that never comes).
static void test_run(TestCase *test) {
    alarm(600);
    void *memory = mmap(nullptr, test_memory, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    SMITH_ASSERT(memory != MAP_FAILED);
    SmithIo io =
        smith_io_make_test((SmithIoTest){.memory = memory, .capacity = test_memory, .input = test->input.data});
    SmithError error = smith_io_test_add_file(&io, "/p/prog.c", test_source);
    SMITH_ASSERT(error.code == 0);
    static const char *const programs[] = {"/p/prog", "/p/build/tests"}; // programs to launch: empty files
    for (size_t i = 0; i < 2; i++) {
        error = smith_io_test_add_file(&io, programs[i], "x");
        SMITH_ASSERT(error.code == 0);
    }
    if (test->has_adapter) {
        error = smith_io_test_add_child(
            &io, (SmithChildScript){
                     .program = "smith", .output = test->dap.data, .chunk = test->chunk, .exit_on = test->exit_on});
        SMITH_ASSERT(error.code == 0);
    }
    const char *const dap_argv[] = {"smith", "dap", nullptr};
    int status = smith_mcp_serve(&io, dap_argv);
    test_equal("the server's exit status", "0", status == 0 ? "0" : "not 0");
    const char *output = smith_io_test_output(&io);
    SMITH_ASSERT(strlen(output) < test_buffer);
    memcpy(test_output.data, output, strlen(output) + 1);
    const char *sent = smith_io_test_child_input(&io, 0);
    snprintf(test_sent.data, test_buffer, "%s", sent != nullptr ? sent : "");
    const char *errors = smith_io_test_errors(&io);
    test_equal("nothing on stderr", "", errors);
    munmap(memory, test_memory);
    test_buffer_free(&test->input);
    test_buffer_free(&test->dap);
    alarm(0);
}

// The text of the tool result that answers the id, or "error <code>: <message>" for a JSON-RPC error. is_error is the
// result's isError. "no reply" if the server wrote none.
static char test_text[(size_t)128 << 10];

static const char *test_reply(int id, bool *is_error) {
    smith_arena_reset(&test_view);
    snprintf(test_text, sizeof test_text, "no reply to %d", id);
    if (is_error != nullptr) *is_error = false;
    const char *line = test_output.data;
    for (size_t turn = 0; turn < 100000 && *line != '\0'; turn++) { // bounded by the output's lines
        const char *newline = strchr(line, '\n');
        size_t length = newline != nullptr ? (size_t)(newline - line) : strlen(line);
        const SmithJsonValue *tree = nullptr;
        SmithJsonError json_error;
        SmithJsonLimits limits = {.depth = 32, .string_length = (size_t)1 << 20, .count = (size_t)1 << 16};
        bool parsed = smith_json_parse(&test_view, (struct smith_array_const_char_n){.data = line, .count = {length}},
                                       limits, &tree, &json_error);
        SMITH_ASSERT(parsed);
        int64_t found = -1;
        const SmithJsonValue *number = smith_json_member(tree, "id");
        if (number != nullptr && number->kind == SmithJsonKind_Number) {
            smith_format_I64Result read = smith_format_parse_i64(number->text);
            if (read.tag == smith_enum_result_i64__SmithFormatError_tag_ok) found = read.ok;
        }
        if (found == id) {
            const SmithJsonValue *result = smith_json_member(tree, "result");
            const SmithJsonValue *failure = smith_json_member(tree, "error");
            if (failure != nullptr) {
                int64_t code = 0;
                smith_format_I64Result read = smith_format_parse_i64(smith_json_member(failure, "code")->text);
                if (read.tag == smith_enum_result_i64__SmithFormatError_tag_ok) code = read.ok;
                snprintf(test_text, sizeof test_text, "error %lld: %.*s", (long long)code,
                         (int)smith_json_member(failure, "message")->text.count[0],
                         smith_json_member(failure, "message")->text.data);
            } else if (result != nullptr && smith_json_member(result, "content") != nullptr) {
                const SmithJsonValue *block = smith_json_member(result, "content")->items[0];
                const SmithJsonValue *text = smith_json_member(block, "text");
                snprintf(test_text, sizeof test_text, "%.*s", (int)text->text.count[0], text->text.data);
                if (is_error != nullptr) *is_error = smith_json_member(result, "isError")->kind == SmithJsonKind_True;
            } else if (result != nullptr) {
                snprintf(test_text, sizeof test_text, "%.*s", (int)length, line);
            }
            return test_text;
        }
        if (newline == nullptr) break;
        line = newline + 1;
    }
    return test_text;
}

static const char *test_ok(int id) {
    bool is_error = false;
    const char *text = test_reply(id, &is_error);
    test_report(!is_error, "the call succeeded", "isError false", text);
    return text;
}

static const char *test_failed(int id) {
    bool is_error = false;
    const char *text = test_reply(id, &is_error);
    test_report(is_error, "the call failed", "isError true", text);
    return text;
}

// --- the connection ---------------------------------------------------------------------------------------------

static void test_initialize(void) {
    TestCase test = test_begin("initialize", false);
    static const char *const asked[] = {"2025-11-25", "2024-11-05", "1999-01-01", "2026-07-28", "2025-06-18"};
    static const char *const given[] = {"2025-11-25", "2024-11-05", "2025-11-25", "2026-07-28", "2025-06-18"};
    int ids[5];
    for (size_t i = 0; i < 5; i++) {
        char params[256];
        snprintf(params, sizeof params,
                 "{'protocolVersion':'%s','capabilities':{},'clientInfo':{'name':'test','version':'1'}}", asked[i]);
        ids[i] = test_request(&test.input, "initialize", params);
    }
    test_add(&test.input, "{'jsonrpc':'2.0','method':'notifications/initialized'}\n");
    int discover = test_request(&test.input, "server/discover", "");
    int ping = test_request(&test.input, "ping", "");
    int unknown = test_request(&test.input, "resources/list", "");
    test_run(&test);
    for (size_t i = 0; i < 5; i++) {
        char needle[64];
        snprintf(needle, sizeof needle, "\"protocolVersion\":\"%s\"", given[i]);
        test_has(asked[i], test_reply(ids[i], nullptr), needle);
    }
    const char *reply = test_reply(ids[0], nullptr);
    test_has("initialize names the server", reply, "\"name\":\"smith\"");
    test_has("initialize has tools", reply, "\"tools\"");
    test_has("initialize has instructions", reply, "\"instructions\"");
    reply = test_reply(discover, nullptr);
    test_has("discover says 2026-07-28", reply, "\"supportedVersions\":[\"2026-07-28\"]");
    test_has("discover names the server", reply, "\"name\":\"smith\"");
    test_has("a ping is answered", test_reply(ping, nullptr), "\"result\"");
    test_equal("an unknown method", "error -32601: method not found", test_reply(unknown, nullptr));
}

static void test_protocol_errors(void) {
    TestCase test = test_begin("protocol errors", false);
    test_add(&test.input, "this is not json\n");
    test_add(&test.input, "[1, 2]\n");
    test_add(&test.input, "\n");
    test_add(&test.input, "{'jsonrpc':'2.0','id':50,'method':'tools/call','params':{'arguments':{}}}\n");
    test_add(&test.input, "{'jsonrpc':'2.0','id':51,'method':'tools/call','params':{'name':'nonesuch'}}\n");
    test_add(&test.input, "{'jsonrpc':'2.0','id':52,'method':'tools/call'}\n");
    test_run(&test);
    test_has("a document that is not JSON", test_output.data, "\"code\":-32700");
    test_has("a message that is no request", test_output.data, "\"code\":-32600");
    test_equal("a call with no name", "error -32602: tools/call needs the name of a tool", test_reply(50, nullptr));
    test_equal("a call of no tool", "error -32602: unknown tool", test_reply(51, nullptr));
    test_equal("a call with no params", "error -32602: tools/call needs the name of a tool", test_reply(52, nullptr));
}

static void test_tools_list(void) {
    TestCase test = test_begin("tools/list", false);
    int id = test_request(&test.input, "tools/list", "");
    test_run(&test);
    static const char *const names[] = {"launch", "breakpoint", "continue",  "step_over", "step_in", "step_out",
                                        "pause",  "stack",      "variables", "evaluate",  "stop"};
    const char *list = test_reply(id, nullptr);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char needle[64]; // the reply is not a tool result: its JSON line is what test_reply gives
        snprintf(needle, sizeof needle, "\"name\":\"%s\"", names[i]);
        test_has(names[i], list, needle);
    }
    test_has("a tool has a schema", list, "\"inputSchema\":{\"type\":\"object\"");
    test_has("a read-only tool says so", list, "\"readOnlyHint\":true");
}

// --- tools with nothing to debug ------------------------------------------------------------------------------

static void test_no_session(void) {
    TestCase test = test_begin("no session", false);
    static const char *const tools[] = {"breakpoint", "continue", "step_over", "step_in",  "step_out",
                                        "pause",      "stack",    "variables", "evaluate", "stop"};
    int ids[10];
    for (size_t i = 0; i < 10; i++)
        ids[i] = test_call(&test.input, tools[i], "{'file':'a.c','line':1,'expression':'x'}");
    int missing = test_call(&test.input, "stack", "{'session':'s9'}");
    int neither = test_call(&test.input, "launch", "{}");
    int both = test_call(&test.input, "launch", "{'program':'a','test':'b'}");
    int runner = test_call(&test.input, "launch", "{'test':'t','cwd':'/nowhere'}");
    int adapter = test_call(&test.input, "launch", "{'program':'/p/prog'}");
    int absent = test_call(&test.input, "launch", "{'program':'prog.c','cwd':'/nowhere'}");
    test_run(&test);
    for (size_t i = 0; i < 10; i++)
        test_equal(tools[i], "no debug session: call launch first\n", test_failed(ids[i]));
    test_equal("an unknown session", "no debug session s9\n", test_failed(missing));
    test_has("launch with neither", test_failed(neither), "launch needs `program`");
    test_has("launch with both", test_failed(both), "launch needs `program`");
    test_has("launch of a test with no runner", test_failed(runner), "no test runner found");
    test_has("launch of a program that is not there", test_failed(absent), "/nowhere/prog.c is not a file");
    test_has("launch with no adapter to start", test_failed(adapter), "cannot start the debug adapter");
}

// --- a session ---------------------------------------------------------------------------------------------------

static constexpr char test_frame_add[] =
    "{'stackFrames':[{'id':1000,'name':'add','source':{'name':'prog.c','path':'/p/prog.c'},'line':10,'column':9},"
    "{'id':1001,'name':'main','source':{'name':'prog.c','path':'/p/prog.c'},'line':20,'column':5}],'totalFrames':2}";

// What the adapter writes while a launch with a breakpoint at prog.c:10 starts: requests 1 to 4, the stop, and the
// stack of the stop (request 5).
static void test_script_launch(TestBuffer *dap) {
    test_response(dap, 1, "initialize", "{'supportsConfigurationDoneRequest':true}");
    test_event(dap, "initialized", "");
    test_response(dap, 3, "setBreakpoints", "{'breakpoints':[{'id':1,'verified':true,'line':10}]}");
    test_response(dap, 4, "configurationDone", "");
    test_response(dap, 2, "launch", "");
    test_event(dap, "stopped",
               "{'reason':'breakpoint','description':'breakpoint 1.1','threadId':1,'hitBreakpointIds':[1],"
               "'allThreadsStopped':true}");
    test_response(dap, 5, "stackTrace", test_frame_add);
}

static constexpr char test_launch_arguments[] =
    "{'program':'/p/prog','args':['one'],'cwd':'/p','breakpoints':[{'file':'prog.c','line':10}]}";

static void test_launch_and_stack(void) {
    TestCase test = test_begin("launch, stack", true);
    test_script_launch(&test.dap);
    test_response(&test.dap, 6, "stackTrace", test_frame_add);
    int launch = test_call(&test.input, "launch", test_launch_arguments);
    int stack = test_call(&test.input, "stack", "{}");
    int stop = test_call(&test.input, "stop", "{}");
    test_run(&test);
    test_equal("launch stops at the breakpoint",
               "session s1: /p/prog one\n"
               "breakpoint 1: prog.c:10 (verified)\n"
               "stopped: breakpoint (breakpoint 1)\n"
               "  #0 add prog.c:10\n"
               "  > 10:     int sum = p.x + p.y;\n",
               test_ok(launch));
    test_equal("the stack", "thread 1, 2 frames\n#0 add prog.c:10\n#1 main prog.c:20\n", test_ok(stack));
    test_equal("stop", "session s1 stopped\n", test_ok(stop));
    test_has("initialize was sent", test_sent.data, "\"command\":\"initialize\"");
    test_has("launch was sent with the program", test_sent.data, "\"program\":\"/p/prog\"");
    test_has("launch was sent with the arguments", test_sent.data, "\"args\":[\"one\"]");
    test_has("the breakpoint was sent", test_sent.data, "\"command\":\"setBreakpoints\"");
    test_has("configurationDone was sent", test_sent.data, "\"command\":\"configurationDone\"");
    test_has("disconnect was sent", test_sent.data, "\"command\":\"disconnect\"");
    test_has("disconnect kills the program", test_sent.data, "\"terminateDebuggee\":true");
    test_lacks("a launch with breakpoints does not stop on entry", test_sent.data, "stopOnEntry");
}

// Every tool, in the order of a debugging session, on one adapter. The adapter's frames are in the order of the
// requests the tools send (the numbers in the comments), which is what the case asserts by running to the end.
static void test_script_session(TestBuffer *dap) {
    test_script_launch(dap); // 1 to 5
    test_response(dap, 6, "stackTrace", test_frame_add);
    // variables: 7 stackTrace, 8 scopes, 9 variables of Locals, 10 variables of p
    test_response(dap, 7, "stackTrace", test_frame_add);
    test_response(dap, 8, "scopes",
                  "{'scopes':[{'name':'Locals','variablesReference':1001,'expensive':false},"
                  "{'name':'Registers','variablesReference':1003,'expensive':false}]}");
    test_response(dap, 9, "variables",
                  "{'variables':[{'name':'p','value':'Point','type':'Point','variablesReference':1002},"
                  "{'name':'sum','value':'7','type':'int','variablesReference':0}]}");
    test_response(dap, 10, "variables",
                  "{'variables':[{'name':'x','value':'3','type':'int','variablesReference':0},"
                  "{'name':'y','value':'4','type':'int','variablesReference':0}]}");
    // evaluate: 11 stackTrace, 12 evaluate; then with side effects allowed 13 stackTrace, 14 evaluate
    test_response(dap, 11, "stackTrace", test_frame_add);
    test_response(dap, 12, "evaluate", "{'result':'4','type':'int','variablesReference':0}");
    test_response(dap, 13, "stackTrace", test_frame_add);
    test_response(dap, 14, "evaluate", "{'result':'5','type':'int','variablesReference':0}");
    // breakpoint: 15 setBreakpoints with both lines
    test_response(dap, 15, "setBreakpoints",
                  "{'breakpoints':[{'id':1,'verified':true,'line':10},{'id':2,'verified':true,'line':12}]}");
    // step_over: 16 next, the stop, 17 stackTrace
    test_response(dap, 16, "next", "");
    test_event(dap, "stopped", "{'reason':'step','description':'step over','threadId':1,'allThreadsStopped':true}");
    test_response(dap, 17, "stackTrace",
                  "{'stackFrames':[{'id':1000,'name':'add','source':{'name':'prog.c','path':'/p/prog.c'},'line':11,"
                  "'column':5}],'totalFrames':2}");
    // continue: 18 continue, the stop at the second breakpoint, 19 stackTrace
    test_response(dap, 18, "continue", "{'allThreadsContinued':true}");
    test_event(dap, "stopped",
               "{'reason':'breakpoint','description':'breakpoint 1.2','threadId':1,'hitBreakpointIds':[2]}");
    test_response(dap, 19, "stackTrace",
                  "{'stackFrames':[{'id':1000,'name':'add','source':{'name':'prog.c','path':'/p/prog.c'},'line':12,"
                  "'column':5}],'totalFrames':2}");
    // continue to the end: 20 continue, the program's output and its exit
    test_response(dap, 20, "continue", "{'allThreadsContinued':true}");
    test_event(dap, "output", "{'category':'stdout','output':'7\\r\\n'}");
    test_event(dap, "exited", "{'exitCode':0}");
    test_event(dap, "terminated", "");
    test_response(dap, 21, "disconnect", "");
}

static void test_session(void) {
    TestCase test = test_begin("a session", true);
    test_script_session(&test.dap);
    int launch = test_call(&test.input, "launch", test_launch_arguments);
    int stack = test_call(&test.input, "stack", "{}");
    int variables = test_call(&test.input, "variables", "{}");
    int evaluate = test_call(&test.input, "evaluate", "{'expression':'p.x + 1'}");
    int refused = test_call(&test.input, "evaluate", "{'expression':'p.x = 5'}");
    int allowed = test_call(&test.input, "evaluate", "{'expression':'p.x = 5','allow_side_effects':true}");
    int breakpoint = test_call(&test.input, "breakpoint", "{'file':'prog.c','line':12,'condition':'sum > 0'}");
    int step = test_call(&test.input, "step_over", "{}");
    int resume = test_call(&test.input, "continue", "{}");
    int finish = test_call(&test.input, "continue", "{}");
    int after = test_call(&test.input, "stack", "{}");
    int ended = test_call(&test.input, "continue", "{}");
    int stop = test_call(&test.input, "stop", "{}");
    int gone = test_call(&test.input, "stack", "{}");
    test_run(&test);
    test_has("launch", test_ok(launch), "stopped: breakpoint (breakpoint 1)");
    test_equal("stack", "thread 1, 2 frames\n#0 add prog.c:10\n#1 main prog.c:20\n", test_ok(stack));
    test_equal("variables",
               "frame #0, scope locals\n"
               "p (Point)\n"
               "  x = 3 (int)\n"
               "  y = 4 (int)\n"
               "sum = 7 (int)\n",
               test_ok(variables));
    test_equal("evaluate", "p.x + 1 = 4 (int)\n", test_ok(evaluate));
    test_has("an assignment is refused", test_failed(refused), "refused: the expression may change the program");
    test_equal("an assignment with allow_side_effects", "p.x = 5 = 5 (int)\n", test_ok(allowed));
    test_has("a breakpoint", test_ok(breakpoint), "prog.c:12");
    test_has("a conditional breakpoint was sent", test_sent.data, "\"condition\":\"sum > 0\"");
    test_equal("step_over",
               "stopped: step\n"
               "  #0 add prog.c:11\n"
               "  > 11:     total += sum;\n",
               test_ok(step));
    test_equal("continue to the next breakpoint",
               "stopped: breakpoint (breakpoint 2)\n"
               "  #0 add prog.c:12\n"
               "  > 12:     return total;\n",
               test_ok(resume));
    test_equal("continue to the end",
               "[stdout]\n7\n"
               "exited: the program exited with code 0\n"
               "the session is over: call stop, then launch to debug again\n",
               test_ok(finish));
    test_equal("stack after the end", "the program has exited; call stop, then launch again\n", test_failed(after));
    test_equal("continue after the end", "the program has exited; call stop, then launch again\n", test_failed(ended));
    test_equal("stop", "session s1 stopped (the program had exited with code 0)\n", test_ok(stop));
    test_equal("the session is gone", "no debug session: call launch first\n", test_failed(gone));
}

// The refusals of a stopped program's tools: a scope or frame that is not there, breakpoints removed and not there.
static void test_session_refusals(void) {
    TestCase test = test_begin("a session's refusals", true);
    test_script_launch(&test.dap); // 1 to 5
    test_response(&test.dap, 6, "stackTrace", test_frame_add);
    test_response(&test.dap, 7, "scopes",
                  "{'scopes':[{'name':'Locals','variablesReference':1001,'expensive':false},"
                  "{'name':'Registers','variablesReference':1003,'expensive':false}]}");
    test_response(&test.dap, 8, "stackTrace", test_frame_add);
    test_response(&test.dap, 9, "setBreakpoints", "{'breakpoints':[]}");
    test_response(&test.dap, 10, "disconnect", "");
    test_call(&test.input, "launch", test_launch_arguments);
    int scope = test_call(&test.input, "variables", "{'scope':'nonesuch'}");
    int frame = test_call(&test.input, "variables", "{'frame':5}");
    int paused = test_call(&test.input, "pause", "{}");
    int removed = test_call(&test.input, "breakpoint", "{'file':'prog.c','line':10,'remove':true}");
    int absent = test_call(&test.input, "breakpoint", "{'file':'prog.c','line':99,'remove':true}");
    int line = test_call(&test.input, "breakpoint", "{'file':'prog.c','line':0}");
    int empty = test_call(&test.input, "evaluate", "{'expression':''}");
    test_run(&test);
    test_equal("a scope that is not there", "no scope nonesuch; the frame has: Locals Registers\n", test_failed(scope));
    test_equal("a frame that is not there", "the stack has 2 frames; there is no frame 5\n", test_failed(frame));
    test_equal("pause of a stopped program", "the program is already stopped\n", test_ok(paused));
    test_equal("a breakpoint removed", "removed the breakpoint at prog.c:10\n", test_ok(removed));
    test_equal("a breakpoint that is not there", "no breakpoint at prog.c:99\n", test_failed(absent));
    test_equal("a breakpoint at line 0", "breakpoint needs a file and a line (1 or more)\n", test_failed(line));
    test_equal("an empty expression", "evaluate needs an expression\n", test_failed(empty));
}

// A test of the project, launched by name: the runner's own process, and a stop at the entry point, since there is no
// breakpoint.
static void test_launch_a_test(void) {
    TestCase test = test_begin("launch a test", true);
    test_response(&test.dap, 1, "initialize", "{}");
    test_event(&test.dap, "initialized", "");
    test_response(&test.dap, 3, "configurationDone", "");
    test_response(&test.dap, 2, "launch", "");
    test_event(&test.dap, "stopped", "{'reason':'entry','threadId':1}");
    test_response(
        &test.dap, 4, "stackTrace",
        "{'stackFrames':[{'id':1,'name':'start','line':0,'column':0,'instructionPointerReference':'0x1000'}]}");
    int launch = test_call(&test.input, "launch", "{'test':'carry_an_attack','runner':'build/tests','cwd':'/p'}");
    test_run(&test);
    const char *text = test_ok(launch);
    test_has("the runner", text, "session s1: /p/build/tests --test carry_an_attack --in-process\n");
    test_has("stopped at the entry", text, "stopped: entry\n  #0 start (no source, at 0x1000)\n");
    test_has("the test runs in the runner's process", test_sent.data,
             "\"args\":[\"--test\",\"carry_an_attack\",\"--in-process\"]");
    test_has("it stops on entry", test_sent.data, "\"stopOnEntry\":true");
}

// --- an adapter that fails ----------------------------------------------------------------------------------------

static void test_adapter_exits_at_once(void) {
    TestCase test = test_begin("an adapter that exits at once", true);
    test.exit_on = SmithChildExit_Start;
    int launch = test_call(&test.input, "launch", test_launch_arguments);
    int stack = test_call(&test.input, "stack", "{}");
    test_run(&test);
    test_has("launch fails", test_failed(launch), "initialize");
    test_equal("no session is left", "no debug session: call launch first\n", test_failed(stack));
}

static void test_adapter_refuses(void) {
    TestCase test = test_begin("an adapter that refuses to launch", true);
    test_response(&test.dap, 1, "initialize", "{}");
    test_failure(&test.dap, 2, "launch", "program does not exist");
    int launch = test_call(&test.input, "launch", "{'program':'/p/prog'}");
    test_run(&test);
    test_has("the adapter's reason", test_failed(launch), "program does not exist");
}

// Timeouts run on the fake clock, which moves while the server waits: no call here takes a moment.
static void test_running_program(void) {
    TestCase test = test_begin("a program that does not stop", true);
    test_response(&test.dap, 1, "initialize", "{}");
    test_event(&test.dap, "initialized", "");
    test_response(&test.dap, 3, "setBreakpoints", "{'breakpoints':[{'id':1,'verified':true,'line':10}]}");
    test_response(&test.dap, 4, "configurationDone", "");
    test_response(&test.dap, 2, "launch", "");
    test_response(&test.dap, 5, "pause", "");
    int launch = test_call(&test.input, "launch",
                           "{'program':'/p/prog','cwd':'/p','timeout_ms':100,"
                           "'breakpoints':[{'file':'prog.c','line':10}]}");
    int stack = test_call(&test.input, "stack", "{}");
    int resume = test_call(&test.input, "continue", "{'timeout_ms':50}");
    int pause = test_call(&test.input, "pause", "{'timeout_ms':50}");
    int silent = test_call(&test.input, "breakpoint", "{'file':'prog.c','line':11}");
    test_run(&test);
    test_has("launch says it is running", test_ok(launch), "the program is running (no stop yet)");
    test_equal("stack while running", "the program is running: call pause (or wait with continue) first\n",
               test_failed(stack));
    test_has("continue waits", test_ok(resume), "still running after 50 ms");
    test_has("pause that does not stop", test_failed(pause), "the program did not stop in time");
    test_has("an adapter that does not answer", test_failed(silent), "did not answer in time");
}

int main(void) {
    test_output = test_buffer_make();
    test_sent = test_buffer_make();
    void *memory = mmap(nullptr, (size_t)64 << 20, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    SMITH_ASSERT(memory != MAP_FAILED);
    test_view = smith_arena_fixed(memory, (size_t)64 << 20);
    test_initialize();
    test_protocol_errors();
    test_tools_list();
    test_no_session();
    test_launch_and_stack();
    test_session();
    test_session_refusals();
    test_launch_a_test();
    test_adapter_exits_at_once();
    test_adapter_refuses();
    test_running_program();
    printf("%d checks, %d failed\n", test_total, test_failures);
    return test_failures == 0 ? 0 : 1;
}
