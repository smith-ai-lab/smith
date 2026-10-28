// <smith/trace.h>: the stack of a failure, captured in the program and written as a few lines of text for smith to
// read. A program holds no symbolizer. When a check, a contract or an assert fails, or a fatal signal arrives, it walks
// its stack by frame pointers and writes the raw addresses, with the UUID and load address of each image they fall in.
// smith turns that text into file names, lines, source text and inlined frames, with LLVM's symbolizer: `smith build
// test` does it as the tests print (the failing frame, the line from disk, the call chain), and `smith symbolize` does
// it for a trace saved from a run anywhere.
//
// The text is a block of lines that start with `smith-trace`, indented or not:
//
//   smith-trace begin requires failed        the heading: what happened, in words
//   smith-trace site tests/a.c:42            optional: where the program says it happened (the old macros' file:line)
//   smith-trace detail <text>                one line of what was wrong (the values of a failed check): the report
//   prints
//                                            it under the source line
//   smith-trace image <uuid> <text> <slide> <size> <path>
//                                            a loaded image a frame falls in: the Mach-O UUID (or - when it has none),
//                                            the address of its __TEXT in memory, its slide (load address minus link
//                                            address), the size of its __TEXT, and its path
//   smith-trace pc 0x1000014e8 [name+off]    the instruction a signal stopped at: an exact address
//   smith-trace frame 0x1000014c0 [name+off] a return address; its call is the instruction before it
//   smith-trace end
//
// A detail line that starts `@ ` is not a failed check's value but a variable read from the stopped frame by lldb
// (`@ moves.count = 7`), written by the test runner's debugger step; `@! text` says why none was read. smith symbolize
// prints them under the source line (`smith test` reads them with `smith --smith-explain`).
//
// name+off is the nearest exported symbol, written when the writer was not in a signal handler; it names the frames
// of system libraries, whose debug info is not on disk. Frames are listed innermost first.
//
// Frame pointers are what the walk follows, so a program keeps them: Apple's arm64 and x86-64 ABIs always do, and a
// Linux build passes -fno-omit-frame-pointer, as the sanitizers already ask.
#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t smith_trace_frames_max = 64;

// A walk ends after the frame whose caller's frame pointer is smith_trace_stop, when it is not 0: the test runner sets
// it to the frame of the function that calls a test, so a stack ends there, the same however the runner was reached.
extern uintptr_t smith_trace_stop;

// Walks the stack of the caller by frame pointers: frames[0] is the address in the caller that smith_trace_capture
// returns to, and the rest are its callers' return addresses. Returns the count, at most capacity. Safe to call from a
// signal handler.
[[nodiscard]] size_t smith_trace_capture(uintptr_t *frames, size_t capacity);

// Walks the stack a signal interrupted, from the ucontext the handler (a SA_SIGINFO one) was given. frames[0] is the
// exact instruction, the rest return addresses. Safe to call from a signal handler.
[[nodiscard]] size_t smith_trace_capture_signal(uintptr_t *frames, size_t capacity, const void *ucontext);

// Writes the block to fd, each line starting with indent. exact says frames[0] is an instruction rather than a return
// address (a signal's). names looks up the nearest symbol of each frame, which is not safe in a signal handler: pass
// false there. site is "file:line" or nullptr. details is text, one `smith-trace detail` line per line of it (a failed
// check's values), or nullptr. Everything else it does is safe in a signal handler.
void smith_trace_write(int fd, const char *indent, const char *heading, const char *site, const char *details,
                       const uintptr_t *frames, size_t count, bool exact, bool names);

// What failed. A contract or a check of a library that cannot include <smith/test.h> calls smith_failure(kind).
typedef enum : unsigned char {
    SmithFailure_Check,
    SmithFailure_Requires,
    SmithFailure_Ensures,
    SmithFailure_Invariant,
    SmithFailure_Assert,
    SmithFailure_Capacity,
    SmithFailure_Memory,
} SmithFailureKind;

// "check failed", "requires failed", "ensures failed", "invariant failed", "assert failed", "arena out of capacity",
// "memory error" (a double free, a free of a block the allocator did not make).
[[nodiscard]] const char *smith_failure_heading(SmithFailureKind kind);

// The entry point of plain-function checks and contracts (smith_requires, smith_ensures, smith_invariant, smith_assert
// and a library's own checks): it ends the program, or the test, with a report. It captures the stack of its caller, so
// the failing line is the caller's, and a function that calls it is skipped when the frame is chosen if its name starts
// with smith_failure, smith_contract_, smith_check, smith_requires, smith_ensures, smith_invariant or smith_assert. It
// does not capture file, line or text: smith reads the line from the source file.
//
// In a test run by smith_tests, the test fails with the report. Anywhere else it writes the heading and the trace to
// standard error and calls abort(), so a log holds what `smith symbolize` needs.
[[noreturn, clang::annotate("smith_compile_error")]] void smith_failure(SmithFailureKind kind);

// What a debug build writes for a check that `smith prove` proved (issue #325): the same failure, a name the effects
// check knows is no failure point, because the proof says it cannot happen, so the verdict of a `fails` ceiling is the
// same in every build while the check stays as a safety net. The lowering writes it, never you.
[[noreturn]] void smith_failure_proved(SmithFailureKind kind);

// smith_failure with one line of what was wrong (`smith-trace detail <text>`), which the report prints under the source
// line: the arena's out-of-capacity stop says what was asked and what was left. The text is copied before anything
// else runs; in a test the hook fails the test with the heading, as for smith_failure.
[[noreturn]] void smith_failure_detail(SmithFailureKind kind, const char *details);

// smith_failure_detail with the line formatted like printf's (at most 255 characters): `smith_failure_format(kind,
// "push of %zu bytes", size)`. A caller that wants no snprintf of its own, and no macro of the C library, uses this.
[[noreturn, gnu::format(printf, 2, 3)]] void smith_failure_format(SmithFailureKind kind, const char *format, ...);

// What smith_failure does instead of writing and aborting; smith_tests installs it in the test runner. It must not
// return. frames and count are the caller's stack, as smith_trace_capture gives it.
extern void (*smith_failure_hook)(SmithFailureKind kind, const uintptr_t *frames, size_t count);

// No macOS crash report for a deliberate crash (issue #622). ReportCrash spends about half a core per crashed process
// under load, and the test runner crashes processes on purpose. Setting the crash exception ports of the task to none
// leaves the kernel to deliver the plain signal, which the runner reads as ever; children inherit the ports. Linux has
// no such cost: both functions do nothing there. smith_crash_handlers counts the crash exception types that still have
// a handler (the test of the above).
void smith_silence_crash_reports(void);
unsigned smith_crash_handlers(void);
