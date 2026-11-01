// The package fetch in process (issue #1298, part of #646), through `smith_core_package_fetch` (runtime/package.c in
// smith.c compiled as a library by tests/units/build.c, as include/smith_core.h). What `smith fetch` and
// `smith_package` do for a URL, a hash and a cache: download, SHA-256, extract, cache by hash, offline use, nothing
// left after a failure. Only file:// URLs are fetched by a test; nothing touches the network. Safe to call again in the
// same process: it keeps no state between calls, and it returns its error where the command ends the process. Not
// shipped.
#pragma once
#include <stddef.h>

// Makes sure the package of `url` is in the cache and puts its folder in `folder_out` (`size` bytes). The cache is
// `xdg_cache_home`/smith/packages, or `home`/.cache/smith/packages when `xdg_cache_home` is nullptr or empty, as
// $XDG_CACHE_HOME and $HOME say it for the command; both nullptr read the process's own variables, which a test does
// not set. `hash` is the "sha256:<64 hex digits>" a build.c names (a cached package is not fetched again, a wrong or
// malformed hash is an error) or nullptr, as `smith fetch` runs it (the archive is always downloaded). `offline` turns
// a package that is not cached into an error naming the URL. `hash_out` (72 bytes, or nullptr) gets the archive's
// "sha256:<hex>", the line `smith fetch` prints. On failure the result is false, `folder_out` and `hash_out` are
// empty and `errors` has the message the command prints (it names the URL, and a wrong hash both hashes); all are
// NUL-terminated and cut short when they do not fit.
bool smith_core_package_fetch(const char *xdg_cache_home, const char *home, const char *url, const char *hash,
                              bool offline, char *folder_out, size_t size, char *hash_out, char *errors,
                              size_t errors_size);
