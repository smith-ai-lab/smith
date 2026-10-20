// The build program's runtime. `smith build` compiles a project's build.c together with this file, runs the result, and
// the result writes .smith/build.ninja. Strings and lists live in a static arena (no malloc); the manifest is
// written only when its bytes change, so ninja's view of it changes only when the build does. See docs/build-c.md.
#include <smith/build.h>

#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <limits.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define SMITH_ASSERT(condition) \
    do { \
        if (!(condition)) smith_assert_fail(#condition, __FILE__, __LINE__); \
    } while (0)

constexpr int smith_exit_usage = 2;
constexpr size_t smith_arena_size = 64U << 20;
constexpr size_t smith_list_max = 1000000;
constexpr size_t smith_entries_max = 65536;
constexpr size_t smith_pools_max = 256;
constexpr size_t smith_options_max = 256;
constexpr size_t smith_defaults_max = 65536;
constexpr size_t smith_capture_max = 1U << 20;
constexpr size_t smith_cat_lists_max = 4096;
constexpr size_t smith_chunk = 65536;
constexpr size_t smith_defers_max = 256;
constexpr size_t smith_lines_file_max = 16U << 20;

typedef enum {
    SmithEntryKind_Step,
    SmithEntryKind_Task,
    SmithEntryKind_Alias,
} SmithEntryKind;

typedef struct {
    SmithEntryKind kind;
    const char *name;           // a task's or alias's name
    SmithStep step;             // the arena copy
    const char *const *targets; // an alias's targets
    bool stamp;                 // a step without outputs: touch its stamp when the command succeeds
} SmithEntry;

typedef struct {
    const char *name;
    int depth;
} SmithPool;

typedef struct {
    const char *name;
    const char *description;
    bool flag;
} SmithDeclared;

typedef struct {
    const char *name;
    const char *value; // nullptr for a bare -Dname
} SmithGiven;

typedef struct {
    const char *const *needs;
    void (*more)(SmithBuild *b);
    bool pending; // some need did not exist when the table was walked
} SmithDefer;

static alignas(16) char smith_arena[smith_arena_size];
static size_t smith_arena_used;
static SmithEntry smith_entries[smith_entries_max];
static size_t smith_entry_count;
static SmithPool smith_pools[smith_pools_max];
static size_t smith_pool_count;
static const char *smith_defaults[smith_defaults_max];
static size_t smith_default_count;
static bool smith_default_called; // an empty default is allowed: it builds nothing
static SmithDeclared smith_declared[smith_options_max];
static size_t smith_declared_count;
static SmithGiven smith_given[smith_options_max];
static size_t smith_given_count;
static SmithDefer smith_defers[smith_defers_max];
static size_t smith_defer_count;
static const char *smith_option_args[smith_options_max]; // the -D arguments of this call, as given
static size_t smith_option_arg_count;
static bool smith_regenerating;
static char smith_capture_buffer[smith_capture_max + 1];
static char smith_compare_a[smith_chunk];
static char smith_compare_b[smith_chunk];
static const char *smith_context; // "step x" or "task x" while one is being checked or written

[[noreturn]] static void smith_assert_fail(const char *condition, const char *file, int line) {
    fprintf(stderr, "smith: assertion failed: %s (%s:%d)\n", condition, file, line);
    fflush(stderr);
    abort();
}

// A mistake in build.c, not in smith.
[[noreturn, gnu::format(printf, 1, 2)]] static void smith_fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("smith: build.c: ", stderr);
    if (smith_context != nullptr) fprintf(stderr, "%s: ", smith_context);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    exit(smith_exit_usage);
}

[[noreturn]] static void smith_unimplemented(const char *name) {
    smith_context = nullptr;
    smith_fail("%s is not implemented yet", name);
}

[[noreturn]] static void smith_too_many(const char *things, size_t limit) {
    smith_context = nullptr;
    smith_fail("too many %s (limit %zu)", things, limit);
}

static void *smith_alloc(size_t size) {
    SMITH_ASSERT(size < smith_arena_size);
    size_t aligned = (size + 15U) & ~(size_t)15U;
    if (aligned > smith_arena_size - smith_arena_used) smith_too_many("strings and lists (bytes)", smith_arena_size);
    void *result = smith_arena + smith_arena_used;
    smith_arena_used += aligned;
    return result;
}

static const char *smith_copy_string(const char *text) {
    SMITH_ASSERT(text != nullptr);
    size_t length = strlen(text);
    char *copy = smith_alloc(length + 1);
    memcpy(copy, text, length + 1);
    return copy;
}

