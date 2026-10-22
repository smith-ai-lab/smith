// The build executor: `smith build` runs the manifest that build.c's program wrote (.smith/build.ninja) itself, with no
// ninja. It reads the subset of ninja's language that smith writes, decides what is out of date the way ninja does
// (mtimes, the command's hash, the depfiles' contents, restat), and runs the jobs on smith's own io: one kqueue loop
// waits on every job's process and pipe. It is included by smith.c, after lib/io.c. The design and what it drops
// compared with ninja are in docs/build-c.md.
//
// Memory: the graph and everything else lives in io.arena, which is address space reserved up front, so there is no
// malloc. A reload of the manifest or a finished job leaves its memory behind; a build is a short-lived process.
//
// The files, in the order they are included:
//   executor_manifest.c  the reader of the manifest, and the graph it makes
//   executor_state.c     what is kept between builds: the command log and the depfile log
//   executor_plan.c      what is out of date, and the order to do it in
//   executor_run.c       the progress line, the jobs and the loop that runs them
//   executor_tools.c     -t clean, compdb, targets, deps, commands and cleandead

constexpr size_t smith_exec_nodes_max = (size_t)1 << 21;
constexpr size_t smith_exec_edges_max = (size_t)1 << 19;
constexpr size_t smith_exec_ids_max = (size_t)1 << 23;
constexpr size_t smith_exec_pools_max = 256;
constexpr size_t smith_exec_rules_max = 64;
constexpr size_t smith_exec_bindings_max = 64;
constexpr size_t smith_exec_defaults_max = (size_t)1 << 16;
constexpr size_t smith_exec_variables_max = 1024;
constexpr size_t smith_exec_text_max = (size_t)4 << 20; // one evaluated value, a command included
constexpr size_t smith_exec_name_max = 4096;            // one path
constexpr size_t smith_exec_reloads_max = 8;            // how many times a rebuilt manifest is read again
constexpr size_t smith_exec_jobs_max = 128;
constexpr size_t smith_exec_job_arena = (size_t)256 << 20; // address space for one running job's output
constexpr uint64_t smith_exec_nanoseconds = 1'000'000'000;
constexpr uint64_t smith_exec_milliseconds = 1'000'000;
constexpr int smith_exec_failed = 1;
constexpr int smith_exec_interrupted_status = 130;
constexpr size_t smith_exec_pool_console = 1; // the pool every console job is in; pool numbers start at 1

typedef enum {
    SmithScan_None,
    SmithScan_Visiting,
    SmithScan_Done,
} SmithScan;

// Where an edge is in the plan of one build.
typedef enum {
    SmithWant_None,    // not in the plan
    SmithWant_Nothing, // in the plan, but clean: it only waits for its inputs
    SmithWant_Start,   // dirty: its command must run
    SmithWant_Running,
    SmithWant_Done,
} SmithWant;

typedef struct {
    const char *path;
    int64_t mtime;     // -1 not read yet, 0 missing, otherwise nanoseconds since the epoch
    uint32_t edge;     // the edge that makes it, plus one; 0 for a file nothing makes
    uint32_t consumer; // the first of its consumer links, plus one; 0 for none
    uint8_t scan;      // SmithScan
    bool dirty;
    bool from_deps; // first seen in a depfile: a missing one is not an error
} SmithNode;

// One edge that reads a node, in a list that ends with 0.
typedef struct {
    uint32_t edge; // plus one
    uint32_t next; // plus one
} SmithLink;

typedef struct {
    const char *rule; // "phony" or the rule's name
    const char *command;
    const char *description;
    const char *depfile;
    uint64_t command_hash;
    uint32_t inputs;         // in ids: the explicit, the implicit and the order-only inputs, in this order
    uint32_t explicit_count; // the explicit inputs, which $in names
    uint32_t implicit_count;
    uint32_t order_count;
    uint32_t outputs; // in ids; the explicit outputs come first
    uint32_t explicit_outputs;
    uint32_t output_count;
    uint32_t deps; // in ids: what the depfile log says the edge read
    uint32_t dep_count;
    uint32_t cursor;  // the input the scan is at
    uint32_t pool;    // 0 for none, 1 for the console, otherwise the pool's number plus one
    uint32_t waiting; // inputs whose edge has not finished
    uint32_t weight;  // commands on the longest chain from this edge to the end
    bool phony;
    bool restat;
    bool generator;
    bool has_deps; // `deps = gcc`: the depfile is read when the command ends, kept in the depfile log and removed
    bool dirty;
    bool outputs_ready;
    bool deps_missing;
    uint8_t scan;
    uint8_t want;
} SmithEdge;

typedef struct {
    const char *name;
    int depth;
    int running;
} SmithPool;

typedef struct {
    const char *key;
    const char *value;
} SmithBinding;

