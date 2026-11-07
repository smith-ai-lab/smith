// What every file of smith shares before anything else: the system headers and the shims (#2685, split of smith.c).
#pragma once

#ifdef __linux__
// What glibc hides from C23 and macOS does not: nftw's FTW_PHYS, wait4 (knowledge/platform-layer.md).
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming): glibc's name for it
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/sysctl.h> // runtime/slots.c reads a process through sysctl on macOS
#endif
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#include "base.h"
#include "../scripts/pack.h"

#include <dirent.h>
#include <ftw.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>

// SMITH_CLANG_ID is the version of the clang linked in and a hash of what is linked (build_llvm_run_macos), compiled in
// by build.c; --smith-which prints it, so that what depends on the compiler is rebuilt for a new LLVM and for nothing
// else. shim/clang.cpp, the only C++ in smith: clang's own main, run as `program` (a clang or clang++ in the folder
// that smith_resource_ensure fills with clang's resource directory) on argv[1 .. argc). argv[0] is ignored.
int smith_clang_main(const char *program, int argc, char **argv, bool lld, const char *resource);
#ifdef SMITH_PROVE
// shim/prove.cpp: `smith prove`, on argv[0 .. argc), compiling with the clang that smith_clang_main runs as `program`
// (with `resource` as its resource directory).
int smith_prove_main(const char *program, const char *resource, int argc, char **argv);
#endif
// shim/lld.cpp: lld's Mach-O linker, run as ld on argv[1 .. argc). Exits the process when it is done.
int smith_lld_main(int argc, char **argv);
// shim/format.cpp, shim/tidy.cpp and shim/clangd.cpp: the mains of clang-format, clang-tidy and clangd, each run on
// argv[1 .. argc), as argv[0], a path in this smith's programs folder (smith_programs_folder, beside clang's), whose
// resource directory clang-tidy and clangd use.
int smith_format_main(int argc, char **argv);
int smith_tidy_main(int argc, char **argv);
int smith_clangd_main(int argc, char **argv);
// shim/clangd.cpp: the files clangd formats and runs its checks by in a project that has none (smith's defaults).
void smith_clangd_defaults(const char *style, const char *tidy);
#ifdef SMITH_LLDB
constexpr bool smith_has_lldb = true;
// shim/lldb.cpp and shim/lldb_dap.cpp: the mains of lldb and lldb-dap (`smith dbg`, `smith dap`), run the same way. A
// smith built against an LLVM install without lldb (build_llvm_run_macos, the lldb step) runs an external lldb instead.
int smith_lldb_main(int argc, char **argv);
int smith_lldb_dap_main(int argc, char **argv);
#ifdef __linux__
int smith_lldb_server_main(int argc, char **argv); // shim/lldb_server.cpp: lldb's debug server on Linux
#endif
#else
constexpr bool smith_has_lldb = false;
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif

// The modules and libraries every file of the old unity saw.
#include <elf.h>
#include <fnmatch.h>
#include <glob.h>
#include <linux/fs.h>
#include <strings.h>
#include <runtime/io.h>
#include <smith/arena.h>
#include <smith/contract.h>
#include <smith/files.h>
#include <smith/handlers.h>
#include <smith/http.h>
#include <smith/inflate.h>
#include <smith/json.h>
#include <smith/mem.h>
#include <smith/net.h>
#include <smith/process.h>
#include <smith/random.h>
#include <smith/text.h>
