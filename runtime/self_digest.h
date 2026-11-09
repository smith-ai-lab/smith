// The digest of a smith by its code (#2833): what runtime/self_digest.c shares with the other files.
#pragma once

#include "core.h"
#include "package.h"

// The digest of the smith at `path` with its commit and its build-id blanked: it changes with the code smith was built
// from and not with the commit it carries. Hex in `hex`, false when the file cannot be read (#2833).
bool smith_self_digest(const char *path, char hex[smith_sha256_hex_size]);