typedef struct {
    const char *name;
    SmithBinding bindings[smith_exec_bindings_max];
    size_t count;
} SmithRule;

// The graph of one manifest.
typedef struct {
    SmithNode *nodes;
    size_t node_count;
    uint32_t *table; // open addressing: a node number plus one, or 0
    size_t table_capacity;
    SmithEdge *edges; // edges[0] is unused: an edge number is its index
    size_t edge_count;
    uint32_t *ids;
    size_t id_count;
    SmithLink *links;
    size_t link_count;
    SmithPool *pools;
    size_t pool_count;
    SmithRule *rules;
    size_t rule_count;
    uint32_t *defaults;
    size_t default_count;
    const char *builddir;
    SmithBinding *variables; // the manifest's top-level variables
    size_t variable_count;
} SmithGraph;

// What the command log says about an output.
typedef struct {
    const char *path;
    int64_t mtime;
    uint64_t hash;
    uint32_t start_ms;
    uint32_t end_ms;
} SmithLogEntry;

// What the depfile log says about an output: the paths its depfile named.
typedef struct {
    const char *path;
    int64_t mtime;
    const char *const *deps;
    uint32_t dep_count;
} SmithDepsEntry;

// The two logs, keyed by the path of an output.
typedef struct {
    SmithLogEntry *log;
    size_t log_count;
    size_t log_lines; // lines in the file, the dead ones included
    uint32_t *log_table;
    size_t log_capacity;
    SmithDepsEntry *deps;
    size_t deps_count;
    size_t deps_records;
    uint32_t *deps_table;
    size_t deps_capacity;
    const char *log_path;
    const char *deps_path;
    bool ready;
} SmithState;

typedef struct SmithJob SmithJob;
typedef struct SmithExec SmithExec;

// A unit of work. Today every job is a process; a job that runs in this process later (a compile) is another kind with
// the same three functions.
typedef struct {
    // Starts the job; false, with the reason in job->message, when it cannot.
    bool (*start)(SmithExec *exec, SmithJob *job);
    // Looks at what the loop completed; true once the job is over, with its result set.
    bool (*poll)(SmithExec *exec, SmithJob *job);
    // Asks a running job to stop: the signal that interrupted the build.
    void (*stop)(SmithExec *exec, SmithJob *job, int signal);
} SmithJobKind;

struct SmithJob {
    const SmithJobKind *kind;
    bool active;
    uint32_t edge;
    uint64_t started; // the clock's milliseconds since the build began
    int64_t wall;     // the file system's time (nanoseconds since the epoch) when it started: what the log records
    int status;       // 0 for success; the exit status, or 128 plus the signal
    int signal;       // the signal that killed the process, or 0
    const char *output;
    size_t output_size;
    bool truncated;    // the output filled the job's memory and its end was dropped
    char message[256]; // why it could not start or be read
    SmithArena arena;  // the job's output goes here; reset when the job is over
    SmithCapture capture;
    SmithOp wait;
    SmithProcess process;
};

typedef struct {
    bool smart;  // a terminal that takes \r and erase-to-end-of-line
    bool dirty;  // the status line is on the screen, with no newline after it
    bool locked; // a console job has the terminal
    size_t columns;
    char status[1024]; // the latest status line that was held back while the terminal was locked
    SmithGrow held;    // output that waits for the terminal
} SmithPrinter;

typedef struct {
    int jobs;             // -j
    int failures_allowed; // -k: how many failed jobs end the build; 0 asks for no limit
    bool dry_run;         // -n
    bool verbose;         // -v
    bool explain;         // -d explain: say why each edge is dirty
    bool quiet;           // --quiet: no progress, only what a failed job printed
    const char *tool;     // -t
    const char *const *tool_args;
    size_t tool_arg_count;
    const char *const *targets;
    size_t target_count;
    const char
        *verb; // the project verb that asked for its target (smith run), for the message when the project has none
} SmithExecOptions;

struct SmithExec {
    SmithIo *io;
    SmithArena *arena;
    SmithExecOptions options;
    const char *manifest;
    SmithGraph graph;
    SmithState state;
    SmithPrinter printer;
    char *scratch; // smith_exec_text_max bytes for evaluating one value
    // The plan of the build that is running.
    uint32_t *ready; // edges whose inputs are done and whose command must run, in no order
    size_t ready_count;
    uint32_t *finishing; // edges that finish with no command, to be taken in turn
    size_t finishing_count;
    size_t wanted;        // edges that must still start or finish
    size_t command_edges; // the commands the plan holds: the progress line's total
    size_t started_edges;
    size_t finished_edges;
    int failures;
    int failures_left; // failed jobs the build can still take before it stops starting new ones
    int stopped;       // the signal that interrupted the build, or 0
    uint64_t origin;   // the monotonic clock when the build began
    SmithJob *jobs;
    size_t running;
};

