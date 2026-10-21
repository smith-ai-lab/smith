// The build program's runtime. `smith build` compiles a project's build.c together with this file, runs the result, and
// the result writes .smith/build.ninja. Strings and lists live in a static arena (no malloc); the manifest is
// written only when its bytes change, so ninja's view of it changes only when the build does. See docs/build-c.md.
#include <smith/build.h>

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
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
constexpr size_t smith_glob_depth_max = 64;
constexpr size_t smith_glob_parts_max = 128;

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

[[noreturn]] static void smith_assert_fail(const char *condition, const char *file, int line) {
    fprintf(stderr, "smith: assertion failed: %s (%s:%d)\n", condition, file, line);
    fflush(stderr);
    abort();
}

// The package cache and SHA-256, which smith's `smith fetch` shares. smith writes it next to this file.
#include "package.c"

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

static void smith_check_stamp(const SmithStep *step) {
    if (step->stamp == nullptr) return;
    if (*step->stamp == '\0') smith_fail(".stamp is empty");
    size_t count = smith_count(step->outputs);
    if (count > 1 || (count == 1 && strcmp(step->outputs[0], step->stamp) != 0))
        smith_fail(".stamp is the step's output; give .stamp or .outputs, not both");
}

static void smith_check_step(const SmithStep *step) {
    smith_check_stamp(step);
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
    if (step->stamp != nullptr) copy.stamp = smith_copy_string(step->stamp);
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
    const char *first = smith_count(step->outputs) > 0 ? step->outputs[0] : step->stamp;
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
    if (!smith_same_text(left->stamp, right->stamp)) return ".stamp";
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
    bool stamp = smith_count(copy.outputs) == 0 || copy.stamp != nullptr;
    if (copy.stamp != nullptr)
        copy.outputs = smith_one(copy.stamp);
    else if (stamp)
        copy.outputs = smith_one(smith_text(".smith/stamp/%016" PRIx64, smith_hash_step(&copy)));
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
    if (step->stamp != nullptr) smith_fail("a task has no outputs, so .stamp means nothing; use smith_step");
    if (step->run != nullptr && step->depfile != nullptr)
        smith_fail("a .run task has no output for its .depfile to name; use smith_step");
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

// The arguments `smith run` and `smith test` were given, which smith passes as the one option SMITH_ARGS: each argument
// after a ";", with "%", ";" and newline as %25, %3B and %0A (smith_encode_argument in smith.c). A list, empty for
// none.
const char *const *smith_args(SmithBuild *b) {
    SMITH_ASSERT(b != nullptr);
    const char *text = smith_option_value(b, "SMITH_ARGS", "", "the arguments of smith run and smith test");
    size_t count = 0;
    for (const char *c = text; *c != '\0'; c++)
        count += *c == ';' ? 1 : 0;
    const char **list = smith_new_list(count);
    for (size_t i = 0; i < count; i++) {
        text++; // the ";"
        size_t length = strcspn(text, ";");
        char *argument = smith_alloc(length + 1);
        size_t used = 0;
        for (size_t k = 0; k < length; k++) {
            if (text[k] == '%' && k + 2 < length) {
                argument[used++] = (char)strtol(smith_text("%.2s", text + k + 1), nullptr, 16);
                k += 2;
            } else {
                argument[used++] = text[k];
            }
        }
        argument[used] = '\0';
        list[i] = argument;
        text += length;
    }
    return list;
}

static void smith_check_given(void) {
    for (size_t i = 0; i < smith_given_count; i++) {
        bool known = false;
        for (size_t j = 0; j < smith_declared_count; j++)
            known = known || strcmp(smith_declared[j].name, smith_given[i].name) == 0;
        if (known) continue;
        if (strcmp(smith_given[i].name, "SMITH_ARGS") == 0) {
            fputs("smith: build.c: this verb was given arguments, but build.c does not read them; "
                  "smith_args(b) gives them to a task\n",
                  stderr);
            exit(smith_exit_usage);
        }
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
// $(sort $(wildcard a b c)). A pattern is walked one component at a time with an explicit stack and no recursion. A
// `**` component matches zero or more folders (at most smith_glob_depth_max deep; symbolic links are not followed), and
// every other component goes through fnmatch. A hidden entry matches only a component that starts with '.', as in the
// shell.

typedef struct {
    const char *path; // what the components before `part` matched; "" at the start
    size_t part;      // the next component to match
    size_t depth;     // the folders `**` has entered
} SmithGlobState;

typedef struct {
    const char *text; // not 0-terminated
    size_t length;
} SmithGlobPart;

static const char *smith_glob_names[smith_list_max];
static size_t smith_glob_name_count;
static SmithGlobState smith_glob_stack[smith_entries_max];
static size_t smith_glob_stack_count;
static SmithGlobPart smith_glob_parts[smith_glob_parts_max];
static size_t smith_glob_part_count;

static void smith_glob_push(const char *path, size_t part, size_t depth) {
    if (smith_glob_stack_count >= smith_entries_max) smith_too_many("pending folders in smith_glob", smith_entries_max);
    smith_glob_stack[smith_glob_stack_count++] = (SmithGlobState){.path = path, .part = part, .depth = depth};
}

static const char *smith_glob_join(const char *path, const char *name) {
    if (*path == '\0') return smith_copy_string(name);
    bool slash = path[strlen(path) - 1] == '/';
    return smith_text("%s%s%s", path, slash ? "" : "/", name);
}

// Splits the pattern at '/' into smith_glob_parts, dropping empty components.
static void smith_glob_split(const char *pattern) {
    smith_glob_part_count = 0;
    const char *c = pattern;
    for (size_t i = 0; *c != '\0' && i <= strlen(pattern); i++) { // bounded by the pattern's length
        const char *end = strchr(c, '/');
        size_t length = end != nullptr ? (size_t)(end - c) : strlen(c);
        if (length > 0) {
            if (smith_glob_part_count >= smith_glob_parts_max)
                smith_fail("smith_glob: '%s' has too many components", pattern);
            smith_glob_parts[smith_glob_part_count++] = (SmithGlobPart){.text = c, .length = length};
        }
        c += length;
        if (*c == '/') c++;
    }
}

static const char *smith_glob_part_text(size_t part) {
    char *copy = smith_alloc(smith_glob_parts[part].length + 1);
    memcpy(copy, smith_glob_parts[part].text, smith_glob_parts[part].length);
    copy[smith_glob_parts[part].length] = '\0';
    return copy;
}

static void smith_glob_add(const char *path) {
    if (*path == '\0') return;
    if (smith_glob_name_count >= smith_list_max) smith_too_many("matches in smith_glob", smith_list_max);
    smith_glob_names[smith_glob_name_count++] = path;
}

// A literal component names one entry, so it is checked, not searched for.
static void smith_glob_literal(const SmithGlobState *state, const char *name) {
    const char *path = smith_glob_join(state->path, name);
    struct stat info;
    if (lstat(path, &info) == 0) smith_glob_push(path, state->part + 1, state->depth);
}

// Reads the folder for a wildcard component, or, for `**`, for the folders to enter.
static void smith_glob_read(const SmithGlobState *state, const char *name, const char *pattern) {
    bool deeper = strcmp(name, "**") == 0;
    DIR *folder = opendir(*state->path == '\0' ? "." : state->path);
    if (folder == nullptr) return;
    for (struct dirent *entry = readdir(folder); entry != nullptr; entry = readdir(folder)) { // ends with the folder
        const char *entry_name = entry->d_name;
        if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) continue;
        if (entry_name[0] == '.' && name[0] != '.') continue;
        if (!deeper && fnmatch(name, entry_name, 0) != 0) continue;
        const char *path = smith_glob_join(state->path, entry_name);
        struct stat info;
        if (deeper && (lstat(path, &info) != 0 || !S_ISDIR(info.st_mode))) continue;
        if (deeper && state->depth >= smith_glob_depth_max)
            smith_fail("smith_glob: '%s' goes deeper than %zu folders", pattern, smith_glob_depth_max);
        smith_glob_push(path, deeper ? state->part : state->part + 1, state->depth + (deeper ? 1 : 0));
    }
    closedir(folder);
}

// Walks one pattern; its matches are appended to smith_glob_names.
static void smith_glob_walk(const char *pattern) {
    smith_glob_split(pattern);
    smith_glob_stack_count = 0;
    smith_glob_push(pattern[0] == '/' ? "/" : "", 0, 0);
    for (size_t steps = 0; smith_glob_stack_count > 0; steps++) { // bounded: the stack, the depth and the arena are
        if (steps >= (size_t)1 << 26) smith_too_many("steps in smith_glob", (size_t)1 << 26);
        SmithGlobState state = smith_glob_stack[--smith_glob_stack_count];
        if (state.part == smith_glob_part_count) {
            smith_glob_add(state.path);
            continue;
        }
        const char *name = smith_glob_part_text(state.part);
        if (strcmp(name, "**") == 0) smith_glob_push(state.path, state.part + 1, state.depth);
        if (strpbrk(name, "*?[\\") != nullptr)
            smith_glob_read(&state, name, pattern);
        else
            smith_glob_literal(&state, name);
    }
}

const char *const *smith_glob(SmithBuild *b, const char *const *patterns) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (patterns == nullptr) smith_fail("smith_glob: the patterns list is nullptr; use an empty list");
    smith_glob_name_count = 0;
    for (size_t i = 0; patterns[i] != nullptr; i++) {
        SMITH_ASSERT(i < smith_list_max);
        smith_glob_walk(patterns[i]);
    }
    size_t matches = smith_glob_name_count;
    if (matches > 1) qsort((void *)smith_glob_names, matches, sizeof *smith_glob_names, smith_compare_strings);
    const char **list = smith_new_list(matches);
    size_t used = 0;
    for (size_t i = 0; i < matches; i++) {
        if (used > 0 && strcmp(list[used - 1], smith_glob_names[i]) == 0) continue;
        list[used++] = smith_glob_names[i];
    }
    list[used] = nullptr;
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
// it cannot start. With combined, its stderr goes into the same pipe as its stdout.
static pid_t smith_spawn_into(const char *const *argv, int pipe_fds[2], bool quiet, bool combined, int *error) {
    posix_spawn_file_actions_t actions;
    int setup = posix_spawn_file_actions_init(&actions);
    if (setup == 0) setup = posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    if (setup == 0 && combined) setup = posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
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
    pid_t pid = smith_spawn_into(argv, pipe_fds, has_fallback, false, &error);
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

// What a compiler is. ninja decides that an object is out of date from its command, its inputs and their mtimes, and a
// new compiler changes none of them when the command says `smith`: the objects look up to date, though the old
// compiler built them (and sanitized objects of two compilers do not link together). So each object and program depends
// on a file in .smith/compiler/ that says which compiler it is and is rewritten, so that its mtime moves, only when
// that changes: ninja then rebuilds every object that compiler built, and a build with the same compiler does nothing.
// A compiler is its executable (path, size and mtime); smith is the clang it runs (see smith_compiler_identity), so a
// new smith with the same LLVM rebuilds nothing. The files are written by every run of this
// program, and `smith build` runs it before ninja each time.
constexpr size_t smith_compilers_max = 64;
static const char *smith_compilers[smith_compilers_max];
static size_t smith_compiler_count;

// The file that holds the identity of this compiler, and a note to write it.
static const char *smith_compiler_file(const char *compiler) {
    bool known = false;
    for (size_t i = 0; i < smith_compiler_count; i++)
        known = known || strcmp(smith_compilers[i], compiler) == 0;
    if (!known) {
        if (smith_compiler_count >= smith_compilers_max) smith_too_many("compilers", smith_compilers_max);
        smith_compilers[smith_compiler_count++] = compiler;
    }
    return smith_text(".smith/compiler/%016" PRIx64, smith_hash_bytes(compiler, strlen(compiler)));
}

// The real path of an executable named by a path, or found on PATH; nullptr when there is none.
static const char *smith_resolve_executable(const char *name) {
    char found[PATH_MAX];
    char real[PATH_MAX];
    if (strchr(name, '/') != nullptr) {
        if (realpath(name, real) == nullptr) return nullptr;
        return smith_copy_string(real);
    }
    const char *path = getenv("PATH");
    if (path == nullptr) return nullptr;
    for (const char *start = path; *start != '\0' || start == path;) { // bounded by the length of PATH
        const char *end = strchr(start, ':');
        size_t length = end != nullptr ? (size_t)(end - start) : strlen(start);
        int written = snprintf(found, sizeof found, "%.*s/%s", (int)length, length > 0 ? start : ".", name);
        if (written > 0 && (size_t)written < sizeof found && access(found, X_OK) == 0 &&
            realpath(found, real) != nullptr)
            return smith_copy_string(real);
        if (end == nullptr) break;
        start = end + 1;
    }
    return nullptr;
}

// "<real path> <size> <mtime>" for the file at the path, or "<path> missing".
static const char *smith_file_identity(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return smith_text("%s missing", path);
#ifdef __APPLE__
    struct timespec modified = info.st_mtimespec;
#else
    struct timespec modified = info.st_mtim;
#endif
    return smith_text("%s %lld %lld.%09ld", path, (long long)info.st_size, (long long)modified.tv_sec,
                      (long)modified.tv_nsec);
}

// What the compiler is. A compiler that is not smith is its executable (real path, size and mtime). Smith is not its
// own file, which changes with every build of smith, but the clang it runs, as `<smith> --smith-which -c a.c` says:
// with clang linked in, "clang in-process <clang>" and "identity <the version of that LLVM and a hash of what is
// linked>", so a smith rebuilt against the same LLVM is the same compiler; with an external clang ($SMITH_CLANG, or a
// stage0's), "clang <path>", so it is that clang's file. Anything else that is named smith is taken for a compiler like
// another.
static const char *smith_compiler_identity(const char *real) {
    const char *base = strrchr(real, '/');
    base = base != nullptr ? base + 1 : real;
    const char *own = smith_file_identity(real);
    if (strncmp(base, "smith", 5) != 0) return own;
    const char *text = smith_capture_with((const char *[]){real, "--smith-which", "-c", "a.c", nullptr}, "", true);
    if (strncmp(text, "clang in-process ", 17) == 0) {
        const char *identity = strstr(text, "\nidentity ");
        return identity != nullptr ? smith_text("in-process %s", identity + 10) : own;
    }
    if (strncmp(text, "clang /", 7) == 0 && strchr(text, '\n') == nullptr)
        return smith_text("external %s", smith_file_identity(text + 6));
    return own;
}

static void smith_write_compiler_files(void) {
    for (size_t i = 0; i < smith_compiler_count; i++) {
        const char *compiler = smith_compilers[i];
        const char *real = smith_resolve_executable(compiler);
        const char *text =
            smith_text("%s\n%s\n", compiler, real != nullptr ? smith_compiler_identity(real) : "missing");
        smith_write_if_different(smith_compiler_file(compiler), text, strlen(text));
    }
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

const char *smith_join(SmithBuild *b, const char *const *items, const char *separator) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (items == nullptr || separator == nullptr) smith_fail("smith_join needs a list and a separator");
    size_t count = smith_count(items);
    size_t separator_length = strlen(separator);
    size_t total = 0;
    for (size_t i = 0; i < count; i++)
        total += strlen(items[i]) + (i > 0 ? separator_length : 0);
    char *text = smith_alloc(total + 1);
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0) memcpy(text + used, separator, separator_length);
        used += i > 0 ? separator_length : 0;
        size_t length = strlen(items[i]);
        memcpy(text + used, items[i], length);
        used += length;
    }
    SMITH_ASSERT(used == total);
    text[total] = '\0';
    return text;
}

const char *const *smith_list(SmithBuild *b, const char *first, ...) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    va_list args;
    va_start(args, first);
    size_t count = 0;
    for (const char *item = first; item != nullptr; item = va_arg(args, const char *)) {
        if (count >= smith_list_max) smith_too_many("entries in a list", smith_list_max);
        count++;
    }
    va_end(args);
    const char **list = smith_new_list(count);
    va_start(args, first);
    const char *item = first;
    for (size_t i = 0; i < count; i++) {
        list[i] = smith_copy_string(item);
        item = va_arg(args, const char *);
    }
    va_end(args);
    return list;
}

