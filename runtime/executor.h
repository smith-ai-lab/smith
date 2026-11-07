// Generated split of smith.c (#2685): what runtime/executor.c shares with the other files.
#pragma once

#include "core.h"

constexpr size_t smith_exec_nodes_max = (size_t)1 << 21;

constexpr size_t smith_exec_edges_max = (size_t)1 << 19;

constexpr size_t smith_exec_ids_max = (size_t)1 << 23;

constexpr size_t smith_exec_pools_max = 256;

constexpr size_t smith_exec_rules_max = 64;

constexpr size_t smith_exec_bindings_max = 64;

constexpr size_t smith_exec_defaults_max = (size_t)1 << 16;

constexpr size_t smith_exec_variables_max = 1024;

constexpr size_t smith_exec_text_max = (size_t)4 << 20; // one evaluated value, a command included

constexpr size_t smith_exec_name_max = 4096; // one path

constexpr size_t smith_exec_jobs_max = 128;

constexpr size_t smith_exec_job_arena = (size_t)256 << 20; // address space for one running job's output

constexpr uint64_t smith_exec_nanoseconds = 1'000'000'000;

constexpr uint64_t smith_exec_milliseconds = 1'000'000;

constexpr int smith_exec_failed = 1;

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
    uint32_t cause;    // when dirty, the node whose own reason (-d explain) started it, plus one
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
    uint8_t step_cache; // 0 not looked at, 1 a compile the step cache serves, 2 not
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

// A block in the arena that doubles when full (the old block stays behind), for output of unknown size: the logs the
// executor writes and the output it holds back for the terminal. The io library has one of its own, which is private.
typedef struct {
    SmithArena *arena;
    unsigned char *data;
    size_t size;
    size_t capacity;
} SmithGrow;

void smith_grow_append(SmithGrow *grow, const void *bytes, size_t size);

typedef struct SmithJob SmithJob;

typedef struct SmithExec SmithExec;

// How the step cache keyed a compile before it ran (executor_cache.c): the key of its manifest.
typedef struct {
    char manifest[65];
    bool valid;
} SmithStepKey;

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
    bool heavy; // a worker of the host's pool (slots.c)
    int token;  // its extra core, a descriptor to give back, or negative
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
    SmithStepKey key;             // the step cache's key of a compile; valid when it was keyed
    bool cached;                  // its outputs came from the step cache, no command ran
    const char *const *step_deps; // every file the compile read, the system's included
    uint32_t step_dep_count;
    const char *depfile_text; // what the compile's depfile said
    size_t depfile_size;
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

constexpr size_t smith_exec_recap_max = 16; // failed steps the recap lists; the rest are counted

constexpr size_t smith_exec_recap_line = 200;

struct SmithExec {
    SmithIo *io;    // the loop and the processes, until std.task replaces them (#2662); null where nothing runs
    SmithArena own; // what arena points to: address space reserved up front
    SmithArena *arena;
    SmithExecOptions options;
    const char *manifest;
    SmithGraph graph;
    SmithState state;
    SmithPrinter printer;
    char *scratch;          // smith_exec_text_max bytes for evaluating one value
    char first_reason[256]; // why the first output of the last plan was out of date (empty: none was)
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
    char recap[smith_exec_recap_max][smith_exec_recap_line]; // the failed steps, for the recap at the end
    bool recap_console[smith_exec_recap_max];                // whether a recap entry ran in the console pool (a test)
    size_t recap_count;
    char first_failed[256]; // the first failed step, "step 46 compile smith.c": the last line names it
    int failures_left;      // failed jobs the build can still take before it stops starting new ones
    int stopped;            // the signal that interrupted the build, or 0
    uint64_t origin;        // the monotonic clock when the build began
    SmithJob *jobs;
    size_t running;
    size_t heavy_running; // jobs that hold a core of the pool: the first rides on the base token
};

const char *smith_project_build_file(void);

[[noreturn]] void smith_exec_fail(const char *format, ...);

void *smith_exec_push(SmithExec *exec, size_t size, size_t align);

void *smith_exec_carve(SmithExec *exec, size_t size);

const char *smith_exec_copy(SmithExec *exec, const char *text, size_t length);

uint64_t smith_exec_hash(const char *text, size_t length);

size_t smith_exec_canonical(char *path, size_t length);

SmithExec *smith_exec_open(SmithIo *io);

[[maybe_unused]] void smith_exec_close(SmithExec *exec);

int smith_exec_run(SmithIo *io, const char *manifest, const SmithExecOptions *options);
