// A C interface to lldb's own driver main, linked into smith as `smith dbg`.
//
// lldb's main is tools/driver/Driver.cpp's, with its options; scripts/llvm.sh renames its symbol, so every flag
// (`-b`, `-o`, `-s`), command, prompt and exit status is lldb's own. The debugserver it launches is the system's
// (smith.c sets LLDB_DEBUGSERVER_PATH when the caller has not). lldb's static initializers are deferred (tool.h).
#include "tool.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" void smith_lldb_slice_formatters(void);
extern "C" int smith_lldb_driver_main(int argc, const char **argv);

// The project folder `path` is in: its nearest ancestor with a `.smith` folder. Empty when there is none.
static std::string project_of(const char *path) {
    char real[PATH_MAX];
    if (realpath(path, real) == nullptr) return "";
    std::string folder = real;
    for (int depth = 0; depth < 64 && !folder.empty(); depth++) { // bounded by the folder depth
        struct stat info;
        if (stat((folder + "/.smith").c_str(), &info) == 0 && S_ISDIR(info.st_mode)) return folder;
        size_t slash = folder.rfind('/');
        if (slash == std::string::npos || slash == 0) break;
        folder.resize(slash);
    }
    return "";
}

// For lldb_explain.cpp: the project folder of `path` in `out`, "" when there is none.
extern "C" void smith_lldb_project_of(const char *path, char *out, size_t capacity) {
    std::snprintf(out, capacity, "%s", project_of(path).c_str());
}

// A compile with debug info is told -ffile-prefix-map=<project>=., so its object names `.` where the folder was and is
// the same in every worktree (#1351). This command maps `.` back to the project, found from a file named on its command
// line or from the folder lldb runs in, so source lines show from any folder.
static std::string source_map_command(int argc, char **argv) {
    std::string project;
    for (int i = 1; i < argc && project.empty(); i++) {
        if (argv[i][0] != '-') project = project_of(argv[i]);
    }
    char here[PATH_MAX];
    if (project.empty() && getcwd(here, sizeof here) != nullptr) project = project_of(here);
    if (project.empty()) return "";
    std::string quoted;
    for (char c : project) {
        if (c == '"' || c == '\\') quoted += '\\';
        quoted += c;
    }
    return "settings set target.source-map . \"" + quoted + "\"";
}

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
    constexpr int extra = sizeof(no_shell) / sizeof(no_shell[0]) + 2;
    constexpr int capacity = 512;
    if (argc + extra >= capacity) return smith_lldb_driver_main(argc, const_cast<const char **>(argv));
    const char *args[capacity];
    int n = 0;
    args[n++] = argv[0];
    for (const char *option : no_shell)
        args[n++] = option;
    std::string map = source_map_command(argc, argv);
    if (!map.empty()) {
        args[n++] = "-O";
        args[n++] = map.c_str();
    }
    for (int i = 1; i < argc; i++)
        args[n++] = argv[i];
    args[n] = nullptr;
    return smith_lldb_driver_main(n, args);
}
