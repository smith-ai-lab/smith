// The build program's runtime. `smith build` compiles a project's build.c together with this file, runs the result, and
// the result writes .smith/build.ninja. Strings and lists live in a static arena (no malloc); the manifest is
// written only when its bytes change, so ninja's view of it changes only when the build does. See docs/build-c.md.
#include <smith/build.h>

#include <errno.h>
#include <fcntl.h>
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
constexpr int smith_exit_not_started = 127;
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
constexpr size_t smith_command_max = 1U << 20;
constexpr int smith_glob_first = GLOB_NOSORT;
constexpr int smith_glob_next = GLOB_NOSORT | GLOB_APPEND;

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
    const char *value; // what the first call returned: a flag's setting is value != nullptr, an option's value itself
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
static char smith_command_buffer[smith_command_max + 1];
static const char *smith_context; // "step x" or "task x" while one is being checked or written
static bool smith_run_mode;       // --run: build() is replayed to find a step's function; nothing is written
static bool smith_in_run;         // the function is running, so smith_write writes again

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

// A task or alias name is a path of components, so it may hold '/': check-alone/src/a.c. It cannot start with '/' or
// '-', end with '/', or hold an empty, '.' or '..' component, which ninja would fold into another name.
static void smith_check_path_name(const char *what, const char *name) {
    if (name == nullptr || *name == '\0') smith_fail("%s needs a name", what);
    for (const char *c = name; *c != '\0'; c++) {
        if (!smith_is_name_byte(*c) && *c != '/')
            smith_fail("%s name '%s' may hold only A-Z a-z 0-9 _ . / -", what, name);
    }
    if (*name == '/' || *name == '-') smith_fail("%s name '%s' may not start with '/' or '-'", what, name);
    if (name[strlen(name) - 1] == '/') smith_fail("%s name '%s' may not end with '/'", what, name);
    const char *start = name;
    for (const char *c = name;; c++) {
        if (*c != '/' && *c != '\0') continue;
        size_t length = (size_t)(c - start);
        if (length == 0 || (length == 1 && start[0] == '.') || (length == 2 && start[0] == '.' && start[1] == '.'))
            smith_fail("%s name '%s' may not hold an empty, '.' or '..' component", what, name);
        if (*c == '\0') break;
        start = c + 1;
    }
}

static SmithEntry *smith_find_named(const char *name) {
    for (size_t i = 0; i < smith_entry_count; i++) {
        SmithEntry *entry = &smith_entries[i];
        if (entry->kind != SmithEntryKind_Step && strcmp(entry->name, name) == 0) return entry;
    }
    return nullptr;
}

static SmithEntry *smith_add_entry(SmithEntryKind kind) {
    if (smith_entry_count >= smith_entries_max) smith_too_many("steps, tasks and aliases", smith_entries_max);
    SmithEntry *entry = &smith_entries[smith_entry_count++];
    *entry = (SmithEntry){.kind = kind};
    return entry;
}

static void smith_check_step(const SmithStep *step) {
    if (step->content_hash) smith_unimplemented(".content_hash");
    bool has_command = step->command != nullptr && step->command[0] != nullptr;
    if (step->run != nullptr) {
        if (has_command) smith_fail("a step has .run or .command, not both");
        return;
    }
    if (step->args != nullptr) smith_fail(".args is given to .run, and this step has none");
    if (step->command == nullptr) smith_fail("a step needs a .command or a .run");
    if (!has_command) smith_fail("a step needs a non-empty .command");
}

