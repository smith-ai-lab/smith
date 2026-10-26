// A C interface to lldb's own driver main, linked into smith as `smith dbg`.
//
// lldb's main is tools/driver/Driver.cpp's, with its options; scripts/llvm.sh renames its symbol, so every flag
// (`-b`, `-o`, `-s`), command, prompt and exit status is lldb's own. The debugserver it launches is the system's
// (smith.c sets LLDB_DEBUGSERVER_PATH when the caller has not). lldb's static initializers are deferred (tool.h).
#include "tool.h"

extern "C" void smith_lldb_slice_formatters(void);
extern "C" int smith_lldb_driver_main(int argc, const char **argv);

// Runs lldb on argv[1 .. argc), as argv[0] (a path in smith's programs folder). Returns its exit status.
extern "C" int smith_lldb_main(int argc, char **argv) {
    smith_tool_start("__smith_lldb", argv[0]);
    smith_lldb_slice_formatters();
    // `run` and `r` are aliases of `process launch --shell-expand-args true`, whose shell gives up after ~10 s of wall
    // time and globs what the user quoted. smith launches without one, like `smith dap` and `smith mcp`;
    // `process launch -X true` still expands. `-O` commands run before the user's own.
    static const char *const no_shell[] = {"-O", "command unalias run",
                                           "-O", "command unalias r",
                                           "-O", "command alias run process launch -X false --",
                                           "-O", "command alias r process launch -X false --"};
    constexpr int extra = sizeof(no_shell) / sizeof(no_shell[0]);
    constexpr int capacity = 512;
    if (argc + extra >= capacity) return smith_lldb_driver_main(argc, const_cast<const char **>(argv));
    const char *args[capacity];
    int n = 0;
    args[n++] = argv[0];
    for (const char *option : no_shell) args[n++] = option;
    for (int i = 1; i < argc; i++) args[n++] = argv[i];
    args[n] = nullptr;
    return smith_lldb_driver_main(n, args);
}
