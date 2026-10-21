// A C interface to clang-format's own main, linked into smith as `smith fmt`.
//
// clang-format's main is ClangFormat.cpp's, with its options; scripts/llvm.sh renames its symbol. So every flag, style
// file, exit status and byte of output is clang-format's own. See tool.h for how its initializers are handled.
#include "tool.h"

extern "C" int smith_clang_format_main(int argc, const char **argv);

// Runs clang-format on argv[1 .. argc), as argv[0], <folder>/bin/clang-format, in clang's folder.
// Returns its exit status.
extern "C" int smith_format_main(int argc, char **argv) {
    smith_tool_start("__smith_fmt", argv[0]);
    return smith_clang_format_main(argc, const_cast<const char **>(argv));
}