static size_t smith_count(const char *const *list) {
    if (list == nullptr) return 0;
    for (size_t i = 0; i < smith_list_max; i++) { // bounded
        if (list[i] == nullptr) return i;
    }
    SMITH_ASSERT(false); // a list this long is missing its nullptr
    return 0;
}

static const char **smith_new_list(size_t count) {
    const char **list = (const char **)smith_alloc((count + 1) * sizeof *list);
    list[count] = nullptr;
    return list;
}

// A nullptr list reads as empty, so an optional field left unset needs no care.
static const char *const *smith_copy_list(const char *const *list) {
    size_t count = smith_count(list);
    const char **copy = smith_new_list(count);
    for (size_t i = 0; i < count; i++)
        copy[i] = smith_copy_string(list[i]);
    return copy;
}

static const char *const *smith_one(const char *text) {
    const char **list = smith_new_list(1);
    list[0] = text;
    return list;
}

[[gnu::format(printf, 1, 0)]] static const char *smith_vformat(const char *format, va_list args) {
    va_list again;
    va_copy(again, args);
    int length = vsnprintf(nullptr, 0, format, args);
    SMITH_ASSERT(length >= 0);
    char *text = smith_alloc((size_t)length + 1);
    int written = vsnprintf(text, (size_t)length + 1, format, again);
    va_end(again);
    SMITH_ASSERT(written == length);
    return text;
}

[[gnu::format(printf, 1, 2)]] static const char *smith_text(const char *format, ...) {
    va_list args;
    va_start(args, format);
    const char *text = smith_vformat(format, args);
    va_end(args);
    return text;
}

static bool smith_is_name_byte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
           c == '-';
}

static void smith_check_name(const char *what, const char *name) {
    if (name == nullptr || *name == '\0') smith_fail("%s needs a name", what);
    for (const char *c = name; *c != '\0'; c++) {
        if (!smith_is_name_byte(*c)) smith_fail("%s name '%s' may hold only A-Z a-z 0-9 _ . -", what, name);
    }
}

static bool smith_name_taken(const char *name) {
    for (size_t i = 0; i < smith_entry_count; i++) {
        const SmithEntry *entry = &smith_entries[i];
        if (entry->kind != SmithEntryKind_Step && strcmp(entry->name, name) == 0) return true;
    }
    return false;
}

static SmithEntry *smith_add_entry(SmithEntryKind kind) {
    if (smith_entry_count >= smith_entries_max) smith_too_many("steps, tasks and aliases", smith_entries_max);
    SmithEntry *entry = &smith_entries[smith_entry_count++];
    *entry = (SmithEntry){.kind = kind};
    return entry;
}

static void smith_check_step(const SmithStep *step) {
    if (step->run != nullptr) smith_unimplemented(".run");
    if (step->args != nullptr) smith_unimplemented(".args");
    if (step->content_hash) smith_unimplemented(".content_hash");
    if (step->command == nullptr) smith_fail("a step needs a .command");
    if (step->command[0] == nullptr) smith_fail("a step needs a non-empty .command");
}

static SmithStep smith_copy_step(const SmithStep *step) {
    SmithStep copy = *step;
    copy.command = smith_copy_list(step->command);
    copy.inputs = smith_copy_list(step->inputs);
    copy.implicit_inputs = smith_copy_list(step->implicit_inputs);
    copy.order_only_inputs = smith_copy_list(step->order_only_inputs);
    copy.outputs = smith_copy_list(step->outputs);
    if (step->depfile != nullptr) copy.depfile = smith_copy_string(step->depfile);
    if (step->description != nullptr) copy.description = smith_copy_string(step->description);
    if (step->pool != nullptr) copy.pool = smith_copy_string(step->pool);
    return copy;
}

// FNV-1a 64 over each word and its 0 byte, then a 1 byte so that ("a", "b") and ("a b") lists differ.
static uint64_t smith_hash_list(uint64_t hash, const char *const *list) {
    for (size_t i = 0; list[i] != nullptr; i++) { // bounded by smith_copy_list
        for (const char *c = list[i];; c++) {
            hash = (hash ^ (unsigned char)*c) * UINT64_C(1099511628211);
            if (*c == '\0') break;
        }
    }
    return (hash ^ 1U) * UINT64_C(1099511628211);
}

// A stamp's name: the command and every input, so two steps with one command but different inputs get two stamps.
static uint64_t smith_hash_step(const SmithStep *step) {
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = smith_hash_list(hash, step->command);
    hash = smith_hash_list(hash, step->inputs);
    hash = smith_hash_list(hash, step->implicit_inputs);
    return smith_hash_list(hash, step->order_only_inputs);
}

