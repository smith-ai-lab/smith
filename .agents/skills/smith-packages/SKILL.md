---
name: smith-packages
description: "Dependencies and caches in smith: smith fetch <url>, smith_package in build.c (a URL and a sha256 hash, no registry), the package cache, smith cache and smith cache clean, -DOFFLINE, the Clang modules cache. Load to add a third-party library or archive to a build, on 'sha256', 'hash mismatch', 'a zip is refused', 'package missing from the cache', when the disk fills with ~/.cache/smith, or to reset a cache."
---

# Packages and caches

This skill ships in the smith plugin, from `.agents/skills/smith-packages/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there. The call's other options are in `smith-build`; design in `docs/build-c.md` ("Packages").

A package is an archive named by a URL and the SHA-256 of its bytes. There is no registry, no version solving and no lock file: the hash is the lock. A project that uses one needs a build file (the conventions have no packages: `smith-build-levels`).

## Add one

```sh
smith fetch https://example.com/lib-1.2.tar.gz
# prints:  .hash = "sha256:9f2c...",
```

`smith fetch` downloads with smith's own HTTPS client (redirects to https only, the system's certificate checks), stores the archive in the cache and prints the line to paste. Then, in build.c:

```c
smith_build_PackageResult found = smith_package(b, &(SmithPackage){
    .url = "https://example.com/lib-1.2.tar.gz",
    .hash = "sha256:9f2c...",
});
if (found.tag == smith_enum_result_const_char_ptr__SmithPackageError_tag_error) {
    fprintf(stderr, "%s\n", found.error.message); // .error.kind: SmithPackageKind_Fetch, _Hash or _Offline
    exit(2);
}
if (found.tag != smith_enum_result_const_char_ptr__SmithPackageError_tag_ok) __builtin_unreachable(); // so `.ok` reads as ok
const char *lib = found.ok;                      // the absolute path of the extracted contents
SmithObject o = {.compiler = "smith-cc", .source = smith_fmt(b, "%s/src/lib.c", lib), .output = "build/lib.o",
                 .flags = smith_list(b, "-I", smith_fmt(b, "%s/include", lib), nullptr)};
smith_object(b, &o);
```

(`build::package(b, &(build::Package){...})` in a build.smith is a `Result(const char *, build::PackageError)`: `.ok` the path, `.error` with `kind` Fetch, Hash or Offline and `message`. A missing `.url` or `.hash` is a mistake in the build file and fails the build.)

- The first call fetches, checks the hash and extracts (`.tar`, `.tar.gz`, `.tar.xz`, in smith itself; **a zip is refused**). A `.gz` that holds one file, not a tar (a release binary), is a package folder with that file in it, named as the URL without `.gz` and mode 0644 (`chmod +x` a copy); a `.xz` of one file is not read; later calls only look the folder up, so builds work offline. An archive with one top-level folder is returned as that folder's contents. The path is stable: use it in steps, `-I` flags and objects.
- Extraction refuses absolute names, `..`, a path through a symlink and a symlink that points outside the package. File modes and times are kept.
- `smith_package` runs every time build.c runs, before the executor: it is a lookup when cached.
- A wrong hash is a `PackageError` of kind Hash with both hashes (and the URL): paste the actual one only after you have confirmed the archive is the one you meant. `-DOFFLINE` turns a package missing from the cache into kind Offline naming its URL, with no fetch (`smith build -DOFFLINE`). macOS only for now.

## The caches

smith keeps two under `$XDG_CACHE_HOME/smith/` (else `~/.cache/smith/`): `packages/<hex>/` and `modules/` (Clang's module cache, shared by every project so the SDK's modules are built once per machine). One rule: an entry unused for 31 days is removed, checked at most once a week. Each root also has a byte cap, 10 GB or `SMITH_CACHE_MAX` (bytes, or `500M`, `10G`): over it, the least recently used entries go first, never one in use or the package just fetched; `smith cache` shows the cap. Both are keyed by hash, so a removed entry is fetched or built again.

- `smith cache` shows each cache's path, entries and size (`-v` lists the entries and their last use).
- `smith cache clean [modules|packages|lint]` empties them. It refuses while a `smith build` is running (builds hold a shared lock on `<cache>/smith/lock`).
- Not here: the compiler's runtime files (`$XDG_DATA_HOME/smith/clang/...`, `.../programs/...`) are outside these caches and are never removed by smith, because built sanitized binaries point into them (`smith-trace`). The project's `.smith/` is rebuilt by `rm -rf .smith`, always safe.
- Tests that touch caches must use a temporary `XDG_CACHE_HOME`; never clean the real one to fix a build.
