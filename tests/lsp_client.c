// A small Language Server Protocol client for tests/lsp.sh: it starts `smith lsp` in a project, runs the commands on
// its command line against the server, and prints one line for each thing it learns, with the paths relative to the
// project and lines and columns from 1, as an editor shows them.
//
// Usage: lsp_client <smith> <project> <command> ...
//   open FILE ... [--]            opens the files together (the `--` ends the list, for more commands after it);
//   "diagnostics FILE" and a "diag FILE:L:C: message" line each replace FILE OLD NEW          changes the open FILE in
//   the editor (not on disk), first OLD to NEW; its diagnostics hover FILE L C                "hover FILE:L:C: text"
//   (the lines of the hover joined with " | ") definition FILE L C [END]     "definition FILE:L:C -> FILE:L:C"; with
//   END, asked again until the answer ends in END
//                                 (the project's background index is built while the server runs)
//   symbols FILE                  "symbol FILE: name" for each
//   complete FILE L C             "complete FILE:L:C: label" for the first 12 items
// The files are sent as language "c": a .smith file is C to the editor's protocol, and the server knows the rest.
// Exits 0 when the server answered every request, 1 otherwise.
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

constexpr size_t lsp_message_capacity = 1 << 20;
constexpr size_t lsp_text_capacity = 1 << 16;
constexpr size_t lsp_slot_count = 16;
constexpr size_t lsp_slot_capacity = 1 << 16;
constexpr int lsp_wait_milliseconds = 60000; // for a reply
constexpr int lsp_quiet_milliseconds = 600;  // without a new message, a server has said all it will about a file
constexpr int lsp_retry_limit = 40;

static int lsp_to_server = -1;
static int lsp_from_server = -1;
static char lsp_message[lsp_message_capacity];
static int lsp_next_id = 1;
static const char *lsp_root;

typedef struct {
    char file[256];
    char text[lsp_text_capacity]; // what the editor holds
    char published[lsp_slot_capacity];
    bool has_published;
} LspFile;

static LspFile lsp_files[lsp_slot_count];
static size_t lsp_file_count;

static void lsp_fail(const char *what, const char *detail) {
    fprintf(stderr, "lsp_client: %s%s%s\n", what, detail[0] != '\0' ? ": " : "", detail);
    exit(1);
}

static void lsp_send(const char *body) {
    char header[64];
    size_t length = strlen(body);
    int header_length = snprintf(header, sizeof header, "Content-Length: %zu\r\n\r\n", length);
    if (write(lsp_to_server, header, (size_t)header_length) != header_length) lsp_fail("write failed", "");
    if (write(lsp_to_server, body, length) != (ssize_t)length) lsp_fail("write failed", "");
}

static bool lsp_read_exactly(char *out, size_t count, int timeout) {
    size_t have = 0;
    while (have < count) {
        struct pollfd poller = {.fd = lsp_from_server, .events = POLLIN};
        if (poll(&poller, 1, timeout) <= 0) return false;
        ssize_t got = read(lsp_from_server, out + have, count - have);
        if (got <= 0) lsp_fail("the server closed its output", "");
        have += (size_t)got;
    }
    return true;
}

// Reads one message into lsp_message; false when none came in `timeout` milliseconds.
static bool lsp_read_message(int timeout) {
    char header[256] = {};
    size_t used = 0;
    while (used < 4 || strcmp(header + used - 4, "\r\n\r\n") != 0) {
        if (used + 1 >= sizeof header) lsp_fail("header too long", "");
        if (!lsp_read_exactly(header + used, 1, used == 0 ? timeout : lsp_wait_milliseconds)) return false;
        used += 1;
    }
    const char *field = strstr(header, "Content-Length:");
    if (field == nullptr) lsp_fail("no Content-Length", "");
    size_t length = strtoul(field + strlen("Content-Length:"), nullptr, 10);
    if (length == 0 || length >= lsp_message_capacity) lsp_fail("bad Content-Length", "");
    if (!lsp_read_exactly(lsp_message, length, lsp_wait_milliseconds)) lsp_fail("a message was cut short", "");
    lsp_message[length] = '\0';
    return true;
}

static LspFile *lsp_find(const char *file) {
    for (size_t i = 0; i < lsp_file_count; i++) {
        if (strcmp(lsp_files[i].file, file) == 0) return &lsp_files[i];
    }
    return nullptr;
}