static void smith_name_step(const SmithStep *step) {
    const char *first = smith_count(step->outputs) > 0 ? step->outputs[0] : nullptr;
    if (first == nullptr && step->command != nullptr) first = step->command[0];
    smith_context = smith_text("step %s", first != nullptr ? first : "(empty)");
}

void smith_step(SmithBuild *b, SmithStep *step) {
    SMITH_ASSERT(b != nullptr && step != nullptr);
    smith_name_step(step);
    smith_check_step(step);
    SmithStep copy = smith_copy_step(step);
    SmithEntry *entry = smith_add_entry(SmithEntryKind_Step);
    if (smith_count(copy.outputs) == 0) {
        copy.outputs = smith_one(smith_text(".smith/stamp/%016" PRIx64, smith_hash_step(&copy)));
        entry->stamp = true;
    }
    entry->step = copy;
    *step = copy;
    smith_context = nullptr;
}

void smith_task(SmithBuild *b, const char *name, SmithStep *step) {
    SMITH_ASSERT(b != nullptr && step != nullptr);
    smith_context = nullptr;
    smith_check_name("a task", name);
    smith_context = smith_text("task %s", name);
    if (smith_name_taken(name)) smith_fail("the name is already used by a task or an alias");
    if (smith_count(step->outputs) > 0) smith_fail("a task has no outputs; use smith_step");
    if (step->restat) smith_fail("a task has no outputs, so .restat means nothing; use smith_step");
    if (step->always) smith_fail("a task already runs every time; .always is not needed");
    smith_check_step(step);
    SmithStep copy = smith_copy_step(step);
    SmithEntry *entry = smith_add_entry(SmithEntryKind_Task);
    entry->name = smith_copy_string(name);
    copy.outputs = smith_one(smith_text(".smith/task/%s", name));
    entry->step = copy;
    copy.outputs = smith_one(entry->name);
    *step = copy;
    smith_context = nullptr;
}

void smith_alias(SmithBuild *b, const char *name, const char *const *targets) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    smith_check_name("an alias", name);
    smith_context = smith_text("alias %s", name);
    if (smith_name_taken(name)) smith_fail("the name is already used by a task or an alias");
    if (targets == nullptr) smith_fail("the targets list is nullptr; use an empty list");
    SmithEntry *entry = smith_add_entry(SmithEntryKind_Alias);
    entry->name = smith_copy_string(name);
    entry->targets = smith_copy_list(targets);
    smith_context = nullptr;
}

void smith_default(SmithBuild *b, const char *const *targets) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (targets == nullptr) smith_fail("smith_default: the targets list is nullptr; use an empty list");
    smith_default_called = true;
    for (size_t i = 0; targets[i] != nullptr; i++) {
        if (smith_default_count >= smith_defaults_max) smith_too_many("defaults", smith_defaults_max);
        smith_defaults[smith_default_count++] = smith_copy_string(targets[i]);
    }
}

void smith_pool(SmithBuild *b, const char *name, int depth) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    smith_check_name("a pool", name);
    if (strcmp(name, "console") == 0) smith_fail("the pool 'console' is ninja's and cannot be declared");
    if (depth < 1) smith_fail("pool '%s' needs a depth of at least 1", name);
    for (size_t i = 0; i < smith_pool_count; i++) {
        if (strcmp(smith_pools[i].name, name) == 0) smith_fail("pool '%s' is declared twice", name);
    }
    if (smith_pool_count >= smith_pools_max) smith_too_many("pools", smith_pools_max);
    smith_pools[smith_pool_count++] = (SmithPool){.name = smith_copy_string(name), .depth = depth};
}

// Options.

static const SmithGiven *smith_find_given(const char *name) {
    for (size_t i = smith_given_count; i > 0; i--) { // the last one given wins
        if (strcmp(smith_given[i - 1].name, name) == 0) return &smith_given[i - 1];
    }
    return nullptr;
}

static void smith_declare(const char *name, const char *description, bool flag) {
    smith_context = nullptr;
    if (name == nullptr || *name == '\0' || strchr(name, '=') != nullptr)
        smith_fail("an option needs a name without '='");
    for (size_t i = 0; i < smith_declared_count; i++) {
        if (strcmp(smith_declared[i].name, name) == 0) smith_fail("option '%s' is declared twice", name);
    }
    if (smith_declared_count >= smith_options_max) smith_too_many("options", smith_options_max);
    smith_declared[smith_declared_count++] = (SmithDeclared){
        .name = smith_copy_string(name),
        .description = description != nullptr ? smith_copy_string(description) : "",
        .flag = flag,
    };
}