// The libraries smith writes into .smith/lib; the list is in smith.c's smith_libraries.
// smith writes every library it ships to .smith/lib/ before build.c runs, so that folder is the list: smith.c's table
// is the only one, and the two cannot disagree.
const char *smith_library(SmithBuild *b, const char *name) {
    SMITH_ASSERT(b != nullptr && name != nullptr);
    smith_context = nullptr;
    bool plain = *name != '\0';
    for (const char *c = name; *c != '\0'; c++) // bounded by the name's length
        plain = plain && ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_');
    const char *source = smith_fmt(b, ".smith/lib/%s.c", name);
    if (plain && access(source, R_OK) == 0) return source;
    const char *const *found = smith_glob(b, smith_list(b, ".smith/lib/*.c", nullptr));
    const char *const *names = smith_list(b, nullptr);
    for (size_t i = 0; found[i] != nullptr; i++) // bounded by the glob's result
        names = smith_cat(b, names,
                          smith_list(b, smith_replace_extension(b, found[i] + strlen(".smith/lib/"), ""), nullptr),
                          nullptr);
    smith_fail("smith_library: unknown library \"%s\"; known: %s", name, smith_join(b, names, ", "));
}

const char *smith_package(SmithBuild *b, const SmithPackage *package) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (package == nullptr || package->url == nullptr || package->url[0] == '\0') {
        smith_fail("smith_package: .url is required");
    }
    if (package->hash == nullptr)
        smith_fail("smith_package: %s: .hash is required; `smith fetch` prints it", package->url);
    bool offline = smith_option(b, "OFFLINE", "a package missing from the cache is an error instead of a fetch");
    char folder[PATH_MAX];
    char hash[smith_package_hash_size];
    SmithPackageError error;
    // A package already in the cache is only looked up; a fetch prints a line first, since a download can be slow.
    if (smith_package_ensure(nullptr, package->url, package->hash, true, folder, hash, &error)) {
        return smith_copy_string(folder);
    }
    if (offline) smith_fail("smith_package: %s", error.text);
    fprintf(stderr, "smith: fetching %s\n", package->url);
    fflush(stderr);
    SmithIo io = smith_io_make(0, nullptr);
    bool fetched = smith_package_ensure(&io, package->url, package->hash, false, folder, hash, &error);
    smith_io_close(&io);
    if (!fetched) smith_fail("smith_package: %s", error.text);
    return smith_copy_string(folder);
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

