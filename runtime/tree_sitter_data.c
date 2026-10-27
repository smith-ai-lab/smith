// The tree-sitter parser for .smith files, compiled by `smith build` into a shared library (build_tree_sitter_steps in
// build.c), embedded: the one translation unit that holds it, which smith.c reads for `smith cc --smith-tree-sitter`.
// SMITH_TREE_SITTER_LIBRARY is its path, which build.c passes. The Linux seed does not link this file.
#include <stddef.h>

const unsigned char smith_tree_sitter_library[] = {
// NOLINTNEXTLINE(bugprone-string-literal-with-embedded-nul): a binary file, embedded on purpose
#embed SMITH_TREE_SITTER_LIBRARY
};
const size_t smith_tree_sitter_library_size = sizeof smith_tree_sitter_library;
