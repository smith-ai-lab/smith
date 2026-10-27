// What the language server's layer (runtime/lsp*.smith) stands on and no .smith module offers yet: threads and locks,
// pipes and descriptors, the process's place, files, the clock and the environment, a child started in a folder, and
// the names the lowering made, which are C++ (shim/names.h). Each group names the gap and the C file or the shim that
// fills it, so that the group goes when the gap closes. The functions are C23, libc only (runtime/lsp_edge.c), except
// the last groups (shim/lsp_edge.cpp), whose C++ is the edge.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Threads and locks: .smith has no thread module (issue #556). runtime/lsp_edge.c.

typedef void (*SmithLspBody)(void *context);

// Starts a thread that runs body(context); false when the system has no more threads.
bool smith_lsp_thread_start(uint64_t *thread, SmithLspBody body, void *context);
// Waits for a thread to end.
void smith_lsp_thread_join(uint64_t thread);
// Lets a thread run to its end on its own.
void smith_lsp_thread_detach(uint64_t thread);

// The locks of the layer: one for each thing that two threads touch.
typedef enum {
    SmithLspLock_Layer,  // the requests waiting for an answer
    SmithLspLock_Editor, // the open documents and the saved ones
    SmithLspLock_Send,   // the pipe to the server
    SmithLspLock_Count,
} SmithLspLock;

void smith_lsp_lock(SmithLspLock which);
void smith_lsp_unlock(SmithLspLock which);

// ---- Pipes and descriptors: smith::files has no pipe, no duplicate, no redirect and no wait for input (issue #557).
// runtime/lsp_edge.c.

// A pipe: what is written to `write_end` is read from `read_end`.
bool smith_lsp_pipe(int *read_end, int *write_end);
// A second descriptor for the same file; -1 when there is none.
int smith_lsp_duplicate(int descriptor);
// Makes `onto` the file `from` is and closes `from`: the process's standard input or output becomes a pipe.
bool smith_lsp_redirect(int from, int onto);
// Whether a read of `descriptor` would not block (it has bytes, or its end), within `milliseconds`.
bool smith_lsp_wait_readable(int descriptor, int milliseconds);

// ---- The process's place: smith::files has no working folder, no real path and no modification time (issue #558).
// runtime/lsp_edge.c.

// The working folder, terminated, in out; false when it does not fit.
bool smith_lsp_working_folder(char *out, size_t size);
// The path with its links resolved, terminated, in out; false when there is none or it does not fit.
bool smith_lsp_real_path(const char *path, char *out, size_t size);
// Whether both files are there and `path` was modified at or after `than`.
bool smith_lsp_modified_at_or_after(const char *path, const char *than);
int smith_lsp_process_id(void);

// ---- Files, descriptors, the clock and the environment: smith::files, console, clock and env stand on the handlers
// runtime (lib/handlers.c), which programs link and smith does not, so the .smith of smith itself can not call them
// (issue #564). runtime/lsp_edge.c; runtime/lsp_world.smith is what the layer calls.

// The bytes of a regular file; -1 when there is none.
int64_t smith_lsp_file_size(const char *path);
// Reads exactly `size` bytes of the file; false when it can not.
bool smith_lsp_file_read(const char *path, uint8_t *buffer, size_t size);
// Whether the path is there.
bool smith_lsp_exists(const char *path);
// Up to `size` bytes of the descriptor; 0 at its end, -1 when it fails.
ptrdiff_t smith_lsp_read(int descriptor, uint8_t *buffer, size_t size);
// All of the bytes to the descriptor; false when it fails.
bool smith_lsp_write_all(int descriptor, const uint8_t *data, size_t size);
void smith_lsp_close(int descriptor);
// Monotonic nanoseconds.
uint64_t smith_lsp_now(void);
// The value of an environment variable, or null.
const char *smith_lsp_environment(const char *name);
// A block of memory that lasts as long as the process, for what its threads share.
void *smith_lsp_memory(size_t *size);

// ---- A child in a folder: smith::process starts a program with neither a folder, nor an addition to its environment,
// nor a quiet standard input and output, nor a lower priority (issue #559). shim/lsp_edge.cpp (the platform layer).

// Runs smith itself (the running executable) with the arguments, terminated, in `directory`, with
// SMITH_EFFECTS_OBJECT=<object> added to its environment, its standard streams on /dev/null and a lower priority, and
// waits for it.
void smith_lsp_summarize(const char *directory, const char *object, const char *const *arguments, size_t count);

// ---- The effects plugin (shim/effects_infer.cpp) is told what the editor does.

void smith_effects_editor_start(void (*listener)(const char *source));
void smith_effects_editor_object(const char *source, const char *object);
void smith_effects_editor_saved(const char *source);
void smith_effects_editor_pending(int pending);

// ---- The names the lowering made: shim/names.h is C++ (std::string, std::vector, shared_ptr), so the layer reads a
// file's names through a handle (issue #560). shim/lsp_edge.cpp. Strings are a pointer and a length, valid until the
// handle is closed.

typedef struct {
    const char *data;
    size_t length;
} SmithLspText;

typedef struct {
    SmithLspText written; // as the file writes it
    SmithLspText member;  // after `x::`
    SmithLspText module;  // empty for none
} SmithLspWritten;

typedef struct {
    uint32_t line; // from 0
    uint32_t column;
    uint32_t length;
    SmithLspText hover;
    SmithLspText file; // where the generic is written
    uint32_t definition_line;
    uint32_t definition_column;
} SmithLspCall;

typedef struct SmithLspNames SmithLspNames;

// What the last parse of the file at `path` (terminated) that reached its end learned; null for none.
SmithLspNames *smith_lsp_names_open(const char *path);
void smith_lsp_names_close(SmithLspNames *names);
// The name the lowering made `lowered`.
bool smith_lsp_names_find(const SmithLspNames *names, SmithLspText lowered, SmithLspWritten *found);
size_t smith_lsp_names_call_count(const SmithLspNames *names);
bool smith_lsp_names_call(const SmithLspNames *names, size_t index, SmithLspCall *call);

// What a completion that stopped at `x::` found at the cursor (a copy).
typedef struct SmithLspQualifier SmithLspQualifier;

// Null when the last parse of the file saw no `x::` at the cursor.
SmithLspQualifier *smith_lsp_qualifier_open(const char *path);
void smith_lsp_qualifier_close(SmithLspQualifier *qualifier);
bool smith_lsp_qualifier_is_c(const SmithLspQualifier *qualifier);
uint32_t smith_lsp_qualifier_line(const SmithLspQualifier *qualifier);
uint32_t smith_lsp_qualifier_column(const SmithLspQualifier *qualifier);
SmithLspText smith_lsp_qualifier_module(const SmithLspQualifier *qualifier);
SmithLspText smith_lsp_qualifier_partial(const SmithLspQualifier *qualifier);

#ifdef __cplusplus
}
#endif