// High level: compiles and links that smith knows are compiles and links.

constexpr size_t smith_objects_max = 65536;

typedef struct {
    const SmithObject *address; // the caller's struct, so a copy that was never registered is found out
    SmithObject object;         // the arena copy; .step is the emitted step
    bool listed;                // false for an object declared again with the same output: one entry in the file
} SmithObjectRow;

static SmithObjectRow smith_object_rows[smith_objects_max];
static size_t smith_object_row_count;
static bool smith_compile_commands_called;

typedef struct {
    const char *source;
    const char *const *arguments; // compiler, flags, -c, source
} SmithCommandRow;

static SmithCommandRow smith_command_rows[smith_objects_max];
static size_t smith_command_row_count;

// The flags `smith build` compiles build.c with; smith.c's bootstrap manifest holds the same words (a test compares).
static const char *const smith_build_c_words[] = {"smith-cc", "-std=gnu23",     "-g", "-O0",     "-Wall", "-Wextra",
                                                  "-I",       ".smith/include", "-c", "build.c", nullptr};

// Clang modules (smith_modules): a module map per header folder, one module per header, and the flags that use them.

constexpr char smith_modules_library_folder[] = ".smith/include/smith";

static const char *const *smith_modules_flags; // nullptr while modules are off
static const char *smith_modules_names[smith_entries_max];
static size_t smith_modules_name_count;
static bool smith_modules_textual_used[smith_entries_max];

static bool smith_modules_is_word_byte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// "src/base/arena.h" gives src_base_arena; ".smith/include/smith/arena.h" gives smith_arena.
static const char *smith_modules_name(const char *path) {
    const char *skip = ".smith/include/";
    if (strncmp(path, skip, strlen(skip)) == 0) path += strlen(skip);
    size_t length = strlen(path);
    if (length > 2 && strcmp(path + length - 2, ".h") == 0) length -= 2;
    char *name = smith_alloc(length + 2);
    size_t used = 0;
    if (length > 0 && path[0] >= '0' && path[0] <= '9') name[used++] = '_';
    for (size_t i = 0; i < length; i++)
        name[used++] = smith_modules_is_word_byte(path[i]) ? path[i] : '_';
    name[used] = '\0';
    return name;
}

static void smith_modules_claim(const char *name, const char *path) {
    for (size_t i = 0; i < smith_modules_name_count; i++) {
        if (strcmp(smith_modules_names[i], name) == 0)
            smith_fail("smith_modules: %s and an earlier header are both the module '%s'; rename one", path, name);
    }
    if (smith_modules_name_count >= smith_entries_max) smith_too_many("modules", smith_entries_max);
    smith_modules_names[smith_modules_name_count++] = name;
}

static bool smith_modules_is_textual(const char *path, const char *const *textual) {
    for (size_t i = 0; textual[i] != nullptr; i++) {
        if (strcmp(textual[i], path) != 0) continue;
        smith_modules_textual_used[i] = true;
        return true;
    }
    return false;
}

// A folder as written in the project: no "./" in front, no "/" behind, relative, with no '..' or quote in it.
static const char *smith_modules_folder(const char *folder) {
    while (strncmp(folder, "./", 2) == 0)
        folder += 2;
    size_t length = strlen(folder);
    while (length > 0 && folder[length - 1] == '/')
        length--;
    const char *copy = smith_text("%.*s", (int)length, folder);
    if (length == 0 || strcmp(copy, ".") == 0 || copy[0] == '/' || strstr(copy, "..") != nullptr ||
        strpbrk(copy, "\"\\\n") != nullptr)
        smith_fail("smith_modules: the folder '%s' must be a relative folder below the project, not '.'", folder);
    return copy;
}

// The module map of one folder: `module <name> { header "<absolute path>" export * }` for each header under it, and
// the headers marked textual together in one module that makes them known without importing them.
static const char *smith_modules_map(SmithBuild *b, const char *folder, const char *const *textual) {
    const char *const *headers = smith_glob(b, smith_list(b, smith_text("%s/**/*.h", folder), nullptr));
    size_t count = smith_count(headers);
    if (count == 0) smith_fail("smith_modules: the folder '%s' holds no .h file", folder);
    const char **lines = smith_new_list(count + 2);
    const char **plain = smith_new_list(count);
    size_t used = 0;
    size_t plain_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (strpbrk(headers[i], "\"\\\n") != nullptr) smith_fail("smith_modules: odd characters in %s", headers[i]);
        if (smith_modules_is_textual(headers[i], textual)) {
            plain[plain_count++] = smith_text("  textual header \"%s/%s\"\n", b->root, headers[i]);
            continue;
        }
        const char *name = smith_modules_name(headers[i]);
        smith_modules_claim(name, headers[i]);
        lines[used++] = smith_text("module %s { header \"%s/%s\" export * }\n", name, b->root, headers[i]);
    }
    plain[plain_count] = nullptr;
    if (plain_count > 0) {
        const char *name = smith_text("%s_textual", smith_modules_name(folder));
        smith_modules_claim(name, folder);
        lines[used++] = smith_text("module %s {\n%s}\n", name, smith_join(b, plain, ""));
    }
    lines[used] = nullptr;
    return smith_join(b, lines, "");
}

// Writes .smith/modules/<name>.modulemap and returns the flag that loads it.
static const char *smith_modules_write(SmithBuild *b, const char *folder, const char *const *textual) {
    const char *text = smith_modules_map(b, folder, textual);
    const char *path = smith_text(".smith/modules/%s.modulemap", smith_modules_name(folder));
    smith_write_if_different(path, text, strlen(text));
    return smith_text("-fmodule-map-file=%s", path);
}

static void smith_modules_check_overlap(const char *const *folders, size_t count) {
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < i; j++) {
            const char *left = smith_modules_folder(folders[j]);
            const char *right = smith_modules_folder(folders[i]);
            size_t n = strlen(left) < strlen(right) ? strlen(left) : strlen(right);
            bool prefix = strncmp(left, right, n) == 0 && (left[n] == '/' || left[n] == '\0') &&
                          (right[n] == '/' || right[n] == '\0');
            if (prefix)
                smith_fail("smith_modules: '%s' and '%s' overlap; list a folder once, it covers its subfolders", left,
                           right);
        }
    }
}

void smith_modules(SmithBuild *b, const SmithModules *modules) {
    SMITH_ASSERT(b != nullptr && modules != nullptr);
    smith_context = nullptr;
    if (smith_modules_flags != nullptr) smith_fail("smith_modules is called twice");
    if (smith_object_row_count > 0) smith_fail("smith_modules is called after smith_object; call it first");
    const char *const *folders = smith_copy_list(modules->folders);
    const char *const *textual = smith_copy_list(modules->textual);
    size_t count = smith_count(folders);
    if (smith_count(textual) > smith_entries_max) smith_too_many("textual headers", smith_entries_max);
    smith_modules_check_overlap(folders, count);
    char cache[PATH_MAX];
    SmithPackageError error;
    if (!smith_cache_folder(SmithCache_Modules, cache, &error)) smith_fail("smith_modules: %s", error.text);
    const char **flags = smith_new_list(count + 5);
    flags[0] = "-fmodules";
    flags[1] = smith_text("-fmodules-cache-path=%s", cache); // one cache for every project and worktree
    // The pruning rule is the same as the package cache's, and written in every command: clang removes a module unused
    // for 31 days, and checks at most once a week.
    flags[2] = smith_text("-fmodules-prune-interval=%d", smith_cache_prune_interval);
    flags[3] = smith_text("-fmodules-prune-after=%d", smith_cache_prune_after);
    for (size_t i = 0; i < count; i++)
        flags[4 + i] = smith_modules_write(b, smith_modules_folder(folders[i]), textual);
    flags[4 + count] = smith_modules_write(b, smith_modules_library_folder, textual);
    for (size_t i = 0; textual[i] != nullptr; i++) {
        if (!smith_modules_textual_used[i])
            smith_fail("smith_modules: the textual header '%s' is not under any of the folders (spell it as a glob "
                       "finds it, such as src/a.h)",
                       textual[i]);
    }
    smith_modules_flags = (const char *const *)flags;
}

