// What the shims of clang-format, clang-tidy and clangd (format.cpp, tidy.cpp, clangd.cpp) share. The section of
// deferred initializers is read through the platform layer (shim/platform.h).
//
// 1. Static initializers. The tools declare their flags as global llvm::cl::opt objects, which LLVM aborts on when two
//    register the same flag, so scripts/defer_init.c moved each tool's initializers out of the section dyld runs at
//    load, into __DATA,__smith_<name>; the tool that runs runs them here, in the order the linker put the objects.
// 2. The resource directory. clangd and clang-tidy find the compiler's builtin headers and modules by the path of
//    their own executable, which is smith's, so scripts/llvm.sh redirects their one call that does it
//    (clang::GetResourcesPath(const char *, void *), in clangd's CompileCommands.cpp and in Tooling.cpp) to
//    smith_resource_path, which answers with the resource directory of the folder the tool is run in (its argv[0] is
//    <folder>/bin/<tool>): the folder smith writes from its embedded copy of clang's files on first use, which the
//    in-process clang treats as its own (smith.c, smith_resource_folder). With that, an editor sees the builtin headers
//    and the modules the compiler does, nothing is read from the LLVM install at run time, and clangd logs no error for
//    a smith that is not installed beside lib/clang.
#pragma once

#include "clang/Driver/CreateInvocationFromArgs.h"
#include "clang/Options/OptionUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include "platform.h"

#include <cstddef>
#include <string>
#include <vector>

extern "C" {
#include "../runtime/lang.h"
}

inline const char *smith_tool_program = nullptr; // argv[0] of the tool that is running: <folder>/bin/<tool>

// The call that scripts/llvm.sh redirects to. [[gnu::used]]: the objects that call it name it, nothing here does.
[[gnu::used]] inline std::string smith_resource_path(const char * /*argv0*/, void * /*main_address*/) {
    return clang::GetResourcesPath(llvm::StringRef(smith_tool_program));
}

// Starts the tool run as `program`: its resource directory, then its initializers from the section `name`.
inline void smith_tool_start(const char *name, const char *program) {
    smith_tool_program = program;
    size_t count = 0;
    void (**table)() = smith_platform_section(name, &count);
    for (size_t i = 0; i < count; i++)
        table[i]();
}

// The command of a .smith input as `smith cc` makes it (smith_lang_command, the one rule of runtime/lang_command.c:
// `-x c`, and `-std=gnu2y` unless it names a standard), for the places where a tool turns a command into a compile:
// clang::createInvocation (clangd: the open file, the preamble and the background index alike) and the tooling
// library's getStripPluginsAdjuster (clang-tidy). A .smith extension is a linker input to the driver, whatever a
// project's compile database says. clang::CommandHook (patches/smith-command-hook.patch) is where both ask; a command
// with no .smith input is left as it was.
inline bool smith_command_hook(llvm::ArrayRef<const char *> Args, llvm::SmallVectorImpl<const char *> &Out) {
    std::vector<char *> in;
    for (const char *arg : Args)
        in.push_back(const_cast<char *>(arg));
    std::vector<char *> out(in.size() + 16);
    int count = smith_lang_command((int)in.size(), in.data(), 1, out.data(), out.size() - 1);
    if (count <= 0) return false;
    Out.assign(out.begin(), out.begin() + count);
    return true;
}
