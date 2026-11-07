// The build executor: `smith build` runs the manifest that build.c's program wrote (.smith/build.ninja) itself, with no
// ninja. It reads the subset of ninja's language that smith writes, decides what is out of date the way ninja does
// (mtimes, the command's hash, the depfiles' contents, restat), and runs the jobs on smith's own io: one kqueue loop
// waits on every job's process and pipe. It is included by smith.c and stands on the io module (linked). The design and
// what it drops compared with ninja are in knowledge/executor.md.
//
// Memory: the graph and everything else lives in io.arena, which is address space reserved up front, so there is no
// malloc. A reload of the manifest or a finished job leaves its memory behind; a build is a short-lived process.
//
// The files, in the order they are included:
//   executor_manifest.c  the reader of the manifest, and the graph it makes
//   executor_state.c     what is kept between builds: the command log and the depfile log
//   executor_plan.c      what is out of date, and the order to do it in
//   executor_cache.c     the shared cache of compile steps
//   executor_run.c       the progress line, the jobs and the loop that runs them
//   executor_tools.c     -t clean, compdb, targets, deps, commands and cleandead

#include "core.h"
#include "executor.h"
#include "executor_cache.h"
#include "executor_manifest.h"
#include "executor_plan.h"
#include "executor_run.h"
#include "executor_state.h"
#include "executor_tools.h"
#include "main.h"
#include "mcp_unit.h"
#include "world.h"

constexpr size_t smith_exec_reloads_max = 8;            // how many times a rebuilt manifest is read again














// Makes room for at least `extra` more bytes and a NUL.
static void smith_grow_reserve(SmithGrow *grow, size_t extra) {
    SMITH_ASSERT(grow->arena != nullptr && grow->size <= grow->capacity);
    if (grow->data != nullptr && grow->capacity - grow->size > extra) return;
    size_t wanted = grow->capacity * 2;
    if (wanted < grow->size + extra + 1) wanted = grow->size + extra + 1;
    if (wanted < 256) wanted = 256;
    unsigned char *bigger = smith_arena_push(grow->arena, wanted, 1);
    if (grow->size > 0) memcpy(bigger, grow->data, grow->size);
    grow->data = bigger;
    grow->capacity = wanted;
}

void smith_grow_append(SmithGrow *grow, const void *bytes, size_t size) {
    smith_grow_reserve(grow, size);
    if (size > 0) memcpy(grow->data + grow->size, bytes, size);
    grow->size += size;
    grow->data[grow->size] = 0;
}









// What a message calls the project's build file: build.c, build.smith, or "the build" when it runs by convention.
const char *smith_project_build_file(void) {
    if (access("build.c", F_OK) == 0) return "build.c";
    if (access("build.smith", F_OK) == 0) return "build.smith";
    return "the build";
}

[[noreturn]] void smith_exec_fail(const char *format, ...) {
    fflush(stdout);
    va_list arguments;
    va_start(arguments, format);
    fputs("smith: error: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
    exit(smith_exec_failed);
}

void *smith_exec_push(SmithExec *exec, size_t size, size_t align) {
    return smith_arena_push(exec->arena, size, align);
}

// Memory that is not touched until it is used, for the tables that are sized by their maximum: address space only, as a
// pushed block is cleared and a carved one is not. It is zero when the io made the arena, and is never given back.
void *smith_exec_carve(SmithExec *exec, size_t size) {
    return smith_arena_carve(exec->arena, size).memory;
}

const char *smith_exec_copy(SmithExec *exec, const char *text, size_t length) {
    char *copy = smith_exec_push(exec, length + 1, 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

// FNV-1a 64: the hash of a command, and of a path in the tables.
uint64_t smith_exec_hash(const char *text, size_t length) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < length; i++) {
        hash ^= (unsigned char)text[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

// Folds `.` and `..` and repeated slashes in a path, in place, as ninja does, so that two spellings of one file are one
// node. A path made empty is ".". Returns the new length.
size_t smith_exec_canonical(char *path, size_t length) {
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


// A SmithExec in an arena of its own, with the scratch text carved from it. The memory is given back by
// smith_exec_close.
SmithExec *smith_exec_open(SmithIo *io) {
    SmithArena arena = smith_world_arena();
    SmithExec *exec = smith_arena_push(&arena, sizeof(SmithExec), alignof(SmithExec));
    exec->own = arena;
    exec->arena = &exec->own;
    exec->io = io;
    SmithArena scratch = smith_arena_carve(exec->arena, smith_exec_text_max);
    exec->scratch = (char *)scratch.memory;
    return exec;
}

[[maybe_unused]] void smith_exec_close(SmithExec *exec) { // used by the in-process entry points (SMITH_CORE)
    munmap(exec->own.memory, exec->own.capacity);
}





// Runs the manifest: a tool, or a build of the targets. A manifest that has a rule of its own (build.c's regeneration)
// is built first and read again when that changed it. Returns the status of the build.
int smith_exec_run(SmithIo *io, const char *manifest, const SmithExecOptions *options) {
    SmithExec *exec = smith_exec_open(io);
    exec->options = *options;
    exec->manifest = manifest;
    smith_run_printer_open(exec);
    smith_run_watch_signals();
    for (size_t reload = 0; reload <= smith_exec_reloads_max; reload++) {
        uint64_t mark = smith_time_now(); // `--time`: reading the manifest and the logs, and planning, are the plan
        smith_manifest_read(exec);
        if (!exec->state.ready) smith_state_open(exec);
        if (options->tool != nullptr) return smith_tool_run(exec);
        smith_stepcache_prune(exec);
        uint32_t node = smith_run_manifest_node(exec);
        exec->first_reason[0] = '\0';
        bool regenerate = node != UINT32_MAX && smith_plan_build(exec, &node, 1);
        smith_time_state.plan_nanoseconds += smith_time_since(mark);
        if (!regenerate) break;
        int status = smith_run_plan(exec);
        if (status != 0 || options->dry_run) return status;
        if (reload == smith_exec_reloads_max)
            smith_exec_fail("%s is still out of date after %zu tries; the first thing out of date in the last one: %s",
                            manifest, reload, exec->first_reason[0] != '\0' ? exec->first_reason : "(none recorded)");
    }
    uint64_t mark = smith_time_now();
    size_t room = exec->graph.node_count + exec->graph.default_count + options->target_count + 1;
    uint32_t *targets = smith_exec_push(exec, room * sizeof(uint32_t), alignof(uint32_t));
    size_t count = smith_plan_targets(exec, targets);
    smith_time_state.plan_nanoseconds += smith_time_since(mark);
    return smith_run_targets(exec, targets, count, true);
}