// Only C and Objective-C sources take the flags; .metal and C++ never do.
static const char *const *smith_modules_for(SmithBuild *b, const char *source) {
    size_t length = strlen(source);
    bool covered = length > 2 && (strcmp(source + length - 2, ".c") == 0 || strcmp(source + length - 2, ".m") == 0);
    return covered && smith_modules_flags != nullptr ? smith_modules_flags : smith_list(b, nullptr);
}

static void smith_require_text(const char *call, const char *field, const char *value) {
    smith_context = nullptr;
    if (value == nullptr || *value == '\0') smith_fail("%s: .%s is required", call, field);
}

// The program that compiles or links: smith is a set of verbs, so it is smith-cc or smith-c++, the links that act as
// `smith cc` and `smith c++`.
static void smith_require_program(const char *call, const char *field, const char *value) {
    smith_require_text(call, field, value);
    if (strcmp(value, "smith") == 0) {
        smith_fail("%s: .%s is \"smith\", which is not a compiler; use \"smith-cc\" or \"smith-c++\"", call, field);
    }
}

static SmithObjectRow *smith_find_row(const SmithObject *address, const char *output) {
    for (size_t i = 0; i < smith_object_row_count; i++) {
        SmithObjectRow *row = &smith_object_rows[i];
        if (row->address == address && row->object.output == output) return row;
    }
    return nullptr;
}

// A .smith compile finds the modules' headers there.
static const char *const *smith_lang_flags(SmithBuild *b, bool lang) {
    return lang ? smith_list(b, "-I", ".smith/include", nullptr) : smith_list(b, nullptr);
}

// .smith sources (docs/smith-lang.md). A module's generated header is a step of its own, made from the module's tokens
// alone (`smith-cc --smith-header`), so every header can be written before any compile, and every compile of a .smith
// source waits for all of them (an order-only input: the stamp of the check below) and gets `-I .smith/include`. What
// an importer depends on is its depfile: it lists the headers the compile read, and a header is rewritten only when
// its bytes change (restat), so an edit to a module's body recompiles that module alone, and an edit to its exports
// recompiles its importers. The check reads each header's `// smith-module:` and `// smith-imports:` lines and fails
// on an import cycle, naming it, before any compile starts.

constexpr size_t smith_lang_modules_max = 4096;
constexpr size_t smith_lang_name_max = 256;
constexpr char smith_lang_stamp[] = ".smith/imports.stamp";

static const char *smith_lang_module_sources[smith_lang_modules_max];
static const char *smith_lang_module_names[smith_lang_modules_max];
static const char *smith_lang_module_headers[smith_lang_modules_max];
static size_t smith_lang_module_count;
static bool smith_lang_sources_used; // some compile is of a .smith source

static bool smith_ends_with(const char *text, const char *suffix) {
    size_t length = strlen(text);
    size_t tail = strlen(suffix);
    return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

static bool smith_lang_word_byte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Past the whitespace and comments at text[at].
static size_t smith_lang_skip(const char *text, size_t length, size_t at) {
    for (size_t guard = 0; at < length && guard < length; guard++) { // each pass moves on
        if (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r') {
            at += 1;
        } else if (text[at] == '/' && at + 1 < length && text[at + 1] == '/') {
            while (at < length && text[at] != '\n')
                at += 1;
        } else if (text[at] == '/' && at + 1 < length && text[at + 1] == '*') {
            at += 2;
            while (at + 1 < length && !(text[at] == '*' && text[at + 1] == '/'))
                at += 1;
            at = at + 2 < length ? at + 2 : length;
        } else {
            break;
        }
    }
    return at;
}

// The word at text[*at], appended to name; false when there is none or the name is full.
static bool smith_lang_take_word(const char *text, size_t length, size_t *at, char *name, size_t *used) {
    size_t start = *at;
    while (*at < length && smith_lang_word_byte(text[*at]))
        *at += 1;
    if (*at == start || *used + (*at - start) + 2 >= smith_lang_name_max) return false;
    memcpy(name + *used, text + start, *at - start);
    *used += *at - start;
    return true;
}

// The name in the `module a::b;` line that starts a .smith file ("a::b"), or nullptr when the file is not a module (or
// cannot be read, or its line is malformed: the compile says so, in its own words).
static const char *smith_lang_module_of(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return nullptr;
    static char text[8192];
    size_t length = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    size_t at = smith_lang_skip(text, length, 0);
    if (at + 6 >= length || memcmp(text + at, "module", 6) != 0 || smith_lang_word_byte(text[at + 6])) return nullptr;
    at = smith_lang_skip(text, length, at + 6);
    char name[smith_lang_name_max];
    size_t used = 0;
    if (!smith_lang_take_word(text, length, &at, name, &used)) return nullptr;
    for (size_t guard = 0; guard < smith_lang_name_max; guard++) { // one `::` and word per pass
        at = smith_lang_skip(text, length, at);
        if (at + 1 >= length || text[at] != ':' || text[at + 1] != ':') break;
        at = smith_lang_skip(text, length, at + 2);
        name[used++] = ':';
        name[used++] = ':';
        if (!smith_lang_take_word(text, length, &at, name, &used)) return nullptr;
    }
    if (at >= length || text[at] != ';') return nullptr;
    name[used] = '\0';
    return smith_copy_string(name);
}

// "a::b" as "a/b".
static const char *smith_lang_slashed(const char *name) {
    char *copy = smith_alloc(strlen(name) + 1);
    size_t used = 0;
    for (size_t i = 0; name[i] != '\0'; i++) { // bounded by the name
        if (name[i] == ':' && name[i + 1] == ':') {
            copy[used++] = '/';
            i += 1;
        } else {
            copy[used++] = name[i];
        }
    }
    copy[used] = '\0';
    return copy;
}

// The module `source` declares, registered with its header's path in *header; nullptr for a file that is no module. The
// module's path rule is checked here, once: module a::b lives in a file whose path ends in a/b.smith.
static const char *smith_lang_register(const char *source, const char **header) {
    const char *name = smith_lang_module_of(source);
    if (name == nullptr) return nullptr;
    const char *path = smith_text("%s.smith", smith_lang_slashed(name));
    bool placed = strcmp(source, path) == 0 || smith_ends_with(source, smith_text("/%s", path));
    if (!placed) smith_fail("smith_object: %s declares module %s, so its path must end in %s", source, name, path);
    *header = smith_text(".smith/include/%s.h", smith_lang_slashed(name));
    for (size_t i = 0; i < smith_lang_module_count; i++) {
        if (strcmp(smith_lang_module_names[i], name) != 0) continue;
        if (strcmp(smith_lang_module_sources[i], source) != 0)
            smith_fail("smith_object: %s and %s both declare module %s", smith_lang_module_sources[i], source, name);
        return name; // the same module compiled again, with other flags
    }
    if (smith_lang_module_count >= smith_lang_modules_max) smith_too_many("modules", smith_lang_modules_max);
    smith_lang_module_sources[smith_lang_module_count] = source;
    smith_lang_module_names[smith_lang_module_count] = name;
    smith_lang_module_headers[smith_lang_module_count++] = *header;
    return name;
}

// The text after `prefix` in the first lines of a generated header, or nullptr.
static const char *smith_lang_header_line(const char *const *lines, const char *prefix) {
    for (size_t i = 0; lines[i] != nullptr && i < 8; i++) {
        if (strncmp(lines[i], prefix, strlen(prefix)) == 0) return lines[i] + strlen(prefix);
    }
    return nullptr;
}

typedef struct {
    const char *name;
    const char *const *imports; // names; the ones that are no module of this build are not edges
} SmithLangNode;

static int smith_lang_node_index(const SmithLangNode *nodes, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(nodes[i].name, name) == 0) return (int)i;
    }
    return -1;
}

// The words of text (separated by spaces) as a list.
static const char *const *smith_lang_words(const char *text) {
    size_t count = 0;
    for (const char *c = text; *c != '\0'; c++)
        count += *c != ' ' && (c == text || c[-1] == ' ');
    const char **words = smith_alloc((count + 1) * sizeof *words);
    size_t used = 0;
    for (const char *c = text; *c != '\0' && used < count;) { // bounded by the words
        while (*c == ' ')
            c += 1;
        size_t length = strcspn(c, " ");
        words[used++] = smith_text("%.*s", (int)length, c);
        c += length;
    }
    words[used] = nullptr;
    return words;
}

// Depth-first from `start` along the imports: `path` is the chain of modules being visited (state 1) and `edge` the
// next import to try of each (state 2 is done). Returns the length of a cycle found, which is path[*from ..], or 0.
static size_t smith_lang_walk(const SmithLangNode *nodes, size_t count, int start, int *state, int *path, size_t *edge,
                              size_t *from) {
    size_t depth = 0;
    path[0] = start;
    edge[0] = 0;
    state[start] = 1;
    for (size_t guard = 0; guard < count * (count + 2) + 2; guard++) { // each pass takes an edge or leaves a module
        const char *import = nodes[path[depth]].imports[edge[depth]];
        if (import == nullptr) {
            state[path[depth]] = 2;
            if (depth == 0) return 0;
            depth -= 1;
            continue;
        }
        edge[depth] += 1;
        int target = smith_lang_node_index(nodes, count, import);
        if (target < 0 || state[target] == 2) continue;
        if (state[target] == 1) {
            for (size_t i = 0; i <= depth; i++) {
                if (path[i] == target) *from = i;
            }
            return depth - *from + 1;
        }
        depth += 1;
        path[depth] = target;
        edge[depth] = 0;
        state[target] = 1;
    }
    return 0;
}