// A server's publishDiagnostics, filed under its uri.
static void lsp_file_diagnostics(void) {
    if (strstr(lsp_message, "\"method\":\"textDocument/publishDiagnostics\"") == nullptr) return;
    const char *uri = strstr(lsp_message, "\"uri\":\"file://");
    if (uri == nullptr) return;
    uri += strlen("\"uri\":\"file://") + strlen(lsp_root) + 1;
    for (size_t i = 0; i < lsp_file_count; i++) {
        size_t length = strlen(lsp_files[i].file);
        if (strncmp(uri, lsp_files[i].file, length) == 0 && uri[length] == '"') {
            snprintf(lsp_files[i].published, sizeof lsp_files[i].published, "%s", lsp_message);
            lsp_files[i].has_published = true;
        }
    }
}

// The reply to request `id`, in lsp_message; the messages in between are filed or dropped.
static void lsp_wait_reply(int id) {
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"id\":%d,\"jsonrpc\":\"2.0\"", id);
    for (int i = 0; i < 100000; i++) {
        if (!lsp_read_message(lsp_wait_milliseconds)) lsp_fail("timed out waiting for a reply", "");
        lsp_file_diagnostics();
        if (strstr(lsp_message, pattern) != nullptr && strstr(lsp_message, "\"method\":") == nullptr) return;
    }
    lsp_fail("no reply", "");
}

static int lsp_request(const char *method, const char *params) {
    static char body[lsp_text_capacity * 2 + 1024];
    int id = lsp_next_id++;
    snprintf(body, sizeof body, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}", id, method, params);
    lsp_send(body);
    lsp_wait_reply(id);
    return id;
}

static void lsp_notify(const char *method, const char *params) {
    static char body[lsp_text_capacity * 2 + 1024];
    snprintf(body, sizeof body, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}", method, params);
    lsp_send(body);
}

// `text` as the inside of a JSON string.
static void lsp_escape(const char *text, char *out, size_t capacity) {
    size_t used = 0;
    for (const char *c = text; *c != '\0' && used + 8 < capacity; c++) {
        if (*c == '"' || *c == '\\') {
            out[used++] = '\\';
            out[used++] = *c;
        } else if (*c == '\n') {
            out[used++] = '\\';
            out[used++] = 'n';
        } else if (*c == '\t') {
            out[used++] = '\\';
            out[used++] = 't';
        } else {
            out[used++] = *c;
        }
    }
    out[used] = '\0';
}

// The JSON string that starts after the opening quote at `start`, unescaped into `out`; newlines become " | ".
static const char *lsp_unescape(const char *start, char *out, size_t capacity) {
    size_t used = 0;
    const char *c = start;
    for (; *c != '\0' && *c != '"' && used + 4 < capacity; c++) {
        if (*c != '\\') {
            out[used++] = *c;
            continue;
        }
        c++;
        if (*c == 'n') {
            out[used++] = ' ';
            out[used++] = '|';
            out[used++] = ' ';
        } else if (*c == 'u') {
            out[used++] = '?';
            c += 4;
        } else {
            out[used++] = *c;
        }
    }
    out[used] = '\0';
    return c;
}

// The line and column (from 1) of the first range start after `from`.
static bool lsp_start_after(const char *from, int *line, int *column) {
    const char *start = strstr(from, "\"start\":{\"character\":");
    if (start == nullptr) return false;
    start += strlen("\"start\":{\"character\":");
    *column = (int)strtol(start, nullptr, 10) + 1;
    const char *at = strstr(start, "\"line\":");
    if (at == nullptr) return false;
    *line = (int)strtol(at + strlen("\"line\":"), nullptr, 10) + 1;
    return true;
}

static void lsp_open_file(const char *file) {
    if (lsp_file_count >= lsp_slot_count) lsp_fail("too many files", file);
    LspFile *slot = &lsp_files[lsp_file_count++];
    snprintf(slot->file, sizeof slot->file, "%s", file);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", lsp_root, file);
    FILE *in = fopen(path, "r");
    if (in == nullptr) lsp_fail("cannot read", path);
    size_t got = fread(slot->text, 1, sizeof slot->text - 1, in);
    fclose(in);
    slot->text[got] = '\0';
    static char escaped[lsp_text_capacity * 2];
    static char params[lsp_text_capacity * 2 + 1024];
    lsp_escape(slot->text, escaped, sizeof escaped);
    snprintf(params, sizeof params,
             "{\"textDocument\":{\"uri\":\"file://%s\",\"languageId\":\"c\",\"version\":1,\"text\":\"%s\"}}", path,
             escaped);
    lsp_notify("textDocument/didOpen", params);
}