static SmithStep smith_copy_step(const SmithStep *step) {
    SmithStep copy = *step;
    copy.command = smith_copy_list(step->command);
    copy.args = smith_copy_list(step->args);
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

static uint64_t smith_hash_bytes(const char *bytes, size_t length) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; i++)
        hash = (hash ^ (unsigned char)bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

// A stamp's name: the command and every input, so two steps with one command but different inputs get two stamps.
static uint64_t smith_hash_step(const SmithStep *step) {
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = smith_hash_list(hash, step->command);
    hash = smith_hash_list(hash, step->inputs);
    hash = smith_hash_list(hash, step->implicit_inputs);
    if (step->run != nullptr) hash = smith_hash_list(hash, step->args); // a function has no stable name to hash
    return smith_hash_list(hash, step->order_only_inputs);
}

static void smith_name_step(const SmithStep *step) {
    const char *first = smith_count(step->outputs) > 0 ? step->outputs[0] : nullptr;
    if (first == nullptr && step->command != nullptr) first = step->command[0];
    smith_context = smith_text("step %s", first != nullptr ? first : "(empty)");
}

static bool smith_same_list(const char *const *left, const char *const *right) {
    size_t count = smith_count(left);
    if (count != smith_count(right)) return false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(left[i], right[i]) != 0) return false;
    }
    return true;
}

static bool smith_same_text(const char *left, const char *right) {
    if (left == nullptr || right == nullptr) return left == right;
    return strcmp(left, right) == 0;
}

// The first field that differs between two steps, or nullptr when every field is equal.
static const char *smith_step_difference(const SmithStep *left, const SmithStep *right) {
    if (left->run != right->run) return ".run";
    if (!smith_same_list(left->command, right->command)) return ".command";
    if (!smith_same_list(left->args, right->args)) return ".args";
    if (!smith_same_list(left->inputs, right->inputs)) return ".inputs";
    if (!smith_same_list(left->implicit_inputs, right->implicit_inputs)) return ".implicit_inputs";
    if (!smith_same_list(left->order_only_inputs, right->order_only_inputs)) return ".order_only_inputs";
    if (!smith_same_list(left->outputs, right->outputs)) return ".outputs";
    if (!smith_same_text(left->depfile, right->depfile)) return ".depfile";
    if (!smith_same_text(left->description, right->description)) return ".description";
    if (!smith_same_text(left->pool, right->pool)) return ".pool";
    if (left->restat != right->restat) return ".restat";
    if (left->always != right->always) return ".always";
    return nullptr;
}

// The earlier step that makes one of these outputs, and which output; nullptr when there is none.
static const SmithEntry *smith_find_output(const char *const *outputs, const char **shared) {
    for (size_t i = 0; i < smith_entry_count; i++) {
        const SmithEntry *entry = &smith_entries[i];
        if (entry->kind != SmithEntryKind_Step) continue;
        for (size_t j = 0; outputs[j] != nullptr; j++) { // bounded by smith_copy_list
            for (size_t k = 0; entry->step.outputs[k] != nullptr; k++) {
                if (strcmp(outputs[j], entry->step.outputs[k]) != 0) continue;
                *shared = outputs[j];
                return entry;
            }
        }
    }
    return nullptr;
}

void smith_step(SmithBuild *b, SmithStep *step) {
    SMITH_ASSERT(b != nullptr && step != nullptr);
    smith_name_step(step);
    smith_check_step(step);
    SmithStep copy = smith_copy_step(step);
    bool stamp = smith_count(copy.outputs) == 0;
    if (stamp) copy.outputs = smith_one(smith_text(".smith/stamp/%016" PRIx64, smith_hash_step(&copy)));
    const char *shared = nullptr;
    const SmithEntry *earlier = smith_find_output(copy.outputs, &shared);
    if (earlier != nullptr) { // an identical step declared again changes nothing
        const char *field = smith_step_difference(&earlier->step, &copy);
        if (field != nullptr)
            smith_fail("the output '%s' is already made by an earlier step whose %s differs", shared, field);
        *step = earlier->step;
        smith_context = nullptr;
        return;
    }
    SmithEntry *entry = smith_add_entry(SmithEntryKind_Step);
    entry->stamp = stamp;
    entry->step = copy;
    *step = copy;
    smith_context = nullptr;
}

static void smith_check_task(const SmithStep *step) {
    if (smith_count(step->outputs) > 0) smith_fail("a task has no outputs; use smith_step");
    if (step->restat) smith_fail("a task has no outputs, so .restat means nothing; use smith_step");
    if (step->always) smith_fail("a task already runs every time; .always is not needed");
    smith_check_step(step);
}