// The check step: fails, naming the cycle, when the imports in the headers have one.
static int smith_lang_check(SmithRun *run) {
    size_t count = smith_count(run->inputs);
    SmithLangNode *nodes = smith_alloc((count + 1) * sizeof *nodes);
    for (size_t i = 0; i < count; i++) {
        const char *const *lines = smith_lines(run->b, run->inputs[i]);
        const char *name = smith_lang_header_line(lines, "// smith-module: ");
        const char *imports = smith_lang_header_line(lines, "// smith-imports:");
        if (name == nullptr || imports == nullptr) {
            fprintf(stderr, "smith: %s is not a header smith wrote (it has no smith-module line)\n", run->inputs[i]);
            return 1;
        }
        nodes[i] = (SmithLangNode){.name = name, .imports = smith_lang_words(imports)};
    }
    int *state = smith_alloc((count + 1) * sizeof *state);
    int *path = smith_alloc((count + 1) * sizeof *path);
    size_t *edge = smith_alloc((count + 1) * sizeof *edge);
    for (size_t i = 0; i < count; i++) {
        size_t from = 0;
        if (state[i] != 0) continue;
        size_t length = smith_lang_walk(nodes, count, (int)i, state, path, edge, &from);
        if (length == 0) continue;
        fputs("smith: import cycle: ", stderr);
        for (size_t k = 0; k < length; k++)
            fprintf(stderr, "%s -> ", nodes[path[from + k]].name);
        fprintf(stderr, "%s\n", nodes[path[from]].name);
        return 1;
    }
    return 0;
}

// Declares the check once every smith_object is in: the compiles of .smith sources wait for it.
static void smith_lang_finish(SmithBuild *b) {
    if (!smith_lang_sources_used) return;
    const char **headers = smith_new_list(smith_lang_module_count);
    for (size_t i = 0; i < smith_lang_module_count; i++)
        headers[i] = smith_lang_module_headers[i];
    headers[smith_lang_module_count] = nullptr;
    SmithStep check = {
        .run = smith_lang_check,
        .inputs = (const char *const *)headers,
        .stamp = smith_lang_stamp,
        .description = "check the imports of the modules",
    };
    smith_step(b, &check);
}

// The header step of a module, and the flags and ordering its compile gets.
static void smith_lang_header_step(SmithBuild *b, const SmithObject *object, const char *header) {
    SmithStep step = {
        .command = smith_list(b, object->compiler, "--smith-header", object->source, "-o", header, nullptr),
        .inputs = smith_one(object->source),
        .implicit_inputs = smith_one(smith_compiler_file(object->compiler)),
        .outputs = smith_one(header),
        .restat = true, // the header is written only when its bytes change
    };
    smith_step(b, &step);
}

void smith_object(SmithBuild *b, SmithObject *object) {
    SMITH_ASSERT(b != nullptr && object != nullptr);
    smith_require_program("smith_object", "compiler", object->compiler);
    smith_require_text("smith_object", "source", object->source);
    smith_require_text("smith_object", "output", object->output);
    SmithObject copy = *object;
    copy.compiler = smith_copy_string(object->compiler);
    copy.source = smith_copy_string(object->source);
    copy.output = smith_copy_string(object->output);
    copy.flags = smith_copy_list(object->flags);
    copy.implicit_inputs = smith_copy_list(object->implicit_inputs);
    const char *depfile = smith_replace_extension(b, copy.output, ".d");
    if (strcmp(depfile, copy.output) == 0) smith_fail("smith_object: the output '%s' already ends in .d", copy.output);
    const char *const tail[] = {"-MMD", "-MF", depfile, "-c", copy.source, "-o", copy.output, nullptr};
    bool lang = smith_ends_with(copy.source, ".smith");
    if (lang) {
        const char *header = nullptr;
        if (smith_lang_register(copy.source, &header) != nullptr) smith_lang_header_step(b, &copy, header);
        smith_lang_sources_used = true;
    }
    copy.step = (SmithStep){
        .command = smith_cat(b, smith_one(copy.compiler), smith_modules_for(b, copy.source), smith_lang_flags(b, lang),
                             copy.flags, tail, nullptr),
        .inputs = smith_one(copy.source),
        .implicit_inputs = smith_cat(b, copy.implicit_inputs, smith_one(smith_compiler_file(copy.compiler)), nullptr),
        .order_only_inputs = lang ? smith_one(smith_lang_stamp) : smith_list(b, nullptr),
        .outputs = smith_one(copy.output),
        .depfile = depfile,
    };
    bool listed = true;
    for (size_t i = 0; i < smith_object_row_count; i++)
        listed = listed && strcmp(smith_object_rows[i].object.output, copy.output) != 0;
    smith_step(b, &copy.step);
    if (smith_object_row_count >= smith_objects_max) smith_too_many("objects", smith_objects_max);
    smith_object_rows[smith_object_row_count++] = (SmithObjectRow){object, copy, listed};
    *object = copy;
}

// A library that is a file: a path (it holds '/') or ends in .a, .dylib or .so; a flag such as -L/usr/lib is not.
static bool smith_is_library_file(const char *word) {
    if (word[0] == '-') return false;
    size_t length = strlen(word);
    const char *suffixes[] = {".a", ".dylib", ".so"};
    bool file = strchr(word, '/') != nullptr;
    for (size_t i = 0; i < 3; i++) {
        size_t n = strlen(suffixes[i]);
        file = file || (length >= n && strcmp(word + length - n, suffixes[i]) == 0);
    }
    return file;
}

static const char *const *smith_library_files(const char *const *libraries) {
    size_t count = smith_count(libraries);
    const char **files = smith_new_list(count);
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        if (smith_is_library_file(libraries[i])) files[used++] = libraries[i];
    }
    files[used] = nullptr;
    return files;
}

void smith_program(SmithBuild *b, SmithProgram *program) {
    SMITH_ASSERT(b != nullptr && program != nullptr);
    smith_require_program("smith_program", "linker", program->linker);
    smith_require_text("smith_program", "output", program->output);
    if (program->objects == nullptr || program->objects[0] == nullptr)
        smith_fail("smith_program: .objects is empty; give it objects passed to smith_object");
    size_t count = smith_count((const char *const *)program->objects);
    const SmithObject **rows = (const SmithObject **)smith_new_list(count);
    const char **outputs = smith_new_list(count);
    for (size_t i = 0; i < count; i++) {
        const SmithObjectRow *row = smith_find_row(program->objects[i], program->objects[i]->output);
        if (row == nullptr) smith_fail("smith_program: .objects[%zu] was never passed to smith_object", i);
        rows[i] = &row->object;
        outputs[i] = row->object.output;
    }
    SmithProgram copy = *program;
    copy.linker = smith_copy_string(program->linker);
    copy.output = smith_copy_string(program->output);
    copy.flags = smith_copy_list(program->flags);
    copy.libraries = smith_copy_list(program->libraries);
    copy.objects = (const SmithObject *const *)rows;
    const char *const tail[] = {"-o", copy.output, nullptr};
    copy.step = (SmithStep){
        .command = smith_cat(b, smith_one(copy.linker), copy.flags, (const char *const *)outputs, copy.libraries, tail,
                             nullptr),
        .inputs = (const char *const *)outputs,
        .implicit_inputs =
            smith_cat(b, smith_library_files(copy.libraries), smith_one(smith_compiler_file(copy.linker)), nullptr),
        .outputs = smith_one(copy.output),
    };
    smith_step(b, &copy.step);
    *program = copy;
}

static void smith_json_string(FILE *out, const char *text) {
    fputc('"', out);
    for (const char *c = text; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\') {
            fputc('\\', out);
            fputc(*c, out);
        } else if ((unsigned char)*c < 0x20U) {
            fprintf(out, "\\u%04x", (unsigned)(unsigned char)*c);
        } else {
            fputc(*c, out);
        }
    }
    fputc('"', out);
}

static void smith_json_entry(FILE *out, const char *directory, const char *file, const char *output,
                             const char *const *arguments) {
    fputs("  {\n    \"directory\": ", out);
    smith_json_string(out, directory);
    fputs(",\n    \"file\": ", out);
    smith_json_string(out, file);
    if (output != nullptr) {
        fputs(",\n    \"output\": ", out);
        smith_json_string(out, output);
    }
    fputs(",\n    \"arguments\": [\n", out);
    for (size_t i = 0; arguments[i] != nullptr; i++) { // bounded by smith_copy_list
        fputs(i > 0 ? ",\n      " : "      ", out);
        smith_json_string(out, arguments[i]);
    }
    fputs("\n    ]\n  }", out);
}

