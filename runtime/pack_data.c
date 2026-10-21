// clang's resource pack (scripts/pack.h), embedded: the one translation unit that holds it, so that nothing the editor
// opens carries tens of MB of data. smith.c reads it by the names scripts/pack.h declares. SMITH_CLANG_RESOURCE is its
// path, which build.c passes.
#include "../scripts/pack.h"

const unsigned char smith_clang_resource[] = {
// NOLINTNEXTLINE(bugprone-string-literal-with-embedded-nul): a binary file, embedded on purpose
#embed SMITH_CLANG_RESOURCE
};
const size_t smith_clang_resource_size = sizeof smith_clang_resource;