// Reads until each of `files` has published, then until the server has been quiet for a moment.
static void lsp_settle(LspFile *const *files, size_t count) {
    for (int i = 0; i < 100000; i++) {
        bool all = true;
        for (size_t f = 0; f < count; f++)
            all = all && files[f]->has_published;
        if (all && !lsp_read_message(lsp_quiet_milliseconds)) return;
        if (!all && !lsp_read_message(lsp_wait_milliseconds)) lsp_fail("timed out waiting for diagnostics", "");
        lsp_file_diagnostics();
    }
    lsp_fail("diagnostics never settled", "");
}

static void lsp_print_diagnostics(const LspFile *file) {
    printf("diagnostics %s\n", file->file);
    const char *at = file->published;
    for (int i = 0; i < 200; i++) { // a diagnostic each
        at = strstr(at, "\"message\":\"");
        if (at == nullptr) return;
        char message[1024];
        const char *end = lsp_unescape(at + strlen("\"message\":\""), message, sizeof message);
        int line = 0;
        int column = 0;
        const char *next = strstr(end, "\"message\":\"");
        if (lsp_start_after(end, &line, &column) && (next == nullptr || strstr(end, "\"start\"") < next)) {
            printf("diag %s:%d:%d: %s\n", file->file, line, column, message);
        }
        at = end;
    }
}

static void lsp_command_open(char **files, int count) {
    LspFile *opened[lsp_slot_count] = {};
    for (int i = 0; i < count; i++) {
        lsp_open_file(files[i]);
        opened[i] = &lsp_files[lsp_file_count - 1];
    }
    lsp_settle(opened, (size_t)count);
    for (int i = 0; i < count; i++)
        lsp_print_diagnostics(opened[i]);
}

static void lsp_command_replace(const char *file, const char *from, const char *to) {
    LspFile *slot = lsp_find(file);
    if (slot == nullptr) lsp_fail("not open", file);
    char *at = strstr(slot->text, from);
    if (at == nullptr) lsp_fail("the text to replace is not in", file);
    static char changed[lsp_text_capacity];
    snprintf(changed, sizeof changed, "%.*s%s%s", (int)(at - slot->text), slot->text, to, at + strlen(from));
    memcpy(slot->text, changed, sizeof slot->text);
    static char escaped[lsp_text_capacity * 2];
    static char params[lsp_text_capacity * 2 + 1024];
    lsp_escape(slot->text, escaped, sizeof escaped);
    snprintf(params, sizeof params,
             "{\"textDocument\":{\"uri\":\"file://%s/%s\",\"version\":2},\"contentChanges\":[{\"text\":\"%s\"}]}",
             lsp_root, file, escaped);
    slot->has_published = false;
    lsp_notify("textDocument/didChange", params);
    LspFile *only[1] = {slot};
    lsp_settle(only, 1);
    lsp_print_diagnostics(slot);
}

static void lsp_position_params(char *params, size_t capacity, const char *file, const char *line, const char *column,
                                const char *extra) {
    snprintf(params, capacity,
             "{\"textDocument\":{\"uri\":\"file://%s/%s\"},\"position\":{\"line\":%ld,\"character\":%ld}%s}", lsp_root,
             file, strtol(line, nullptr, 10) - 1, strtol(column, nullptr, 10) - 1, extra);
}

static void lsp_command_hover(const char *file, const char *line, const char *column) {
    char params[1024];
    lsp_position_params(params, sizeof params, file, line, column, "");
    lsp_request("textDocument/hover", params);
    const char *value = strstr(lsp_message, "\"value\":\"");
    char text[2048] = "(nothing)";
    if (value != nullptr) lsp_unescape(value + strlen("\"value\":\""), text, sizeof text);
    printf("hover %s:%s:%s: %s\n", file, line, column, text);
}

// The first location of a definition reply as "FILE:L:C", else "(nothing)".
static void lsp_first_location(char *out, size_t capacity) {
    snprintf(out, capacity, "(nothing)");
    const char *range = strstr(lsp_message, "\"range\":{");
    const char *uri = strstr(lsp_message, "\"uri\":\"file://");
    int line = 0;
    int column = 0;
    if (range == nullptr || uri == nullptr || !lsp_start_after(range, &line, &column)) return;
    uri += strlen("\"uri\":\"file://") + strlen(lsp_root) + 1;
    const char *end = strchr(uri, '"');
    if (end == nullptr) return;
    snprintf(out, capacity, "%.*s:%d:%d", (int)(end - uri), uri, line, column);
}

static void lsp_command_definition(const char *file, const char *line, const char *column, const char *expected) {
    char params[1024];
    char where[1024];
    lsp_position_params(params, sizeof params, file, line, column, "");
    for (int attempt = 0; attempt < lsp_retry_limit; attempt++) {
        lsp_request("textDocument/definition", params);
        lsp_first_location(where, sizeof where);
        size_t have = strlen(where);
        size_t want = expected != nullptr ? strlen(expected) : 0;
        if (expected == nullptr || (have >= want && strcmp(where + have - want, expected) == 0)) break;
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 250000000};
        nanosleep(&pause, nullptr);
    }
    printf("definition %s:%s:%s -> %s\n", file, line, column, where);
}