static bool smith_source_recorded(const char *source) {
    for (size_t i = 0; i < smith_object_row_count; i++) {
        if (strcmp(smith_object_rows[i].object.source, source) == 0) return true;
    }
    for (size_t i = 0; i < smith_command_row_count; i++) {
        if (strcmp(smith_command_rows[i].source, source) == 0) return true;
    }
    return false;
}

void smith_compile_command(SmithBuild *b, const char *compiler, const char *source, const char *const *flags) {
    SMITH_ASSERT(b != nullptr);
    smith_require_text("smith_compile_command", "compiler", compiler);
    smith_require_text("smith_compile_command", "source", source);
    if (smith_compile_commands_called) smith_fail("smith_compile_command is called after smith_compile_commands");
    if (smith_source_recorded(source)) return;
    if (smith_command_row_count >= smith_objects_max) smith_too_many("compile commands", smith_objects_max);
    const char *copy = smith_copy_string(source);
    const char *const tail[] = {"-c", copy, nullptr};
    const char *const *arguments =
        smith_cat(b, smith_one(smith_copy_string(compiler)), smith_modules_for(b, copy),
                  smith_lang_flags(b, smith_ends_with(copy, ".smith")), smith_copy_list(flags), tail, nullptr);
    smith_command_rows[smith_command_row_count++] = (SmithCommandRow){copy, arguments};
}

static void smith_put_compile_commands(FILE *out, const SmithBuild *b) {
    fputs("[\n", out);
    for (size_t i = 0; i < smith_object_row_count; i++) {
        const SmithObject *object = &smith_object_rows[i].object;
        if (!smith_object_rows[i].listed) continue;
        smith_json_entry(out, b->root, object->source, object->output, smith_object_rows[i].object.step.command);
        fputs(",\n", out);
    }
    for (size_t i = 0; i < smith_command_row_count; i++) {
        smith_json_entry(out, b->root, smith_command_rows[i].source, nullptr, smith_command_rows[i].arguments);
        fputs(",\n", out);
    }
    smith_json_entry(out, b->root, "build.c", nullptr, smith_build_c_words);
    fputs("\n]\n", out);
}

void smith_compile_commands(SmithBuild *b, const char *path) {
    SMITH_ASSERT(b != nullptr);
    smith_context = nullptr;
    if (smith_compile_commands_called) smith_fail("smith_compile_commands is called twice");
    smith_compile_commands_called = true;
    smith_check_write_path(path);
    FILE *counter = fopen("/dev/null", "w");
    if (counter == nullptr) smith_fail("cannot open /dev/null");
    smith_put_compile_commands(counter, b);
    long size = ftell(counter);
    fclose(counter);
    SMITH_ASSERT(size > 0);
    char *text = smith_alloc((size_t)size + 1);
    FILE *out = fmemopen(text, (size_t)size + 1, "w");
    if (out == nullptr) smith_fail("cannot render %s", path);
    smith_put_compile_commands(out, b);
    SMITH_ASSERT(fclose(out) == 0);
    text[size] = '\0';
    smith_write(b, path, text);
}

// smith_tests: smith's test library and runner, linked with the project's test files.

constexpr size_t smith_tests_max = 65536;

// The name in a line that defines a test, `void test_<name>(void) {` or `void test_<name>(SmithKey <key>) {` with
// <name> in [a-z0-9_] and <key> in [a-z_], or nullptr. *length is the name's length, test_ not counted.
static const char *smith_tests_match(const char *line, size_t *length, bool *property) {
    const char *prefix = "void test_";
    if (strncmp(line, prefix, strlen(prefix)) != 0) return nullptr;
    const char *name = line + strlen(prefix);
    size_t size = strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_");
    const char *rest = name + size;
    if (size == 0) return nullptr;
    *length = size;
    *property = false;
    if (strcmp(rest, "(void) {") == 0) return name;
    if (strncmp(rest, "(SmithKey ", strlen("(SmithKey ")) != 0) return nullptr;
    const char *key = rest + strlen("(SmithKey ");
    size_t key_size = strspn(key, "abcdefghijklmnopqrstuvwxyz_");
    if (key_size == 0 || strcmp(key + key_size, ") {") != 0) return nullptr;
    *property = true;
    return name;
}

typedef struct {
    const char *name; // with the test_ prefix
    const char *file;
    bool property;
} SmithTestFound;

// Every test in the files, in file order and line order. A name defined twice is an error that names both files.
static bool smith_tests_find(SmithRun *run, SmithTestFound *found, size_t *count) {
    *count = 0;
    for (size_t i = 0; run->inputs[i] != nullptr; i++) { // bounded by smith_copy_list
        const char *const *lines = smith_lines(run->b, run->inputs[i]);
        for (size_t j = 0; lines[j] != nullptr; j++) { // bounded by smith_split_lines
            size_t length = 0;
            bool property = false;
            const char *name = smith_tests_match(lines[j], &length, &property);
            if (name == nullptr) continue;
            const char *full = smith_text("test_%.*s", (int)length, name);
            for (size_t k = 0; k < *count; k++) {
                if (strcmp(found[k].name, full) != 0) continue;
                fprintf(stderr, "smith: smith_tests: %s is defined in %s and in %s\n", full, found[k].file,
                        run->inputs[i]);
                return false;
            }
            if (*count >= smith_tests_max) smith_too_many("tests", smith_tests_max);
            found[(*count)++] = (SmithTestFound){full, run->inputs[i], property};
        }
    }
    return true;
}

// The step that finds the tests: .inputs are the files, .outputs the prototypes (tests_list.h) and the table
// (tests_list.c). It rewrites a file only when its bytes change, so the step is restat.
static int smith_tests_scan(SmithRun *run) {
    static SmithTestFound found[smith_tests_max];
    size_t count = 0;
    SMITH_ASSERT(run->outputs[0] != nullptr && run->outputs[1] != nullptr);
    if (!smith_tests_find(run, found, &count)) return 1;
    const char **prototypes = smith_new_list(count);
    const char **rows = smith_new_list(count);
    for (size_t i = 0; i < count; i++) {
        const char *argument = found[i].property ? "SmithKey key" : "void";
        prototypes[i] = smith_text("void %s(%s);\n", found[i].name, argument);
        rows[i] =
            smith_text("    {\"%s\", %s, %s},\n", found[i].name + strlen("test_"),
                       found[i].property ? "nullptr" : found[i].name, found[i].property ? found[i].name : "nullptr");
    }
    const char *header = smith_text("// Generated by smith_tests from the test files; do not edit.\n#pragma once\n\n"
                                    "#include <smith/random.h>\n\n%s",
                                    smith_join(run->b, prototypes, ""));
    const char *base = strrchr(run->outputs[0], '/');
    const char *table = smith_text("// Generated by smith_tests from the test files; do not edit.\n"
                                   "#include <smith/test.h>\n\n#include \"%s\"\n\n"
                                   "const SmithTest smith_test_table[] = {\n%s    {nullptr, nullptr, nullptr},\n};\n"
                                   "const size_t smith_test_count = sizeof smith_test_table / sizeof "
                                   "smith_test_table[0] - 1;\n",
                                   base != nullptr ? base + 1 : run->outputs[0], smith_join(run->b, rows, ""));
    smith_write(run->b, run->outputs[0], header);
    smith_write(run->b, run->outputs[1], table);
    return 0;
}

static int smith_tests_run(SmithRun *run);

static bool smith_tests_links(const SmithTests *tests, const char *source) {
    for (size_t i = 0; tests->objects != nullptr && tests->objects[i] != nullptr; i++) { // bounded by the list
        if (tests->objects[i]->source != nullptr && strcmp(tests->objects[i]->source, source) == 0) return true;
    }
    return false;
}

static void smith_tests_check(const SmithTests *tests) {
    smith_context = nullptr;
    if (smith_count(tests->sources) == 0) smith_fail("smith_tests: .sources is empty; give it the test files");
    bool arena = smith_tests_links(tests, ".smith/lib/arena.c");
    bool random = smith_tests_links(tests, ".smith/lib/random.c");
    if (arena && random) return;
    const char *missing = !arena && !random ? "arena and random" : arena ? "random" : "arena";
    const char *fix = !arena && !random ? "smith_library(b, \"arena\") and smith_library(b, \"random\")"
                      : arena           ? "smith_library(b, \"random\")"
                                        : "smith_library(b, \"arena\")";
    smith_fail("smith_tests: .objects must include smith's %s, which the tests link once; add a SmithObject "
               "compiled from %s to .objects",
               missing, fix);
}

// One object of the tests' own: compiled with the compiler, its output next to the runner.
static SmithObject *smith_tests_object(SmithBuild *b, const SmithTests *tests, const char *source,
                                       const char *const *flags, const char *const *implicit_inputs) {
    const char *visible = source + strspn(source, "./"); // .smith/lib/test.c would give a hidden object
    char *flat = smith_alloc(strlen(visible) + 1);
    for (size_t i = 0; visible[i] != '\0'; i++)
        flat[i] = visible[i] == '/' ? '_' : visible[i];
    flat[strlen(visible)] = '\0';
    SmithObject *object = smith_alloc(sizeof *object);
    *object = (SmithObject){
        .compiler = tests->compiler,
        .source = source,
        .output = smith_replace_extension(b, smith_text("%s_objects/%s", tests->output, flat), ".o"),
        .flags = flags,
        .implicit_inputs = implicit_inputs,
    };
    smith_object(b, object);
    return object;
}

