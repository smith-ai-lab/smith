// `smith dap` finds a debug object's sources (issue #621). Included by smith.c after debug_root.c.
//
// A debug object's compilation directory is `.`: lldb-dap needs a `sourcePath` in the launch request to map it to the
// project, and an editor does not know to send one. Before lldb-dap starts, smith forks a child that copies the
// client's messages (standard input) into a pipe that becomes lldb-dap's standard input, and adds
// `"sourcePath":"<root>"` to the arguments of a `launch` or `attach` request that names a `program` with a project root
// above it and has neither a `sourcePath` nor a `sourceMap` of its own. Every other byte passes unchanged. The child
// ends at the end of the input (or when lldb-dap has gone), and closes its standard output and error so it never holds
// the client's pipes open.

#ifdef SMITH_LLDB

constexpr size_t smith_dap_body_max = 1 << 20; // a larger message is not read: it passes unchanged, whole
constexpr size_t smith_dap_header_max = 128;
constexpr size_t smith_dap_chunk = 8192;

static bool smith_dap_write(int fd, const char *data, size_t count) {
    for (size_t done = 0; done < count;) { // bounded by count: each write moves at least a byte
        ssize_t written = write(fd, data + done, count - done);
        if (written <= 0) return false;
        done += (size_t)written;
    }
    return true;
}

// Reads exactly count bytes; false at the end of the input.
static bool smith_dap_read(int fd, char *data, size_t count) {
    for (size_t done = 0; done < count;) { // bounded by count
        ssize_t got = read(fd, data + done, count - done);
        if (got <= 0) return false;
        done += (size_t)got;
    }
    return true;
}

// The header of a message, up to and including the empty line. False at the end of the input or for a header that is
// too long.
static bool smith_dap_read_header(int fd, char *header, size_t *length) {
    *length = 0;
    while (*length < smith_dap_header_max - 1) { // bounded by the capacity
        if (!smith_dap_read(fd, header + *length, 1)) return false;
        (*length)++;
        if (*length >= 4 && memcmp(header + *length - 4, "\r\n\r\n", 4) == 0) {
            header[*length] = '\0';
            return true;
        }
    }
    return false;
}

// Copies the string value of "key" in the message to out. False when there is no such member, or the value is not a
// plain string (it has an escape).
static bool smith_dap_string(const char *body, const char *key, char *out, size_t capacity) {
    char quoted[64];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    const char *at = strstr(body, quoted);
    if (at == nullptr) return false;
    at += strlen(quoted);
    at += strspn(at, " \t\r\n");
    if (*at != ':') return false;
    at++;
    at += strspn(at, " \t\r\n");
    if (*at != '"') return false;
    at++;
    size_t length = strcspn(at, "\"\\");
    if (at[length] != '"' || length >= capacity) return false;
    memcpy(out, at, length);
    out[length] = '\0';
    return true;
}

// The message with sourcePath added to a launch or attach request's arguments, in place; the new length. A message that
// is not such a request, or has the member already, or names no program with a root, is unchanged.
static size_t smith_dap_rewrite(char *body, size_t length, size_t capacity) {
    char text[PATH_MAX];
    if (!smith_dap_string(body, "command", text, sizeof text) ||
        (strcmp(text, "launch") != 0 && strcmp(text, "attach") != 0))
        return length;
    if (strstr(body, "\"sourcePath\"") != nullptr || strstr(body, "\"sourceMap\"") != nullptr) return length;
    char program[PATH_MAX];
    if (!smith_dap_string(body, "program", program, sizeof program)) return length;
    char joined[PATH_MAX * 2];
    char folder[PATH_MAX];
    if (program[0] != '/' && smith_dap_string(body, "cwd", folder, sizeof folder)) {
        snprintf(joined, sizeof joined, "%s/%s", folder, program);
        snprintf(program, sizeof program, "%s", joined);
    }
    char root[PATH_MAX];
    if (!smith_project_root(program, root, sizeof root) || strpbrk(root, "\"\\") != nullptr) return length;
    char *arguments = strstr(body, "\"arguments\"");
    char *brace = arguments != nullptr ? strchr(arguments, '{') : nullptr;
    if (brace == nullptr) return length;
    bool empty = brace[1 + strspn(brace + 1, " \t\r\n")] == '}';
    char member[PATH_MAX + 32];
    int added = snprintf(member, sizeof member, "\"sourcePath\":\"%s\"%s", root, empty ? "" : ",");
    if (added <= 0 || length + (size_t)added >= capacity) return length;
    size_t offset = (size_t)(brace + 1 - body);
    memmove(body + offset + (size_t)added, body + offset, length - offset);
    memcpy(body + offset, member, (size_t)added);
    body[length + (size_t)added] = '\0';
    return length + (size_t)added;
}

// Copies `count` bytes from one descriptor to the other, unchanged.
static bool smith_dap_pass(int from, int to, size_t count) {
    char chunk[smith_dap_chunk];
    for (size_t done = 0; done < count;) { // bounded by count
        size_t step = count - done < sizeof chunk ? count - done : sizeof chunk;
        if (!smith_dap_read(from, chunk, step) || !smith_dap_write(to, chunk, step)) return false;
        done += step;
    }
    return true;
}

static char smith_dap_body[smith_dap_body_max + PATH_MAX + 64];

// The child's loop: message after message, from the client to lldb-dap.
static void smith_dap_copy(int from, int to) {
    char header[smith_dap_header_max];
    size_t header_length = 0;
    while (smith_dap_read_header(from, header, &header_length)) { // ends with the input
        const char *field = strstr(header, "Content-Length:");
        size_t length = field != nullptr ? (size_t)strtoull(field + strlen("Content-Length:"), nullptr, 10) : 0;
        if (field == nullptr || length > smith_dap_body_max) { // not a message to read: it passes as it is
            if (!smith_dap_write(to, header, header_length) || !smith_dap_pass(from, to, length)) return;
            continue;
        }
        if (!smith_dap_read(from, smith_dap_body, length)) return;
        smith_dap_body[length] = '\0';
        length = smith_dap_rewrite(smith_dap_body, length, sizeof smith_dap_body);
        char sent[smith_dap_header_max];
        int count = snprintf(sent, sizeof sent, "Content-Length: %zu\r\n\r\n", length);
        if (count <= 0 || !smith_dap_write(to, sent, (size_t)count) || !smith_dap_write(to, smith_dap_body, length))
            return;
    }
}

// Puts the filter between the client and lldb-dap: standard input becomes the pipe it writes. False (and nothing
// changed) for a terminal, which no client is, or when the pipe or the child cannot be made.
static bool smith_dap_filter_start(void) {
    int pipes[2];
    if (isatty(STDIN_FILENO) != 0 || pipe(pipes) != 0) return false;
    fflush(nullptr);
    pid_t child = fork();
    if (child < 0) {
        close(pipes[0]);
        close(pipes[1]);
        return false;
    }
    if (child == 0) {
        close(pipes[0]);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { // never hold the client's pipes open once lldb-dap has gone
            dup2(null, STDOUT_FILENO);
            dup2(null, STDERR_FILENO);
        }
        signal(SIGPIPE, SIG_DFL);
        smith_dap_copy(STDIN_FILENO, pipes[1]);
        _exit(0);
    }
    close(pipes[1]);
    dup2(pipes[0], STDIN_FILENO);
    close(pipes[0]);
    return true;
}

#endif
