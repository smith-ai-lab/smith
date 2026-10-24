// A small Debug Adapter Protocol client for tests/dap.sh: it starts `smith dap`, writes the requests of a debugging
// session as hand-written JSON on its standard input, reads the replies and events from its standard output, and
// prints every message it sends (">") and receives ("<") so that the log is a transcript.
//
// Usage: dap_client <smith> <program> <source file> <line> <variable> <value> [<argument of the program> ...]
// The session: initialize, launch <program>, setBreakpoints at <line> of <source file>, configurationDone, the
// `stopped` event, stackTrace, scopes, variables (the locals, then the fields of the first one that has fields),
// continue, the `terminated` event, disconnect. It exits 0 when the locals hold <variable> with <value>, 1 otherwise.
#include <libproc.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cpu_wait.h"

extern char **environ;

constexpr size_t message_capacity = 1 << 20;
constexpr int message_limit = 400; // messages read while waiting for one, so a wait ends
constexpr int wait_milliseconds =
    30000; // of the adapter's CPU time, for each read (wall time is only a 10 minute backstop)

static int to_adapter = -1;
static int from_adapter = -1;
static pid_t adapter_pid = 0;
static char message[message_capacity];
static int seq = 0;

static void fail(const char *what) {
    fprintf(stderr, "dap_client: %s\n", what);
    if (adapter_pid > 0) kill(adapter_pid, SIGKILL); // our own child
    exit(1);
}

static void send_request(const char *command, const char *arguments) {
    char body[8192];
    int length = snprintf(body, sizeof body, "{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\",\"arguments\":%s}",
                          ++seq, command, arguments);
    if (length <= 0 || (size_t)length >= sizeof body) fail("request too long");
    char header[64];
    int header_length = snprintf(header, sizeof header, "Content-Length: %d\r\n\r\n", length);
    printf("> %s\n", body);
    if (write(to_adapter, header, (size_t)header_length) != header_length) fail("write failed");
    if (write(to_adapter, body, (size_t)length) != length) fail("write failed");
}

// The CPU time of the adapter and of what it started (lldb-dap starts debugserver, whose work is the adapter's wait).
static uint64_t tree_cpu_ns(pid_t pid, int depth) {
    uint64_t total = cpu_wait_cpu_ns(pid);
    pid_t children[64];
    int bytes = proc_listchildpids(pid, children, sizeof children);
    int count = bytes > 0 ? bytes / (int)sizeof(pid_t) : 0;
    for (int i = 0; depth > 0 && i < count && i < 64; i++)
        total += tree_cpu_ns(children[i], depth - 1);
    return total;
}

// How long, in wall seconds, neither the adapter nor its children may use CPU while a reply is awaited: past it the
// debugger is blocked, in practice waiting for the Developer Tools permission (60; the test shortens it).
static int idle_limit_milliseconds(void) {
    const char *seconds = getenv("DAP_CLIENT_IDLE_SECONDS");
    return seconds != nullptr ? atoi(seconds) * 1000 : 60000;
}

// Waits for the adapter's output: ready, spent (30 s of the adapter's own CPU), idle (nobody used CPU for the idle
// limit) or hung (the ten minute wall backstop).
constexpr int wait_idle = 3;
static int wait_for_adapter(void) {
    int idle_limit = idle_limit_milliseconds();
    uint64_t began = cpu_wait_wall_ms();
    uint64_t idle_began = began;
    uint64_t adapter_began = cpu_wait_cpu_ns(adapter_pid);
    uint64_t tree_last = tree_cpu_ns(adapter_pid, 3);
    while (true) {
        struct pollfd poller = {.fd = from_adapter, .events = POLLIN};
        if (poll(&poller, 1, cpu_wait_slice_milliseconds) > 0) return cpu_wait_ready;
        uint64_t now = cpu_wait_wall_ms();
        uint64_t tree_now = tree_cpu_ns(adapter_pid, 3);
        if (tree_now != tree_last) idle_began = now;
        tree_last = tree_now;
        if ((cpu_wait_cpu_ns(adapter_pid) - adapter_began) / 1000000 >= (uint64_t)wait_milliseconds)
            return cpu_wait_spent;
        if (now - idle_began >= (uint64_t)idle_limit) return wait_idle;
        if (now - began >= (uint64_t)cpu_wait_hang_milliseconds) return cpu_wait_hung;
    }
}

static void read_exactly(char *out, size_t count) {
    size_t have = 0;
    while (have < count) {
        int waited = wait_for_adapter();
        if (waited == cpu_wait_hung) fail("hung: no answer from the adapter in ten minutes of wall time");
        if (waited == wait_idle) fail("the debugger is waiting for permission (Developer Tools)");
        if (waited != cpu_wait_ready) fail("the adapter spent its CPU time limit without answering");
        ssize_t got = read(from_adapter, out + have, count - have);
        if (got <= 0) fail("the adapter closed its output");
        have += (size_t)got;
    }
}

