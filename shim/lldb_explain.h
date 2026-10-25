// What shim/lldb_explain.cpp gives runtime/explain.c: a stopped process read through lldb. Plain C, included by both.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char function[256]; // as written: `geometry::box::area`, `smith_check`
    char file[1024];    // "" when the frame has no line
    unsigned line;
} SmithExplainFrame;

typedef enum {
    SmithExplain_Ok,
    SmithExplain_Missing, // no such variable, member or index: not a value
    SmithExplain_Optimized,
    SmithExplain_Unreadable,
} SmithExplainState;

// Attaches to pid and picks the thread that holds smith's failure. False, with the reason in problem, when it cannot.
bool smith_explain_attach(long pid, const char *self, char *problem, size_t capacity);
// The frames of that thread, innermost first; returns how many.
size_t smith_explain_frames(SmithExplainFrame *out, size_t capacity);
// Reads the variable path (`a.b`, `p->q[1]`) in the frame as `frame variable` does. out gets the value as text and
// integer, when not null, its value as a signed integer.
SmithExplainState smith_explain_read(size_t frame, const char *path, char *out, size_t capacity, long long *integer);
// Lets the process run on.
void smith_explain_detach(void);

// One variable of a frame, its value formatted as a failed check's values are.
typedef struct {
    char label[128];
    char value[200];
} SmithExplainVariable;

// The arguments and locals of the frame that are in scope, in the order lldb lists them; returns how many.
size_t smith_explain_variables(size_t frame, SmithExplainVariable *out, size_t capacity);

// `smith dbg --crash` and `--at`: a program run under lldb (shim/lldb_explain.cpp), stopped at a crash or a line.
typedef enum {
    SmithCrash_Stopped,
    SmithCrash_Exited,
} SmithCrashState;

typedef struct {
    SmithCrashState state;
    int status;         // the exit status, or the exit status a crash stands for (128 plus the signal, else 1)
    bool at_breakpoint; // stopped at the line of --at
    char reason[1024];  // why it stopped, as lldb says it
} SmithCrashStop;

// Launches program (argv[0] is its path; args end with nullptr) under lldb with its standard input, output and error
// as they are, and runs it to its first stop or its exit. at_file is "" for no breakpoint. False, with the reason in
// problem, when it cannot be debugged; then nothing was run.
bool smith_crash_start(const char *self, const char *const *args, const char *at_file, unsigned at_line,
                       SmithCrashStop *stop, char *problem, size_t capacity);
// How many places the breakpoint of smith_crash_start has.
size_t smith_crash_locations(void);
// Runs on from a stop to the next one or the exit.
void smith_crash_continue(SmithCrashStop *stop);
// Ends the program.
void smith_crash_kill(void);