bool smith_option(SmithBuild *b, const char *name, const char *description) {
    SMITH_ASSERT(b != nullptr);
    smith_declare(name, description, true);
    const SmithGiven *given = smith_find_given(name);
    if (given == nullptr) return false;
    if (given->value != nullptr) smith_fail("-D%s takes no value; write -D%s", name, name);
    return true;
}

const char *smith_option_value(SmithBuild *b, const char *name, const char *fallback, const char *description) {
    SMITH_ASSERT(b != nullptr);
    smith_declare(name, description, false);
    const SmithGiven *given = smith_find_given(name);
    if (given == nullptr) return fallback;
    if (given->value == nullptr) smith_fail("-D%s needs a value; write -D%s=value", name, name);
    return given->value;
}

static void smith_check_given(void) {
    for (size_t i = 0; i < smith_given_count; i++) {
        bool known = false;
        for (size_t j = 0; j < smith_declared_count; j++)
            known = known || strcmp(smith_declared[j].name, smith_given[i].name) == 0;
        if (known) continue;
        fprintf(stderr, "smith: build.c: unknown option -D%s\n", smith_given[i].name);
        fputs(smith_declared_count > 0 ? "declared options:\n" : "build.c declares no options\n", stderr);
        for (size_t j = 0; j < smith_declared_count; j++) {
            const SmithDeclared *option = &smith_declared[j];
            fprintf(stderr, "  -D%s%s  %s\n", option->name, option->flag ? "" : "=value", option->description);
        }
        exit(smith_exit_usage);
    }
}

// The outside world.

const char *smith_env(SmithBuild *b, const char *name, const char *fallback) {
    SMITH_ASSERT(b != nullptr && name != nullptr);
    const char *value = getenv(name);
    return value != nullptr ? value : fallback;
}

static int smith_compare_strings(const void *left, const void *right) {
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

const char *const *smith_glob(SmithBuild *b, const char *pattern) {
    SMITH_ASSERT(b != nullptr && pattern != nullptr);
    glob_t found = {};
    int status = glob(pattern, GLOB_NOSORT, nullptr, &found);
    if (status != 0 && status != GLOB_NOMATCH) smith_fail("glob '%s' failed", pattern);
    size_t count = status == 0 ? (size_t)found.gl_pathc : 0;
    const char **list = smith_new_list(count);
    for (size_t i = 0; i < count; i++)
        list[i] = smith_copy_string(found.gl_pathv[i]);
    globfree(&found);
    if (count > 1) qsort((void *)list, count, sizeof *list, smith_compare_strings); // strcmp, not the locale
    return list;
}

static void smith_wait(const char *name, pid_t pid) {
    int status = 0;
    int waited = -1;
    for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
        waited = waitpid(pid, &status, 0);
    if (waited < 0) smith_fail("%s: cannot wait for it", name);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return;
    if (WIFEXITED(status)) smith_fail("%s failed (status %d)", name, WEXITSTATUS(status));
    smith_fail("%s failed (signal %d)", name, WTERMSIG(status));
}

static size_t smith_read_all(const char *name, int fd) {
    size_t used = 0;
    for (size_t i = 0; i < smith_capture_max + 1024; i++) { // each read moves at least a byte, or ends the loop
        ssize_t got = read(fd, smith_capture_buffer + used, smith_capture_max + 1 - used);
        if (got == 0) return used;
        if (got < 0 && errno != EINTR) smith_fail("%s: cannot read its output", name);
        if (got > 0) used += (size_t)got;
        if (used > smith_capture_max) smith_fail("%s printed more than %zu bytes", name, smith_capture_max);
    }
    smith_fail("%s: its output did not end", name);
}

static pid_t smith_spawn_into(const char *const *argv, int pipe_fds[2]) {
    posix_spawn_file_actions_t actions;
    int setup = posix_spawn_file_actions_init(&actions);
    if (setup == 0) setup = posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    if (setup == 0) setup = posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    if (setup == 0 && pipe_fds[1] != STDOUT_FILENO) setup = posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
    SMITH_ASSERT(setup == 0);
    pid_t pid = 0;
    int status = posix_spawnp(&pid, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);
    if (status != 0) {
        close(pipe_fds[0]);
        smith_fail("%s failed (cannot run: %s)", argv[0], strerror(status));
    }
    return pid;
}

const char *smith_capture(SmithBuild *b, const char *const *argv) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (argv == nullptr || argv[0] == nullptr) smith_fail("smith_capture needs a command");
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) smith_fail("%s: cannot make a pipe", argv[0]);
    pid_t pid = smith_spawn_into(argv, pipe_fds);
    size_t length = smith_read_all(argv[0], pipe_fds[0]);
    close(pipe_fds[0]);
    smith_wait(argv[0], pid);
    if (length > 0 && smith_capture_buffer[length - 1] == '\n') length -= 1;
    char *text = smith_alloc(length + 1);
    memcpy(text, smith_capture_buffer, length);
    text[length] = '\0';
    return text;
}