void smith_task(SmithBuild *b, const char *name, SmithStep *step) {
    SMITH_ASSERT(b != nullptr && step != nullptr);
    smith_context = nullptr;
    smith_check_path_name("a task", name);
    smith_context = smith_text("task %s", name);
    smith_check_task(step);
    SmithStep copy = smith_copy_step(step);
    copy.outputs = smith_one(smith_text(".smith/task/%s", name));
    SmithEntry *entry = smith_find_named(name);
    if (entry != nullptr) { // a task declared again, identical, changes nothing
        if (entry->kind != SmithEntryKind_Task) smith_fail("the name is already used by an alias");
        const char *field = smith_step_difference(&entry->step, &copy);
        if (field != nullptr) smith_fail("the task is already declared and its %s differs", field);
        *step = entry->step;
        step->outputs = smith_one(entry->name);
        smith_context = nullptr;
        return;
    }
    entry = smith_add_entry(SmithEntryKind_Task);
    entry->name = smith_copy_string(name);
    entry->step = copy;
    copy.outputs = smith_one(entry->name);
    *step = copy;
    smith_context = nullptr;
}

void smith_alias(SmithBuild *b, const char *name, const char *const *targets) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    smith_check_path_name("an alias", name);
    smith_context = smith_text("alias %s", name);
    if (targets == nullptr) smith_fail("the targets list is nullptr; use an empty list");
    SmithEntry *entry = smith_find_named(name);
    if (entry != nullptr) { // an alias declared again, with the same targets, changes nothing
        if (entry->kind != SmithEntryKind_Alias) smith_fail("the name is already used by a task");
        if (!smith_same_list(entry->targets, targets))
            smith_fail("the alias is already declared and its targets differ");
        smith_context = nullptr;
        return;
    }
    entry = smith_add_entry(SmithEntryKind_Alias);
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

// Declares an option, or finds it again: the same name and kind reads the same value, so a callback that runs later can
// read what build() read. The other kind is an error. *fresh tells the first declaration.
static SmithDeclared *smith_declare(const char *name, const char *description, bool flag, bool *fresh) {
    smith_context = nullptr;
    if (name == nullptr || *name == '\0' || strchr(name, '=') != nullptr)
        smith_fail("an option needs a name without '='");
    *fresh = false;
    for (size_t i = 0; i < smith_declared_count; i++) {
        if (strcmp(smith_declared[i].name, name) != 0) continue;
        if (smith_declared[i].flag != flag)
            smith_fail("option '%s' is declared as %s and read as %s", name, flag ? "a value" : "a flag",
                       flag ? "a flag" : "a value");
        return &smith_declared[i];
    }
    if (smith_declared_count >= smith_options_max) smith_too_many("options", smith_options_max);
    *fresh = true;
    SmithDeclared *declared = &smith_declared[smith_declared_count++];
    *declared = (SmithDeclared){
        .name = smith_copy_string(name),
        .description = description != nullptr ? smith_copy_string(description) : "",
        .flag = flag,
    };
    return declared;
}

bool smith_option(SmithBuild *b, const char *name, const char *description) {
    SMITH_ASSERT(b != nullptr);
    bool fresh = false;
    SmithDeclared *declared = smith_declare(name, description, true, &fresh);
    if (!fresh) return declared->value != nullptr;
    const SmithGiven *given = smith_find_given(name);
    if (given == nullptr) return false;
    if (given->value != nullptr) smith_fail("-D%s takes no value; write -D%s", name, name);
    declared->value = "";
    return true;
}

