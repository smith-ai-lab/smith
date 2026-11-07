// The package cache, SHA-256 and the archive readers of smith (#2685): runtime/package.c and what it includes, one
// translation unit as the build runtime has it. package.c is embedded in every project's build program with its package.h, so
// neither includes smith's core.h; this file gives them the system headers.
#include "core.h"

#ifdef SMITH_CORE
struct stat;
struct timespec smith_platform_mtime(const struct stat *info); // platform_linux.c or platform_macos.c
void smith_core_package_environment(bool given, const char *xdg, const char *home); // package.c
#endif
#include "package.c"