// Helpers.

const char *smith_fmt(SmithBuild *b, const char *format, ...) {
    SMITH_ASSERT(b != nullptr && format != nullptr);
    va_list args;
    va_start(args, format);
    const char *text = smith_vformat(format, args);
    va_end(args);
    return text;
}

const char *const *smith_cat(SmithBuild *b, const char *const *first, ...) {
    SMITH_ASSERT(b != nullptr);
    static const char *const *lists[smith_cat_lists_max];
    size_t lists_count = 0;
    va_list args;
    va_start(args, first);
    for (const char *const *list = first; list != nullptr; list = va_arg(args, const char *const *)) {
        if (lists_count >= smith_cat_lists_max) smith_too_many("lists in smith_cat", smith_cat_lists_max);
        lists[lists_count++] = list;
    }
    va_end(args);
    size_t total = 0;
    for (size_t i = 0; i < lists_count; i++)
        total += smith_count(lists[i]);
    if (total > smith_list_max) smith_too_many("entries in a list", smith_list_max);
    const char **joined = smith_new_list(total);
    size_t used = 0;
    for (size_t i = 0; i < lists_count; i++) {
        for (size_t j = 0; lists[i][j] != nullptr; j++)
            joined[used++] = smith_copy_string(lists[i][j]);
    }
    SMITH_ASSERT(used == total);
    return joined;
}

const char *const *smith_map(SmithBuild *b, const char *const *items, const char *format) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (items == nullptr || format == nullptr) smith_fail("smith_map needs a list and a format");
    const char *hole = strstr(format, "%s");
    if (hole == nullptr || strstr(hole + 2, "%s") != nullptr)
        smith_fail("smith_map: '%s' must hold exactly one %%s", format);
    size_t count = smith_count(items);
    size_t before = (size_t)(hole - format);
    const char *after = hole + 2;
    const char **mapped = smith_new_list(count);
    for (size_t i = 0; i < count; i++) {
        size_t item_length = strlen(items[i]);
        size_t after_length = strlen(after);
        char *text = smith_alloc(before + item_length + after_length + 1);
        memcpy(text, format, before);
        memcpy(text + before, items[i], item_length);
        memcpy(text + before + item_length, after, after_length + 1);
        mapped[i] = text;
    }
    return mapped;
}

const char *const *smith_when(SmithBuild *b, bool condition, const char *const *items) {
    SMITH_ASSERT(b != nullptr);
    return smith_copy_list(condition ? items : nullptr);
}

const char *smith_replace_extension(SmithBuild *b, const char *path, const char *extension) {
    SMITH_ASSERT(b != nullptr && path != nullptr && extension != nullptr);
    const char *slash = strrchr(path, '/');
    const char *base = slash != nullptr ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t keep = dot != nullptr ? (size_t)(dot - path) : strlen(path);
    return smith_text("%.*s%s", (int)keep, path, extension);
}

void smith_defer(SmithBuild *b, const char *const *needs, void (*more)(SmithBuild *b)) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (needs == nullptr) smith_fail("smith_defer: the needs list is nullptr; use an empty list");
    if (more == nullptr) smith_fail("smith_defer needs a function to call");
    if (smith_defer_count >= smith_defers_max) smith_too_many("smith_defer calls", smith_defers_max);
    smith_defers[smith_defer_count++] = (SmithDefer){.needs = smith_copy_list(needs), .more = more};
}

static bool smith_all_exist(const char *const *paths) {
    struct stat info;
    for (size_t i = 0; paths[i] != nullptr; i++) { // bounded by smith_copy_list
        if (stat(paths[i], &info) != 0) return false;
    }
    return true;
}