const char *smith_option_value(SmithBuild *b, const char *name, const char *fallback, const char *description) {
    SMITH_ASSERT(b != nullptr);
    bool fresh = false;
    SmithDeclared *declared = smith_declare(name, description, false, &fresh);
    if (!fresh) return declared->value;
    const SmithGiven *given = smith_find_given(name);
    if (given != nullptr && given->value == nullptr) smith_fail("-D%s needs a value; write -D%s=value", name, name);
    declared->value = given != nullptr ? given->value : fallback;
    return declared->value;
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

// The union of the matches of every pattern, sorted by strcmp (not the locale), each name once: make's
// $(sort $(wildcard a b c)).
const char *const *smith_glob(SmithBuild *b, const char *const *patterns) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (patterns == nullptr) smith_fail("smith_glob: the patterns list is nullptr; use an empty list");
    glob_t found = {};
    size_t count = smith_count(patterns);
    for (size_t i = 0; i < count; i++) {
        int status = glob(patterns[i], i > 0 ? smith_glob_next : smith_glob_first, nullptr, &found);
        if (status != 0 && status != GLOB_NOMATCH) smith_fail("glob '%s' failed", patterns[i]);
    }
    size_t matches = count > 0 ? (size_t)found.gl_pathc : 0;
    if (matches > smith_list_max) smith_too_many("matches in smith_glob", smith_list_max);
    if (matches > 1) qsort((void *)found.gl_pathv, matches, sizeof *found.gl_pathv, smith_compare_strings);
    const char **list = smith_new_list(matches);
    size_t used = 0;
    for (size_t i = 0; i < matches; i++) {
        if (used > 0 && strcmp(list[used - 1], found.gl_pathv[i]) == 0) continue;
        list[used++] = smith_copy_string(found.gl_pathv[i]);
    }
    list[used] = nullptr;
    if (count > 0) globfree(&found);
    return list;
}

// Waits for the program; the raw wait status.
static int smith_wait(const char *name, pid_t pid) {
    int status = 0;
    int waited = -1;
    for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
        waited = waitpid(pid, &status, 0);
    if (waited < 0) smith_fail("%s: cannot wait for it", name);
    return status;
}

static void smith_require_success(const char *name, int status) {
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

// Starts the program with its stdout in the pipe, and its stderr on /dev/null when quiet. Returns -1 and the error when
// it cannot start.
static pid_t smith_spawn_into(const char *const *argv, int pipe_fds[2], bool quiet, int *error) {
    posix_spawn_file_actions_t actions;
    int setup = posix_spawn_file_actions_init(&actions);
    if (setup == 0) setup = posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    if (setup == 0) setup = posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    if (setup == 0 && pipe_fds[1] != STDOUT_FILENO) setup = posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
    if (setup == 0 && quiet)
        setup = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    SMITH_ASSERT(setup == 0);
    pid_t pid = 0;
    int status = posix_spawnp(&pid, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);
    if (status == 0) return pid;
    close(pipe_fds[0]);
    *error = status;
    return -1;
}

// Runs the program and returns its stdout without the last newline. With a fallback, a program that cannot start,
// exits non-zero or is killed gives the fallback, and its stderr is discarded. Without one, any of those is an error.
static const char *smith_capture_with(const char *const *argv, const char *fallback, bool has_fallback) {
    if (argv == nullptr || argv[0] == nullptr) smith_fail("smith_capture needs a command");
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) smith_fail("%s: cannot make a pipe", argv[0]);
    int error = 0;
    pid_t pid = smith_spawn_into(argv, pipe_fds, has_fallback, &error);
    if (pid < 0) {
        if (has_fallback) return fallback;
        smith_fail("%s failed (cannot run: %s)", argv[0], strerror(error));
    }
    size_t length = smith_read_all(argv[0], pipe_fds[0]);
    close(pipe_fds[0]);
    int status = smith_wait(argv[0], pid);
    if (has_fallback && !(WIFEXITED(status) && WEXITSTATUS(status) == 0)) return fallback;
    smith_require_success(argv[0], status);
    if (length > 0 && smith_capture_buffer[length - 1] == '\n') length -= 1;
    char *text = smith_alloc(length + 1);
    memcpy(text, smith_capture_buffer, length);
    text[length] = '\0';
    return text;
}

