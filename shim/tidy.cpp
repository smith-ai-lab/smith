// A C interface to clang-tidy's own main, linked into smith as `smith lint`.
//
// clangTidyMain is the function clang-tidy's main calls (clang-tidy/tool/ClangTidyMain.cpp), so every flag, .clang-tidy
// file, check, fix and diagnostic is clang-tidy's own. See tool.h for how its initializers and its resource directory
// are handled.
#include "clang-tidy/tool/ClangTidyMain.h"
#include "tool.h"

// Runs clang-tidy on argv[1 .. argc), as argv[0], <folder>/bin/clang-tidy, in clang's folder. Returns
// its exit status.
extern "C" int smith_tidy_main(int argc, char **argv) {
    smith_tool_start("__smith_tidy", argv[0]);
    return clang::tidy::clangTidyMain(argc, const_cast<const char **>(argv));
}