// Walks the table in order up to its current count, which grows when a `more` defers again. Nothing builds while the
// program runs, so a single pass reaches every entry that can be called. Each `more` runs from this loop, not from
// inside another.
static void smith_run_defers(SmithBuild *b) {
    for (size_t i = 0; i < smith_defer_count; i++) { // bounded by smith_defers_max
        if (smith_all_exist(smith_defers[i].needs)) {
            smith_defers[i].more(b);
            smith_context = nullptr;
        } else {
            smith_defers[i].pending = true;
        }
    }
}

static bool smith_any_pending(void) {
    for (size_t i = 0; i < smith_defer_count; i++) {
        if (smith_defers[i].pending) return true;
    }
    return false;
}

// Reads the whole file into the arena, ending it with a 0 byte; returns its size.
static size_t smith_read_file(const char *path, char **text) {
    FILE *file = fopen(path, "rb");
    struct stat info;
    if (file == nullptr || fstat(fileno(file), &info) != 0 || !S_ISREG(info.st_mode)) {
        if (file != nullptr) fclose(file);
        smith_fail("cannot read %s; if a step makes it, read it inside smith_defer", path);
    }
    if ((uintmax_t)info.st_size > smith_lines_file_max) {
        fclose(file);
        smith_fail("%s is larger than %zu MiB", path, smith_lines_file_max >> 20);
    }
    size_t size = (size_t)info.st_size;
    *text = smith_alloc(size + 1);
    size_t got = fread(*text, 1, size, file);
    fclose(file);
    if (got != size) smith_fail("cannot read %s; if a step makes it, read it inside smith_defer", path);
    (*text)[size] = '\0';
    return size;
}

const char *const *smith_lines(SmithBuild *b, const char *path) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (path == nullptr) smith_fail("smith_lines needs a path");
    char *text = nullptr;
    size_t size = smith_read_file(path, &text);
    size_t count = 0;
    for (size_t i = 0; i < size; i++) // a final newline adds no empty entry
        count += text[i] == '\n' || (i + 1 == size && text[i] != '\n');
    if (count > smith_list_max) smith_too_many("lines in a file", smith_list_max);
    const char **list = smith_new_list(count);
    char *start = text;
    for (size_t i = 0; i < count; i++) {
        char *end = memchr(start, '\n', (size_t)(text + size - start));
        if (end == nullptr) end = text + size;
        *end = '\0'; // the last line's end is the terminator smith_read_file wrote
        list[i] = start;
        start = end + 1;
    }
    return list;
}

const char *const *smith_run_lines(SmithRun *run, const char *const *argv) {
    (void)run;
    (void)argv;
    smith_unimplemented("smith_run_lines");
}

// The manifest.

static void smith_put_path(FILE *out, const char *path) {
    for (const char *c = path; *c != '\0'; c++) {
        if (*c == '\n' || *c == '|') smith_fail("the path '%s' holds a newline or '|', which ninja cannot name", path);
        if (*c == '$' || *c == ' ' || *c == ':') fputc('$', out);
        fputc(*c, out);
    }
}

static void smith_put_paths(FILE *out, const char *const *paths) {
    for (size_t i = 0; paths[i] != nullptr; i++) { // bounded by smith_copy_list
        fputc(' ', out);
        smith_put_path(out, paths[i]);
    }
}

static bool smith_is_safe_word(const char *word) {
    if (*word == '\0') return false;
    for (const char *c = word; *c != '\0'; c++) {
        if (!smith_is_name_byte(*c) && *c != '/' && *c != '=' && *c != ':' && *c != ',' && *c != '+' && *c != '@' &&
            *c != '%' && *c != '^')
            return false;
    }
    return true;
}

static void smith_put_quoted(FILE *out, const char *word) {
    if (strchr(word, '\n') != nullptr) smith_fail("the command word '%s' holds a newline", word);
    if (smith_is_safe_word(word)) {
        fputs(word, out);
        return;
    }
    fputc('\'', out);
    for (const char *c = word; *c != '\0'; c++) {
        if (*c == '\'')
            fputs("'\\''", out);
        else if (*c == '$')
            fputs("$$", out);
        else
            fputc(*c, out);
    }
    fputc('\'', out);
}

static void smith_put_quoted_list(FILE *out, const char *const *words) {
    for (size_t i = 0; words[i] != nullptr; i++) {
        if (i > 0) fputc(' ', out);
        smith_put_quoted(out, words[i]);
    }
}

// A command word. The words $in and $out stand for the step's inputs and outputs; every other word is quoted for the
// shell and its $ is doubled. ninja evaluates an edge's own variable (cmd) without the edge's $in and $out, so the
// paths are written here, quoted the same way, instead of being left to ninja.
static void smith_put_word(FILE *out, const SmithStep *step, const char *word) {
    if (strcmp(word, "$in") == 0) {
        smith_put_quoted_list(out, step->inputs);
    } else if (strcmp(word, "$out") == 0) {
        smith_put_quoted_list(out, step->outputs);
    } else {
        smith_put_quoted(out, word);
    }
}