static void lsp_command_symbols(const char *file) {
    char params[1024];
    snprintf(params, sizeof params, "{\"textDocument\":{\"uri\":\"file://%s/%s\"}}", lsp_root, file);
    lsp_request("textDocument/documentSymbol", params);
    const char *at = lsp_message;
    for (int i = 0; i < 200; i++) { // a symbol each
        at = strstr(at, "\"name\":\"");
        if (at == nullptr) return;
        char name[256];
        at = lsp_unescape(at + strlen("\"name\":\""), name, sizeof name);
        printf("symbol %s: %s\n", file, name);
    }
}

static void lsp_command_complete(const char *file, const char *line, const char *column) {
    char params[1024];
    lsp_position_params(params, sizeof params, file, line, column, "");
    lsp_request("textDocument/completion", params);
    const char *at = lsp_message;
    for (int i = 0; i < 12; i++) { // the first twelve
        at = strstr(at, "\"label\":\"");
        if (at == nullptr) return;
        char label[256];
        at = lsp_unescape(at + strlen("\"label\":\""), label, sizeof label);
        printf("complete %s:%s:%s: %s\n", file, line, column, label);
    }
}

static void lsp_start(const char *smith) {
    int in[2];
    int out[2];
    if (pipe(in) != 0 || pipe(out) != 0) lsp_fail("pipe failed", "");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_addclose(&actions, in[1]);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", 1, 0);
    char *argv[] = {(char *)smith, (char *)"lsp", nullptr};
    pid_t pid = 0;
    if (posix_spawn(&pid, smith, &actions, nullptr, argv, environ) != 0) lsp_fail("cannot start smith lsp", "");
    close(in[0]);
    close(out[1]);
    lsp_to_server = in[1];
    lsp_from_server = out[0];
}

// Runs the command at argv[at]; returns how many arguments it took.
static int lsp_run_command(int argc, char **argv, int at) {
    const char *name = argv[at];
    int rest = argc - at - 1;
    if (strcmp(name, "--") == 0) return 1;
    if (strcmp(name, "open") == 0) {
        int count = 0;
        while (at + 1 + count < argc && count < (int)lsp_slot_count && strcmp(argv[at + 1 + count], "--") != 0)
            count++;
        lsp_command_open(argv + at + 1, count);
        return 1 + count + (at + 1 + count < argc && strcmp(argv[at + 1 + count], "--") == 0 ? 1 : 0);
    }
    if (strcmp(name, "replace") == 0 && rest >= 3) {
        lsp_command_replace(argv[at + 1], argv[at + 2], argv[at + 3]);
        return 4;
    }
    if (strcmp(name, "hover") == 0 && rest >= 3) {
        lsp_command_hover(argv[at + 1], argv[at + 2], argv[at + 3]);
        return 4;
    }
    if (strcmp(name, "definition") == 0 && rest >= 3) {
        bool end = rest >= 4 && strchr(argv[at + 4], ':') != nullptr;
        lsp_command_definition(argv[at + 1], argv[at + 2], argv[at + 3], end ? argv[at + 4] : nullptr);
        return end ? 5 : 4;
    }
    if (strcmp(name, "symbols") == 0 && rest >= 1) {
        lsp_command_symbols(argv[at + 1]);
        return 2;
    }
    if (strcmp(name, "complete") == 0 && rest >= 3) {
        lsp_command_complete(argv[at + 1], argv[at + 2], argv[at + 3]);
        return 4;
    }
    lsp_fail("unknown command", name);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) lsp_fail("usage: lsp_client smith project command ...", "");
    signal(SIGPIPE, SIG_IGN);
    lsp_root = argv[2];
    lsp_start(argv[1]);
    char params[2048];
    snprintf(
        params, sizeof params,
        "{\"processId\":null,\"rootUri\":\"file://%s\",\"capabilities\":{},\"clientInfo\":{\"name\":\"lsp_client\"}}",
        lsp_root);
    lsp_request("initialize", params);
    lsp_notify("initialized", "{}");
    for (int at = 3; at < argc;)
        at += lsp_run_command(argc, argv, at);
    lsp_request("shutdown", "null");
    lsp_notify("exit", "null");
    close(lsp_to_server);
    int status = 0;
    wait(&status);
    printf("lsp_client: done\n");
    return 0;
}
