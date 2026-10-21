// What the shims of clang-format, clang-tidy and clangd (format.cpp, tidy.cpp, clangd.cpp) share. macOS only: it reads
// a Mach-O section through the executable's own header.
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

#include "clang/Options/OptionUtils.h"
#include "llvm/ADT/StringRef.h"

#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

#include <cstddef>
#include <string>

inline const char *smith_tool_program = nullptr; // argv[0] of the tool that is running: <folder>/bin/<tool>

// The call that scripts/llvm.sh redirects to. [[gnu::used]]: the objects that call it name it, nothing here does.
[[gnu::used]] inline std::string smith_resource_path(const char * /*argv0*/, void * /*main_address*/) {
    return clang::GetResourcesPath(llvm::StringRef(smith_tool_program));
}

// Starts the tool run as `program`: its resource directory, then its initializers from the section `name`.
inline void smith_tool_start(const char *name, const char *program) {
    smith_tool_program = program;
    unsigned long size = 0;
    auto *table = reinterpret_cast<void (**)()>(getsectiondata(&_mh_execute_header, "__DATA", name, &size));
    for (size_t i = 0; table != nullptr && i < size / sizeof(void (*)()); i++)
        table[i]();
}