const char *smith_capture(SmithBuild *b, const char *const *argv) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    return smith_capture_with(argv, nullptr, false);
}

const char *smith_capture_or(SmithBuild *b, const char *const *argv, const char *fallback) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    return smith_capture_with(argv, fallback, true);
}

// Files that build.c computes and the manifest refers to.

// Whether the file holds exactly these bytes.
static bool smith_file_holds(const char *path, const char *text, size_t length) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    struct stat info;
    bool same = fstat(fileno(file), &info) == 0 && S_ISREG(info.st_mode) && (uintmax_t)info.st_size == length;
    for (size_t done = 0; same && done < length;) { // bounded: each read moves at least a byte, or ends the loop
        size_t want = length - done < smith_chunk ? length - done : smith_chunk;
        size_t got = fread(smith_compare_a, 1, want, file);
        same = got == want && memcmp(smith_compare_a, text + done, got) == 0;
        done += got;
    }
    fclose(file);
    return same;
}

static void smith_make_parents(const char *path) {
    char folder[PATH_MAX];
    if (strlen(path) >= sizeof folder) smith_fail("the path '%s' is too long", path);
    memcpy(folder, path, strlen(path) + 1);
    for (char *c = folder + 1; *c != '\0'; c++) { // bounded by the path's length
        if (*c != '/') continue;
        *c = '\0';
        struct stat info;
        if (mkdir(folder, 0777) != 0 && (stat(folder, &info) != 0 || !S_ISDIR(info.st_mode)))
            smith_fail("cannot make the folder %s", folder);
        *c = '/';
    }
}