static void smith_put_value(FILE *out, const char *key, const char *value) {
    if (strchr(value, '\n') != nullptr) smith_fail("the %s '%s' holds a newline", key, value);
    fprintf(out, "  %s = ", key);
    for (const char *c = value; *c != '\0'; c++) {
        if (*c == '$') fputc('$', out);
        fputc(*c, out);
    }
    fputc('\n', out);
}

static void smith_check_pool(const SmithStep *step) {
    if (step->pool == nullptr || strcmp(step->pool, "console") == 0) return;
    for (size_t i = 0; i < smith_pool_count; i++) {
        if (strcmp(smith_pools[i].name, step->pool) == 0) return;
    }
    smith_fail("the pool '%s' is not declared; call smith_pool", step->pool);
}

static void smith_put_edge(FILE *out, const SmithEntry *entry) {
    const SmithStep *step = &entry->step;
    smith_check_pool(step);
    fputs("build", out);
    smith_put_paths(out, step->outputs);
    fputs(": step", out);
    smith_put_paths(out, step->inputs);
    if (smith_count(step->implicit_inputs) > 0 || step->always) fputs(" |", out);
    smith_put_paths(out, step->implicit_inputs);
    if (step->always) fputs(" smith_always", out);
    if (smith_count(step->order_only_inputs) > 0) fputs(" ||", out);
    smith_put_paths(out, step->order_only_inputs);
    fputs("\n  cmd =", out);
    for (size_t i = 0; step->command[i] != nullptr; i++) {
        fputc(' ', out);
        smith_put_word(out, step, step->command[i]);
    }
    if (entry->stamp) {
        fputs(" && touch ", out);
        smith_put_quoted_list(out, step->outputs);
    }
    fputc('\n', out);
    if (step->description != nullptr) smith_put_value(out, "description", step->description);
    if (step->depfile != nullptr) {
        smith_put_value(out, "depfile", step->depfile);
        fputs("  deps = gcc\n", out);
    }
    if (step->restat) fputs("  restat = 1\n", out);
    if (step->pool != nullptr) smith_put_value(out, "pool", step->pool);
}

static void smith_put_phony(FILE *out, const char *name, const char *const *targets) {
    fputs("build ", out);
    smith_put_path(out, name);
    fputs(": phony", out);
    smith_put_paths(out, targets);
    fputc('\n', out);
}

static void smith_put_entry(FILE *out, const SmithEntry *entry) {
    smith_context = entry->kind == SmithEntryKind_Step
                        ? smith_text("step %s", entry->step.outputs[0])
                        : smith_text("%s %s", entry->kind == SmithEntryKind_Task ? "task" : "alias", entry->name);
    if (entry->kind != SmithEntryKind_Alias) smith_put_edge(out, entry);
    if (entry->kind == SmithEntryKind_Task) smith_put_phony(out, entry->name, entry->step.outputs);
    if (entry->kind == SmithEntryKind_Alias) smith_put_phony(out, entry->name, entry->targets);
    smith_context = nullptr;
}

static void smith_put_regenerate_rule(FILE *out) {
    fputs("rule smith_regenerate\n  command = .smith/program --regenerate .smith/build.ninja", out);
    for (size_t i = 0; i < smith_option_arg_count; i++) {
        fputc(' ', out);
        smith_put_quoted(out, smith_option_args[i]);
    }
    fputs("\n  description = smith: regenerate .smith/build.ninja\n  generator = 1\n", out);
}

// ninja reruns the program when a need changes or the program does; it decides by mtime only.
static void smith_put_regenerate_edge(FILE *out) {
    fputs("build .smith/build.ninja: smith_regenerate", out);
    for (size_t i = 0; i < smith_defer_count; i++)
        smith_put_paths(out, smith_defers[i].needs);
    fputs(" | .smith/program\n", out);
}

