// A C interface to lldb's own driver main, linked into smith as `smith dbg`.
//
// lldb's main is tools/driver/Driver.cpp's, with its options; scripts/llvm.sh renames its symbol, so every flag
// (`-b`, `-o`, `-s`), command, prompt and exit status is lldb's own. The debugserver it launches is the system's
// (smith.c sets LLDB_DEBUGSERVER_PATH when the caller has not). lldb's static initializers are deferred (tool.h).
#include "tool.h"

extern "C" int smith_lldb_driver_main(int argc, const char **argv);

// Runs lldb on argv[1 .. argc), as argv[0] (a path in smith's programs folder). Returns its exit status.
extern "C" int smith_lldb_main(int argc, char **argv) {
    smith_tool_start("__smith_lldb", argv[0]);
    return smith_lldb_driver_main(argc, const_cast<const char **>(argv));
}