// Writes the file through a temporary file and a rename, only when its bytes differ, so its mtime moves only when its
// content does.
static void smith_write_if_different(const char *path, const char *text, size_t length) {
    if (smith_file_holds(path, text, length)) return;
    smith_make_parents(path);
    const char *temporary = smith_text("%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (file == nullptr) smith_fail("cannot write %s", temporary);
    if (fwrite(text, 1, length, file) != length || fclose(file) != 0) smith_fail("cannot write %s", temporary);
    if (rename(temporary, path) != 0) smith_fail("cannot replace %s", path);
}

// A relative path with no '..' component, and nothing under .smith/, which is smith's own folder.
static void smith_check_write_path(const char *path) {
    if (path == nullptr || *path == '\0') smith_fail("smith_write needs a path");
    if (*path == '/') smith_fail("smith_write: the path '%s' must be relative", path);
    if (path[strlen(path) - 1] == '/') smith_fail("smith_write: the path '%s' names a folder", path);
    const char *start = path;
    bool first = true;
    for (const char *c = path;; c++) {
        if (*c != '/' && *c != '\0') continue;
        size_t length = (size_t)(c - start);
        if (length == 2 && start[0] == '.' && start[1] == '.')
            smith_fail("smith_write: the path '%s' holds a '..' component", path);
        if (first && length == 6 && strncmp(start, ".smith", 6) == 0)
            smith_fail("smith_write: the path '%s' is under .smith/, which is smith's own", path);
        if (length > 0 && !(length == 1 && start[0] == '.')) first = false;
        if (*c == '\0') break;
        start = c + 1;
    }
}

void smith_write(SmithBuild *b, const char *path, const char *text) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    smith_check_write_path(path);
    if (text == nullptr) smith_fail("smith_write: the text of '%s' is nullptr", path);
    if (smith_run_mode && !smith_in_run) return; // the replay only records; the manifest run already wrote it
    smith_write_if_different(path, text, strlen(text));
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

// Splits the text, which ends with a 0 byte after size bytes, into lines in place.
static const char *const *smith_split_lines(char *text, size_t size) {
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

const char *const *smith_lines(SmithBuild *b, const char *path) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (path == nullptr) smith_fail("smith_lines needs a path");
    char *text = nullptr;
    size_t size = smith_read_file(path, &text);
    return smith_split_lines(text, size);
}

// Runs a program with its stdout captured and its stderr passed through. The result is its exit status, 128 plus the
// signal that killed it, or 127 when it cannot start. *lines is its stdout as lines, or nullptr when it did not start.
static int smith_run_program(const char *const *argv, const char *const **lines) {
    smith_context = nullptr;
    if (argv == nullptr || argv[0] == nullptr) smith_fail("a run needs a command");
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) smith_fail("%s: cannot make a pipe", argv[0]);
    int error = 0;
    pid_t pid = smith_spawn_into(argv, pipe_fds, false, &error);
    if (lines != nullptr) *lines = nullptr;
    if (pid < 0) return smith_exit_not_started;
    size_t length = smith_read_all(argv[0], pipe_fds[0]);
    close(pipe_fds[0]);
    int status = smith_wait(argv[0], pid);
    if (lines != nullptr) {
        char *text = smith_alloc(length + 1);
        memcpy(text, smith_capture_buffer, length);
        text[length] = '\0';
        *lines = smith_split_lines(text, length);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

const char *const *smith_run_lines(SmithRun *run, const char *const *argv) {
    SMITH_ASSERT(run != nullptr);
    const char *const *lines = nullptr;
    int status = smith_run_program(argv, &lines);
    if (status == 0) return lines;
    fprintf(stderr, "smith: %s failed (status %d)\n", argv[0], status);
    return nullptr;
}

int smith_run_status(SmithRun *run, const char *const *argv, const char *const **lines) {
    SMITH_ASSERT(run != nullptr);
    return smith_run_program(argv, lines);
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

// A word for sh. For ninja's own text (ninja) every $ is doubled, and a newline is an error because ninja cannot hold
// one; for a script file neither applies.
static void smith_put_quoted(FILE *out, const char *word, bool ninja) {
    if (ninja && strchr(word, '\n') != nullptr) smith_fail("the command word '%s' holds a newline", word);
    if (smith_is_safe_word(word)) {
        fputs(word, out);
        return;
    }
    fputc('\'', out);
    for (const char *c = word; *c != '\0'; c++) {
        if (*c == '\'')
            fputs("'\\''", out);
        else if (*c == '$' && ninja)
            fputs("$$", out);
        else
            fputc(*c, out);
    }
    fputc('\'', out);
}

static void smith_put_quoted_list(FILE *out, const char *const *words, bool ninja) {
    for (size_t i = 0; words[i] != nullptr; i++) {
        if (i > 0) fputc(' ', out);
        smith_put_quoted(out, words[i], ninja);
    }
}

// A command word. The words $in and $out stand for the step's inputs and outputs; every other word is quoted for the
// shell. ninja evaluates an edge's own variable (cmd) without the edge's $in and $out, so the paths are written here,
// quoted the same way, instead of being left to ninja.
static void smith_put_word(FILE *out, const SmithStep *step, const char *word) {
    if (strcmp(word, "$in") == 0) {
        smith_put_quoted_list(out, step->inputs, false);
    } else if (strcmp(word, "$out") == 0) {
        smith_put_quoted_list(out, step->outputs, false);
    } else {
        smith_put_quoted(out, word, false);
    }
}

// The command line as sh reads it: the words, then the stamp's touch. It is built in a fixed buffer and ends with a
// 0 byte; *length is its size.
static const char *smith_render_command(const SmithEntry *entry, size_t *length) {
    const SmithStep *step = &entry->step;
    FILE *file = fmemopen(smith_command_buffer, smith_command_max, "w");
    if (file == nullptr) smith_fail("cannot render the command");
    if (step->run != nullptr) {
        fputs(".smith/program --run ", file);
        smith_put_quoted(file, step->outputs[0], false);
        for (size_t i = 0; i < smith_option_arg_count; i++) {
            fputc(' ', file);
            smith_put_quoted(file, smith_option_args[i], false);
        }
    }
    for (size_t i = 0; step->run == nullptr && step->command[i] != nullptr; i++) {
        if (i > 0) fputc(' ', file);
        smith_put_word(file, step, step->command[i]);
    }
    if (entry->stamp) {
        fputs(" && touch ", file);
        smith_put_quoted_list(file, step->outputs, false);
    }
    long size = fflush(file) == 0 ? ftell(file) : -1;
    fclose(file);
    if (size < 0 || (size_t)size >= smith_command_max)
        smith_fail("the command is larger than %zu bytes", smith_command_max);
    *length = (size_t)size;
    smith_command_buffer[size] = '\0';
    return smith_command_buffer;
}

// The edge's cmd. A command that holds a newline cannot be one line of ninja, so the whole command line goes into
// .smith/command/<FNV-1a 64 of its text> and cmd runs that file with sh. The name changes with the text, so ninja
// reruns the step when the command changes. The file is read by sh, not ninja, so its $ are not doubled.
static void smith_put_command(FILE *out, const SmithEntry *entry) {
    size_t length = 0;
    const char *text = smith_render_command(entry, &length);
    fputs("  cmd = ", out);
    if (memchr(text, '\n', length) == nullptr) {
        for (size_t i = 0; i < length; i++) {
            if (text[i] == '$') fputc('$', out);
            fputc(text[i], out);
        }
        fputc('\n', out);
        return;
    }
    const char *path = smith_text(".smith/command/%016" PRIx64, smith_hash_bytes(text, length));
    smith_command_buffer[length] = '\n'; // the file ends its last line
    smith_write_if_different(path, text, length + 1);
    fprintf(out, "sh %s\n", path);
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
    if (smith_count(step->implicit_inputs) > 0 || step->always || step->run != nullptr) fputs(" |", out);
    smith_put_paths(out, step->implicit_inputs);
    if (step->run != nullptr) fputs(" .smith/program", out); // the function's code is part of the program
    if (step->always) fputs(" smith_always", out);
    if (smith_count(step->order_only_inputs) > 0) fputs(" ||", out);
    smith_put_paths(out, step->order_only_inputs);
    fputc('\n', out);
    smith_put_command(out, entry);
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
        smith_put_quoted(out, smith_option_args[i], true);
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

// --run <output>: the build has been replayed, so the step that makes the output is in the table; call its function.
static int smith_run_step(SmithBuild *b, const char *output) {
    const SmithEntry *found = nullptr;
    for (size_t i = 0; i < smith_entry_count && found == nullptr; i++) {
        const SmithEntry *entry = &smith_entries[i];
        if (entry->kind != SmithEntryKind_Alias && strcmp(entry->step.outputs[0], output) == 0) found = entry;
    }
    SMITH_ASSERT(found != nullptr && found->step.run != nullptr); // smith wrote the name
    static const char *const none[] = {nullptr};
    SmithRun run = {
        .b = b,
        .inputs = found->step.inputs,
        .outputs = found->kind == SmithEntryKind_Task ? none : found->step.outputs,
        .args = found->step.args,
    };
    smith_in_run = true;
    int status = found->step.run(&run);
    smith_in_run = false;
    smith_context = nullptr;
    return status;
}

int main(int argc, char **argv) {
    SMITH_ASSERT(argc >= 2 && argv[1] != nullptr);
    smith_regenerating = strcmp(argv[1], "--regenerate") == 0;
    smith_run_mode = strcmp(argv[1], "--run") == 0;
    int first =
        smith_regenerating || smith_run_mode ? 2 : 1; // the manifest or the output comes next, then -D arguments
    SMITH_ASSERT(argc > first && argv[first] != nullptr);
    smith_parse_options(argc - first - 1, argv + first + 1);
    SmithBuild b = smith_make_build();
    build(&b);
    smith_context = nullptr;
    smith_run_defers(&b);
    if (smith_run_mode) return smith_run_step(&b, argv[first]);
    // A -D that only a pending smith_defer callback reads is not known yet; stage 2 checks it.
    if (!smith_any_pending()) smith_check_given();
    if (!smith_default_called && !smith_any_pending()) smith_fail("no default; call smith_default");
    smith_write_manifest(argv[first]);
    return 0;
}