static void smith_put_manifest(FILE *out) {
    fputs("# Written by smith build from build.c. Edit build.c, not this file.\n", out);
    fputs("ninja_required_version = 1.10\nbuilddir = .smith\nrule step\n  command = $cmd\n", out);
    if (smith_defer_count > 0) smith_put_regenerate_rule(out);
    for (size_t i = 0; i < smith_pool_count; i++)
        fprintf(out, "pool %s\n  depth = %d\n", smith_pools[i].name, smith_pools[i].depth);
    for (size_t i = 0; i < smith_entry_count; i++) {
        if (smith_entries[i].step.always) {
            fputs("build smith_always: phony\n", out);
            break;
        }
    }
    for (size_t i = 0; i < smith_entry_count; i++)
        smith_put_entry(out, &smith_entries[i]);
    if (smith_defer_count > 0) smith_put_regenerate_edge(out);
    // ninja builds every target when there is no default line, so an empty default names a target that does nothing.
    if (smith_default_count == 0) fputs("build smith_nothing: phony\ndefault smith_nothing", out);
    if (smith_default_count > 0) fputs("default", out);
    for (size_t i = 0; i < smith_default_count; i++) {
        fputc(' ', out);
        smith_put_path(out, smith_defaults[i]);
    }
    fputc('\n', out);
}

static bool smith_same_file(const char *left, const char *right) {
    FILE *a = fopen(left, "rb");
    FILE *b = fopen(right, "rb");
    bool same = a != nullptr && b != nullptr;
    for (size_t i = 0; same && i < (size_t)INT_MAX; i++) { // bounded; a chunk is read each time
        size_t got_a = fread(smith_compare_a, 1, smith_chunk, a);
        size_t got_b = fread(smith_compare_b, 1, smith_chunk, b);
        same = got_a == got_b && memcmp(smith_compare_a, smith_compare_b, got_a) == 0;
        if (got_a < smith_chunk || got_b < smith_chunk) break;
    }
    if (a != nullptr) fclose(a);
    if (b != nullptr) fclose(b);
    return same;
}

// Writes the manifest through a temporary file and renames it only when the bytes differ, so its mtime moves only
// when the build does. When ninja asked for the regeneration it always replaces it: the new mtime is past the needs,
// so ninja reloads once and settles.
static void smith_write_manifest(const char *path) {
    const char *temporary = smith_text("%s.tmp", path);
    FILE *out = fopen(temporary, "wb");
    if (out == nullptr) smith_fail("cannot write %s", temporary);
    smith_put_manifest(out);
    if (fclose(out) != 0) smith_fail("cannot write %s", temporary);
    if (!smith_regenerating && smith_same_file(temporary, path)) {
        unlink(temporary);
        return;
    }
    if (rename(temporary, path) != 0) smith_fail("cannot replace %s", path);
}

static void smith_parse_options(int count, char **args) {
    for (int i = 0; i < count; i++) { // bounded by count
        SMITH_ASSERT(strncmp(args[i], "-D", 2) == 0);
        if (smith_given_count >= smith_options_max) smith_too_many("-D arguments", smith_options_max);
        SMITH_ASSERT(smith_option_arg_count == smith_given_count); // the two tables grow together
        smith_option_args[smith_option_arg_count++] = args[i];
        const char *name = args[i] + 2;
        const char *equals = strchr(name, '=');
        if (*name == '\0' || equals == name) smith_fail("-D needs a name");
        size_t length = equals != nullptr ? (size_t)(equals - name) : strlen(name);
        char *copy = smith_alloc(length + 1);
        memcpy(copy, name, length);
        copy[length] = '\0';
        smith_given[smith_given_count++] =
            (SmithGiven){.name = copy, .value = equals != nullptr ? equals + 1 : nullptr};
    }
}

static SmithBuild smith_make_build(void) {
    struct utsname names;
    char folder[PATH_MAX];
    if (uname(&names) != 0 || getcwd(folder, sizeof folder) == nullptr) smith_fail("cannot read the system or folder");
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    return (SmithBuild){
        .os = smith_copy_string(names.sysname),
        .arch = smith_copy_string(names.machine),
        .root = smith_copy_string(folder),
        .cpus = cpus > 0 ? (int)cpus : 1,
    };
}

int main(int argc, char **argv) {
    SMITH_ASSERT(argc >= 2 && argv[1] != nullptr);
    smith_regenerating = strcmp(argv[1], "--regenerate") == 0;
    int first = smith_regenerating ? 2 : 1; // the manifest comes next, then the -D arguments
    SMITH_ASSERT(argc > first && argv[first] != nullptr);
    smith_parse_options(argc - first - 1, argv + first + 1);
    SmithBuild b = smith_make_build();
    build(&b);
    smith_context = nullptr;
    smith_run_defers(&b);
    smith_check_given();
    if (!smith_default_called && !smith_any_pending()) smith_fail("no default; call smith_default");
    smith_write_manifest(argv[first]);
    return 0;
}