// The runner and, after it, the arguments `smith test` was given: the runner's own options (--test, --seed, --seeds,
// --jobs, --shrink, --shuffle, --full, --isolate and the others its header documents).
static const char *const *smith_tests_arguments(SmithBuild *b, const char *runner) {
    const char *program = strchr(runner, '/') != nullptr ? runner : smith_text("./%s", runner);
    return smith_cat(b, smith_list(b, program, nullptr), smith_args(b), nullptr);
}

void smith_tests(SmithBuild *b, SmithTests *tests) {
    SMITH_ASSERT(b != nullptr && tests != nullptr);
    smith_tests_check(tests);
    SmithTests copy = *tests;
    copy.sources = smith_copy_list(tests->sources);
    copy.flags = smith_copy_list(tests->flags);
    copy.link_flags = smith_copy_list(tests->link_flags);
    copy.inputs = smith_copy_list(tests->inputs);
    copy.compiler = smith_copy_string(tests->compiler != nullptr ? tests->compiler : "smith-cc");
    copy.output = smith_copy_string(tests->output != nullptr ? tests->output : "build/tests");
    const char *name = tests->name != nullptr ? tests->name : "test";
    const char *slash = strrchr(copy.output, '/');
    const char *folder = slash != nullptr ? smith_text("%.*s", (int)(slash - copy.output + 1), copy.output) : "";
    const char *list_header = smith_text("%stests_list.h", folder);
    const char *list_table = smith_text("%stests_list.c", folder);
    SmithStep scan = {
        .run = smith_tests_scan,
        .inputs = copy.sources,
        .outputs = smith_list(b, list_header, list_table, nullptr),
        .description = "find the tests",
        .restat = true,
    };
    smith_step(b, &scan);
    const char *const *test_flags = smith_cat(
        b, copy.flags, smith_list(b, "-include", list_header, "-Werror=missing-prototypes", nullptr), nullptr);
    size_t count = smith_count(copy.sources);
    const char **objects = smith_new_list(count + 3 + smith_count((const char *const *)tests->objects));
    size_t used = 0;
    for (size_t i = 0; i < count; i++)
        objects[used++] = (const char *)smith_tests_object(b, &copy, copy.sources[i], test_flags,
                                                           smith_list(b, list_header, nullptr));
    objects[used++] = (const char *)smith_tests_object(b, &copy, list_table, copy.flags, nullptr);
    objects[used++] = (const char *)smith_tests_object(b, &copy, smith_library(b, "test"), copy.flags, nullptr);
    if (!smith_tests_links(tests, ".smith/lib/trace.c")) // a project that links the trace library itself is not doubled
        objects[used++] = (const char *)smith_tests_object(b, &copy, smith_library(b, "trace"), copy.flags, nullptr);
    for (size_t i = 0; tests->objects[i] != nullptr; i++)
        objects[used++] = (const char *)tests->objects[i];
    SmithProgram program = {
        .linker = copy.compiler,
        .output = copy.output,
        .flags = copy.link_flags,
        .objects = (const SmithObject *const *)objects,
    };
    smith_program(b, &program);
    SmithStep task = {
        .run = smith_tests_run,
        .inputs = smith_cat(b, program.step.outputs, copy.inputs, nullptr),
        .args = smith_tests_arguments(b, copy.output),
        .pool = "console",
    };
    smith_task(b, name, &task);
    copy.step = task;
    *tests = copy;
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
static int smith_run_capture(const char *const *argv, const char *const **lines, bool combined) {
    smith_context = nullptr;
    if (argv == nullptr || argv[0] == nullptr) smith_fail("a run needs a command");
    fflush(stdout); // the function's own printing comes before the program's
    fflush(stderr);
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) smith_fail("%s: cannot make a pipe", argv[0]);
    int error = 0;
    pid_t pid = smith_spawn_into(argv, pipe_fds, false, combined, &error);
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
    int status = smith_run_capture(argv, &lines, false);
    if (status == 0) return lines;
    fprintf(stderr, "smith: %s failed (status %d)\n", argv[0], status);
    return nullptr;
}

int smith_run_status(SmithRun *run, const char *const *argv, const char *const **lines) {
    SMITH_ASSERT(run != nullptr);
    return smith_run_capture(argv, lines, false);
}

int smith_run_combined(SmithRun *run, const char *const *argv, const char *const **lines) {
    SMITH_ASSERT(run != nullptr);
    return smith_run_capture(argv, lines, true);
}

// The current environment with the NAME=value entries added; an entry replaces one of the same name.
static char *const *smith_merge_environment(const char *const *environment) {
    size_t extra = smith_count(environment);
    size_t current = 0;
    for (; environ[current] != nullptr; current++)
        SMITH_ASSERT(current < smith_list_max);
    const char **merged = smith_new_list(current + extra);
    size_t used = 0;
    for (size_t i = 0; i < current; i++) {
        bool replaced = false;
        for (size_t j = 0; j < extra && !replaced; j++) {
            const char *equals = strchr(environment[j], '=');
            if (equals == nullptr) smith_fail("smith_run_program: '%s' is not NAME=value", environment[j]);
            size_t length = (size_t)(equals - environment[j]) + 1; // the name and the '='
            replaced = strncmp(environ[i], environment[j], length) == 0;
        }
        if (!replaced) merged[used++] = environ[i];
    }
    for (size_t j = 0; j < extra; j++)
        merged[used++] = environment[j];
    merged[used] = nullptr;
    return (char *const *)merged;
}

int smith_run_program(SmithRun *run, const char *const *argv, const char *const *environment) {
    SMITH_ASSERT(run != nullptr);
    smith_context = nullptr;
    if (argv == nullptr || argv[0] == nullptr) smith_fail("a run needs a command");
    char *const *envp = smith_merge_environment(environment);
    fflush(stdout);
    fflush(stderr);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, (char *const *)argv, envp) != 0) return smith_exit_not_started;
    int status = smith_wait(argv[0], pid);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

// The task of smith_tests: runs the runner with the options it was given. .args is the program and its arguments.
//
// A failure is a stack of addresses (<smith/trace.h>), and smith turns them into the failing line and the call chain,
// so the runner's output goes through `smith symbolize` on its way to the terminal, and the runner is asked for its
// traces
// (--trace). That needs the full smith, which links LLVM's symbolizer: the stage0 runs the runner as it always did.
// AddressSanitizer and UBSan symbolize their reports by starting a program named llvm-symbolizer, which here is smith.

static bool smith_tests_can_symbolize(SmithRun *run, const char *self) {
    const char *argv[] = {self, "symbolize", "--available", nullptr};
    return smith_run_status(run, argv, nullptr) == 0;
}

// .smith/bin/llvm-symbolizer, a link to smith, which smith runs under that name as the symbolizer protocol's server.
static const char *smith_tests_symbolizer_link(SmithRun *run, const char *self) {
    char folder[PATH_MAX];
    if (getcwd(folder, sizeof folder) == nullptr) smith_fail("smith_tests: cannot find the current folder");
    const char *path = smith_fmt(run->b, "%s/.smith/bin/llvm-symbolizer", folder);
    smith_make_parents(path);
    unlink(path);
    if (symlink(self, path) != 0) smith_fail("smith_tests: cannot make %s", path);
    return path;
}

static const char *const *smith_tests_environment(SmithRun *run, const char *self) {
    const char *link = smith_tests_symbolizer_link(run, self);
    const char *const *environment = smith_list(run->b, nullptr);
    static const char *const names[] = {"ASAN_SYMBOLIZER_PATH", "UBSAN_SYMBOLIZER_PATH"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (getenv(names[i]) != nullptr) continue; // the project's own choice stands
        const char *entry = smith_fmt(run->b, "%s=%s", names[i], link);
        environment = smith_cat(run->b, environment, smith_list(run->b, entry, nullptr), nullptr);
    }
    return environment;
}