// Reads one message into `message`.
static void read_message(void) {
    char header[256] = {};
    size_t used = 0;
    while (used < 4 || strcmp(header + used - 4, "\r\n\r\n") != 0) {
        if (used + 1 >= sizeof header) fail("header too long");
        read_exactly(header + used, 1);
        used += 1;
    }
    const char *field = strstr(header, "Content-Length:");
    if (field == nullptr) fail("no Content-Length");
    size_t length = strtoul(field + strlen("Content-Length:"), nullptr, 10);
    if (length == 0 || length >= message_capacity) fail("bad Content-Length");
    read_exactly(message, length);
    message[length] = '\0';
    if (strstr(message, "\"event\":\"module\"") != nullptr) {
        printf("< (a module event)\n");
    } else if (length > 500) {
        printf("< %.400s ... %s\n", message, message + length - 100); // the head, and the event's name at the end
    } else {
        printf("< %s\n", message);
    }
}

// Reads messages until one holds `needle`, so the events and replies in between are in the transcript.
static void wait_for(const char *needle) {
    for (int i = 0; i < message_limit; i++) {
        read_message();
        if (strstr(message, needle) != nullptr) return;
    }
    fprintf(stderr, "dap_client: never saw %s\n", needle);
    exit(1);
}

// The integer after `"key":` that follows `after` in the last message (the first one when after is null).
static long number_after(const char *after, const char *key) {
    const char *start = after != nullptr ? strstr(message, after) : message;
    if (start == nullptr) fail("the reply lacks a field");
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char *found = strstr(start, pattern);
    if (found == nullptr) fail("the reply lacks a number");
    return strtol(found + strlen(pattern), nullptr, 10);
}

static void start_adapter(const char *smith) {
    int in[2];
    int out[2];
    if (pipe(in) != 0 || pipe(out) != 0) fail("pipe failed");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_addclose(&actions, in[1]);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    char *argv[] = {(char *)smith, (char *)"dap", nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, smith, &actions, nullptr, argv, environ) != 0) fail("cannot start smith dap");
    adapter_pid = pid;
    close(in[0]);
    close(out[1]);
    to_adapter = in[1];
    from_adapter = out[0];
}

int main(int argc, char **argv) {
    if (argc < 7) fail("usage: dap_client smith program source line variable value [program arguments]");
    signal(SIGPIPE, SIG_IGN);
    const char *program = argv[2];
    const char *source = argv[3];
    const char *line = argv[4];
    char expected[128];
    snprintf(expected, sizeof expected, "\"name\":\"%s\"", argv[5]);
    start_adapter(argv[1]);

    char arguments[4096];
    send_request("initialize", "{\"adapterID\":\"smith\",\"clientID\":\"dap_client\",\"linesStartAt1\":true,"
                               "\"columnsStartAt1\":true,\"pathFormat\":\"path\"}");
    wait_for("\"command\":\"initialize\"");
    char program_arguments[1024] = "";
    for (int i = 7; i < argc; i++) {
        size_t used = strlen(program_arguments);
        snprintf(program_arguments + used, sizeof program_arguments - used, "%s\"%s\"", i > 7 ? "," : "", argv[i]);
    }
    snprintf(arguments, sizeof arguments, "{\"program\":\"%s\",\"args\":[%s],\"stopOnEntry\":false}", program,
             program_arguments);
    send_request("launch", arguments);
    wait_for("\"event\":\"initialized\"");
    snprintf(arguments, sizeof arguments, "{\"source\":{\"path\":\"%s\"},\"breakpoints\":[{\"line\":%s}]}", source,
             line);
    send_request("setBreakpoints", arguments);
    wait_for("\"command\":\"setBreakpoints\"");
    // Not verified yet is allowed: the file may be one the program loads later, and the `stopped` event settles it.
    send_request("configurationDone", "{}");
    wait_for("\"event\":\"stopped\"");
    if (strstr(message, "\"reason\":\"breakpoint\"") == nullptr) fail("stopped, but not at a breakpoint");
    long thread = number_after(nullptr, "threadId");

    snprintf(arguments, sizeof arguments, "{\"threadId\":%ld}", thread);
    send_request("stackTrace", arguments);
    wait_for("\"command\":\"stackTrace\"");
    long frame = number_after("\"stackFrames\"", "id");
    snprintf(arguments, sizeof arguments, "{\"frameId\":%ld}", frame);
    send_request("scopes", arguments);
    wait_for("\"command\":\"scopes\"");
    long locals = number_after("\"scopes\"", "variablesReference");
    snprintf(arguments, sizeof arguments, "{\"variablesReference\":%ld}", locals);
    send_request("variables", arguments);
    wait_for("\"command\":\"variables\"");
    bool found = strstr(message, expected) != nullptr && strstr(message, argv[6]) != nullptr;
    const char *struct_reference = strstr(message, "\"variablesReference\":");
    while (struct_reference != nullptr &&
           strtol(struct_reference + strlen("\"variablesReference\":"), nullptr, 10) == 0)
        struct_reference = strstr(struct_reference + 1, "\"variablesReference\":");
    if (struct_reference != nullptr) {
        long fields = strtol(struct_reference + strlen("\"variablesReference\":"), nullptr, 10);
        snprintf(arguments, sizeof arguments, "{\"variablesReference\":%ld}", fields);
        send_request("variables", arguments);
        wait_for("\"command\":\"variables\"");
    }

    snprintf(arguments, sizeof arguments, "{\"threadId\":%ld}", thread);
    send_request("continue", arguments);
    wait_for("\"event\":\"terminated\"");
    send_request("disconnect", "{}");
    close(to_adapter);
    int status = 0;
    wait(&status);
    printf("%s\n", found ? "dap_client: ok" : "dap_client: the locals lack the variable");
    return found ? 0 : 1;
}