// What a message calls the project's build file: build.c, build.smith, or "the build" when it runs by convention.
static const char *smith_project_build_file(void) {
    if (access("build.c", F_OK) == 0) return "build.c";
    if (access("build.smith", F_OK) == 0) return "build.smith";
    return "the build";
}

[[noreturn]] static void smith_exec_fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] static void smith_exec_fail(const char *format, ...) {
    fflush(stdout);
    va_list arguments;
    va_start(arguments, format);
    fputs("smith: error: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    exit(smith_exec_failed);
}

static void *smith_exec_push(SmithExec *exec, size_t size, size_t align) {
    return smith_arena_push(exec->arena, size, align);
}

// Memory that is not touched until it is used, for the tables that are sized by their maximum: address space only, as a
// pushed block is cleared and a carved one is not. It is zero when the io made the arena, and is never given back.
static void *smith_exec_carve(SmithExec *exec, size_t size) {
    return smith_arena_carve(exec->arena, size).memory;
}

static const char *smith_exec_copy(SmithExec *exec, const char *text, size_t length) {
    char *copy = smith_exec_push(exec, length + 1, 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

// FNV-1a 64: the hash of a command, and of a path in the tables.
static uint64_t smith_exec_hash(const char *text, size_t length) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < length; i++) {
        hash ^= (unsigned char)text[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Folds `.` and `..` and repeated slashes in a path, in place, as ninja does, so that two spellings of one file are one
// node. A path made empty is ".". Returns the new length.
static size_t smith_exec_canonical(char *path, size_t length) {
    bool absolute = length > 0 && path[0] == '/';
    char *floor = path + (absolute ? 1 : 0); // `..` cannot go above this
    char *out = floor;
    const char *in = floor;
    const char *end = path + length;
    // Each pass consumes one component, so the loop ends within length passes. The output never gets ahead of the
    // input: each byte written has a byte of the path that it replaces.
    for (size_t guard = 0; in < end && guard <= length; guard++) {
        const char *slash = memchr(in, '/', (size_t)(end - in));
        size_t part = slash != nullptr ? (size_t)(slash - in) : (size_t)(end - in);
        bool dot = part == 1 && in[0] == '.';
        bool up = part == 2 && in[0] == '.' && in[1] == '.';
        if (up) {
            char *start = out; // the start of the last component written
            while (start > floor && start[-1] != '/')
                start -= 1;
            bool last_is_up = out - start == 2 && start[0] == '.' && start[1] == '.';
            if (out > floor && !last_is_up) {
                out = start > floor ? start - 1 : start; // drop the component and the slash before it
            } else if (!absolute) {
                if (out > floor) *out++ = '/';
                *out++ = '.';
                *out++ = '.';
            }
        } else if (part > 0 && !dot) {
            if (out > floor) *out++ = '/';
            memmove(out, in, part);
            out += part;
        }
        in += part + (slash != nullptr ? 1 : 0);
    }
    if (out == path) *out++ = '.';
    *out = '\0';
    return (size_t)(out - path);
}

// Each file uses the ones before it, so they are blocks of one line: clang-format sorts only within a block.
#include "executor_manifest.c"

#include "executor_state.c"

#include "executor_plan.c"

#include "executor_run.c"

#include "executor_tools.c"

// Runs the manifest: a tool, or a build of the targets. A manifest that has a rule of its own (build.c's regeneration)
// is built first and read again when that changed it. Returns the status of the build.
static int smith_exec_run(SmithIo *io, const char *manifest, const SmithExecOptions *options) {
    SmithExec *exec = smith_arena_push(&io->arena, sizeof(SmithExec), alignof(SmithExec));
    exec->io = io;
    exec->arena = &io->arena;
    exec->options = *options;
    exec->manifest = manifest;
    SmithArena scratch = smith_arena_carve(&io->arena, smith_exec_text_max);
    exec->scratch = (char *)scratch.memory;
    smith_run_printer_open(exec);
    smith_run_watch_signals();
    for (size_t reload = 0; reload <= smith_exec_reloads_max; reload++) {
        smith_manifest_read(exec);
        if (!exec->state.ready) smith_state_open(exec);
        if (options->tool != nullptr) return smith_tool_run(exec);
        uint32_t node = smith_run_manifest_node(exec);
        if (node == UINT32_MAX || !smith_plan_build(exec, &node, 1)) break;
        int status = smith_run_plan(exec);
        if (status != 0 || options->dry_run) return status;
        if (reload == smith_exec_reloads_max)
            smith_exec_fail("%s is still out of date after %zu tries", manifest, reload);
    }
    size_t room = exec->graph.node_count + exec->graph.default_count + options->target_count + 1;
    uint32_t *targets = smith_exec_push(exec, room * sizeof(uint32_t), alignof(uint32_t));
    size_t count = smith_plan_targets(exec, targets);
    return smith_run_targets(exec, targets, count, true);
}
