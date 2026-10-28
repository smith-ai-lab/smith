// A C interface to lldb-dap's own main, linked into smith as `smith dap`: a Debug Adapter Protocol server on stdio (or
// `--connection`), for editors and agents. scripts/llvm.sh renames the symbol of its main (tools/lldb-dap/tool), so
// every option and every message is lldb-dap's own. lldb's static initializers are deferred (tool.h).
#include "tool.h"

extern "C" void smith_lldb_slice_formatters(void);
extern "C" int smith_lldb_dap_tool_main(int argc, char **argv);

// Runs lldb-dap on argv[1 .. argc), as argv[0] (a path in smith's programs folder). Returns its exit status. A debug
// object's compilation directory is `.` (issue #621): `source_map`, when not null, is the lldb command that maps it to
// the project root, run before the client's requests (a sourceMap or sourcePath of its launch replaces it).
extern "C" int smith_lldb_dap_main(int argc, char **argv, const char *source_map) {
    smith_tool_start("__smith_lldb", argv[0]);
    smith_lldb_slice_formatters();
    constexpr int capacity = 512;
    if (source_map == nullptr || argc + 2 >= capacity) return smith_lldb_dap_tool_main(argc, argv);
    char *args[capacity];
    int n = 0;
    args[n++] = argv[0];
    args[n++] = const_cast<char *>("--pre-init-command");
    args[n++] = const_cast<char *>(source_map);
    for (int i = 1; i < argc; i++)
        args[n++] = argv[i];
    args[n] = nullptr;
    return smith_lldb_dap_tool_main(n, args);
}
