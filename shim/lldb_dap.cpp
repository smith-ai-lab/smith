// A C interface to lldb-dap's own main, linked into smith as `smith dap`: a Debug Adapter Protocol server on stdio (or
// `--connection`), for editors and agents. scripts/llvm.sh renames the symbol of its main (tools/lldb-dap/tool), so
// every option and every message is lldb-dap's own. lldb's static initializers are deferred (tool.h).
#include "tool.h"

extern "C" int smith_lldb_dap_tool_main(int argc, char **argv);

// Runs lldb-dap on argv[1 .. argc), as argv[0] (a path in smith's programs folder). Returns its exit status.
extern "C" int smith_lldb_dap_main(int argc, char **argv) {
    smith_tool_start("__smith_lldb", argv[0]);
    return smith_lldb_dap_tool_main(argc, argv);
}
