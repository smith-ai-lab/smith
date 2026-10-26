// A C interface to lldb-server's own main, linked into smith on Linux: lldb debugs a process there through lldb-server
// (`gdbserver`, `platform`), which it starts as LLDB_DEBUGSERVER_PATH. smith.c makes that path a clone of smith named
// lldb-server in its programs folder and runs this for it, so the debugger needs no program beside smith.
// scripts/llvm_linux.c renames the symbol of its main (tools/lldb-server); lldb's static initializers are deferred
// (tool.h).
#include "tool.h"

extern "C" int smith_lldb_server_tool_main(int argc, char **argv);

// Runs lldb-server on argv[1 .. argc), as argv[0] (the clone in smith's programs folder). Returns its exit status.
extern "C" int smith_lldb_server_main(int argc, char **argv) {
    smith_tool_start("__smith_lldb", argv[0]);
    return smith_lldb_server_tool_main(argc, argv);
}