static int smith_tests_exit_code(int status) {
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int smith_tests_run(SmithRun *run) {
    smith_context = nullptr;
    const char *self = getenv("SMITH_SELF");
    if (self == nullptr || !smith_tests_can_symbolize(run, self)) return smith_run_program(run, run->args, nullptr);
    const char *const *runner = smith_cat(run->b, run->args, smith_list(run->b, "--trace", nullptr), nullptr);
    if (isatty(STDOUT_FILENO) != 0) runner = smith_cat(run->b, runner, smith_list(run->b, "--color", nullptr), nullptr);
    const char *const filter[] = {self, "symbolize", nullptr};
    int ends[2];
    if (pipe(ends) != 0) smith_fail("smith_tests: cannot make a pipe");
    posix_spawn_file_actions_t into_pipe;
    posix_spawn_file_actions_t from_pipe;
    posix_spawn_file_actions_init(&into_pipe);
    posix_spawn_file_actions_init(&from_pipe);
    posix_spawn_file_actions_adddup2(&into_pipe, ends[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&into_pipe, ends[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&into_pipe, ends[0]);
    posix_spawn_file_actions_addclose(&into_pipe, ends[1]);
    posix_spawn_file_actions_adddup2(&from_pipe, ends[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&from_pipe, ends[0]);
    posix_spawn_file_actions_addclose(&from_pipe, ends[1]);
    fflush(stdout);
    fflush(stderr);
    pid_t symbolizer = 0;
    pid_t program = 0;
    int started = posix_spawn(&symbolizer, self, &from_pipe, nullptr, (char *const *)filter, environ);
    if (started == 0)
        started = posix_spawnp(&program, runner[0], &into_pipe, nullptr, (char *const *)runner,
                               smith_merge_environment(smith_tests_environment(run, self)));
    close(ends[0]);
    close(ends[1]);
    posix_spawn_file_actions_destroy(&into_pipe);
    posix_spawn_file_actions_destroy(&from_pipe);
    if (started != 0) return smith_exit_not_started;
    int status = smith_wait(runner[0], program);
    (void)smith_wait(self, symbolizer);
    return smith_tests_exit_code(status);
}

// What a .run step read, for its depfile. ninja's reader takes a space as '\ ', '#' as '\#' and '$' as '$$'.
static const char *smith_run_depfile;
static const char *smith_run_target;
static const char *smith_depends[smith_entries_max];
static size_t smith_depend_count;

void smith_run_depend(SmithRun *run, const char *path) {
    SMITH_ASSERT(run != nullptr);
    smith_context = nullptr;
    if (path == nullptr || *path == '\0') smith_fail("smith_run_depend needs a path");
    if (smith_run_depfile == nullptr)
        smith_fail("smith_run_depend: this step has no .depfile, so it cannot record '%s'", path);
    if (strchr(path, '\n') != nullptr) smith_fail("smith_run_depend: the path '%s' holds a newline", path);
    if (smith_depend_count >= smith_entries_max) smith_too_many("paths in smith_run_depend", smith_entries_max);
    smith_depends[smith_depend_count++] = smith_copy_string(path);
}

static void smith_put_depfile_word(FILE *out, const char *word) {
    for (const char *c = word; *c != '\0'; c++) {
        if (*c == ' ' || *c == '#') fputc('\\', out);
        if (*c == '$') fputc('$', out);
        fputc(*c, out);
    }
}

// Always written when the step has a .depfile, even with no paths, so ninja never reads a stale one.
static void smith_write_run_depfile(void) {
    if (smith_run_depfile == nullptr) return;
    smith_make_parents(smith_run_depfile);
    FILE *out = fopen(smith_run_depfile, "wb");
    if (out == nullptr) smith_fail("cannot write %s", smith_run_depfile);
    smith_put_depfile_word(out, smith_run_target);
    fputc(':', out);
    for (size_t i = 0; i < smith_depend_count; i++) {
        fputc(' ', out);
        smith_put_depfile_word(out, smith_depends[i]);
    }
    fputc('\n', out);
    if (fclose(out) != 0) smith_fail("cannot write %s", smith_run_depfile);
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

// The fixed function a step's function is measured from: a command carries the difference of the two addresses, which
// is the same in every run of one executable.
static int smith_run_anchor(SmithRun *run) {
    (void)run;
    return 0;
}

// What a function gets, as the words `--input a --input b`: everything it sees is in its step's command.
static void smith_put_run_words(FILE *file, const char *flag, const char *const *words) {
    for (size_t i = 0; words[i] != nullptr; i++) { // bounded by smith_copy_list
        fprintf(file, " %s ", flag);
        smith_put_quoted(file, words[i], false);
    }
}

// The command line as sh reads it: the words, then the stamp's touch. It is built in a fixed buffer and ends with a
// 0 byte; *length is its size.
static const char *smith_render_command(const SmithEntry *entry, size_t *length) {
    const SmithStep *step = &entry->step;
    FILE *file = fmemopen(smith_command_buffer, smith_command_max, "w");
    if (file == nullptr) smith_fail("cannot render the command");
    if (step->run != nullptr) {
        fprintf(file, ".smith/program --run %" PRIxPTR, (uintptr_t)step->run - (uintptr_t)smith_run_anchor);
        smith_put_run_words(file, "--input", step->inputs);
        if (entry->kind != SmithEntryKind_Task) smith_put_run_words(file, "--output", step->outputs);
        smith_put_run_words(file, "--arg", step->args);
        const char *name = step->description != nullptr         ? step->description
                           : entry->kind == SmithEntryKind_Task ? entry->name
                                                                : step->outputs[0];
        fputs(" --description ", file);
        smith_put_quoted(file, name, false);
        if (step->depfile != nullptr) {
            fputs(" --depfile ", file);
            smith_put_quoted(file, step->depfile, false);
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

// The words after each `flag` in `--run`'s arguments (flag word pairs from argv[3]), as a list; any other argument is
// an error.
static const char *const *smith_run_collect(int argc, char **argv, const char *flag) {
    size_t count = 0;
    for (int i = 3; i < argc; i += 2) { // bounded by argc: each flag takes its word
        bool known = strcmp(argv[i], "--input") == 0 || strcmp(argv[i], "--output") == 0 ||
                     strcmp(argv[i], "--arg") == 0 || strcmp(argv[i], "--description") == 0 ||
                     strcmp(argv[i], "--depfile") == 0;
        if (!known) smith_fail("--run: unexpected argument '%s'", argv[i]);
        if (i + 1 >= argc) smith_fail("--run: %s needs a word", argv[i]);
        count += strcmp(argv[i], flag) == 0;
    }
    const char **list = smith_new_list(count);
    size_t used = 0;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], flag) == 0) list[used++] = argv[i + 1];
    }
    return list;
}

// The word after the last `flag`, or nullptr.
static const char *smith_run_value(int argc, char **argv, const char *flag) {
    const char *value = nullptr;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], flag) == 0) value = argv[i + 1];
    }
    return value;
}

// --run <function> [--input p]... [--output p]... [--arg w]...: the step's command carries everything the function
// gets, so build() and the callbacks do not run. The function is the anchor's address plus the offset.
static int smith_run_command(SmithBuild *b, int argc, char **argv) {
    if (argc < 3) smith_fail("--run needs a function");
    char *end = nullptr;
    errno = 0;
    uintmax_t offset = strtoumax(argv[2], &end, 16);
    if (errno != 0 || end == argv[2] || *end != '\0') smith_fail("--run: '%s' is not an offset", argv[2]);
    uintptr_t address = (uintptr_t)smith_run_anchor + (uintptr_t)offset;
    Dl_info anchor_info;
    Dl_info function_info;
    SMITH_ASSERT(dladdr((void *)(uintptr_t)smith_run_anchor, &anchor_info) != 0);
    SMITH_ASSERT(dladdr((void *)address, &function_info) != 0);     // inside a loaded image ...
    SMITH_ASSERT(function_info.dli_fbase == anchor_info.dli_fbase); // ... which is this executable, not a library
    SmithRun run = {
        .b = b,
        .inputs = smith_run_collect(argc, argv, "--input"),
        .outputs = smith_run_collect(argc, argv, "--output"),
        .args = smith_run_collect(argc, argv, "--arg"),
    };
    const char *description = smith_run_value(argc, argv, "--description");
    smith_run_depfile = smith_run_value(argc, argv, "--depfile");
    smith_run_target = run.outputs[0];
    int (*function)(SmithRun *) = (int (*)(SmithRun *))address;
    int status = function(&run);
    smith_context = nullptr;
    smith_write_run_depfile();
    if (status != 0) {
        fflush(stdout);
        fprintf(stderr, "smith: %s: failed (status %d)\n", description != nullptr ? description : "step", status);
    }
    return status;
}

int main(int argc, char **argv) {
    SMITH_ASSERT(argc >= 2 && argv[1] != nullptr);
    if (strcmp(argv[1], "--run") == 0) {
        SmithBuild b = smith_make_build();
        return smith_run_command(&b, argc, argv);
    }
    smith_regenerating = strcmp(argv[1], "--regenerate") == 0;
    int first = smith_regenerating ? 2 : 1; // the manifest comes next, then -D arguments
    SMITH_ASSERT(argc > first && argv[first] != nullptr);
    smith_parse_options(argc - first - 1, argv + first + 1);
    SmithBuild b = smith_make_build();
    build(&b);
    smith_context = nullptr;
    smith_run_defers(&b);
    smith_lang_finish(&b);
    // A -D that only a pending smith_defer callback reads is not known yet; stage 2 checks it.
    if (!smith_any_pending()) smith_check_given();
    if (!smith_default_called && !smith_any_pending()) smith_fail("no default; call smith_default");
    smith_write_compiler_files();
    smith_write_manifest(argv[first]);
    return 0;
}
