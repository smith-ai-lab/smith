---
type: design
title: Packages, smith_package and smith fetch
description: A package is a URL and a hash fetched by smith's own HTTPS client into the cache, with no registry and no lock file.
tags: [packages, fetch]
---
# Packages, smith_package and smith fetch

`smith fetch <url>` downloads a package archive into the cache (`$XDG_CACHE_HOME/smith/packages/<hex>/`, else
`~/.cache/smith/...`) and prints the `.hash = "sha256:…",` line for build.c's `smith_package`. A package is a URL and
a hash, fetched with smith's own HTTPS client and extracted by smith itself (`.tar`, `.tar.gz`, `.tar.xz`, a `.gz` of one file; no other
process); the hash is the lock (no registry, no lock file). `-DOFFLINE` forbids fetching. macOS only for now.

A package is an archive named in build.c by a URL and the SHA-256 of its bytes, as in Zig. There is no registry, no
version solving and no lock file: the hash is the lock, so the same hash always gives the same bytes.

```c
smith_build_PackageResult cmake_package = smith_package(b, &(SmithPackage){
    .url = "https://github.com/Kitware/CMake/releases/download/v4.1.2/cmake-4.1.2-macos-universal.tar.gz",
    .hash = "sha256:...", // `smith fetch <url>` prints this line
});
// cmake_package.ok is "<cache>/smith/packages/<hex>"; bin/cmake is under it. A fetch that fails or a wrong hash is
// .error (kind Fetch, Hash or Offline, and a message); a missing .url or .hash fails the build.
```

- **Cache:** `$XDG_CACHE_HOME/smith/packages/<hex>/`, or `~/.cache/smith/packages/<hex>/`, keyed by the archive's hash.
  `smith_package` fetches, verifies and extracts on the first use and afterwards only looks the folder up, so a build
  works offline once it has fetched. It returns the absolute path, which is stable and can go in steps, `-I` flags and
  objects. An archive with one top-level folder is returned as the contents of that folder.
- **Safe to interrupt:** the archive goes to a temporary file, its SHA-256 is checked, it is extracted into a temporary
  folder, and only then is the folder renamed to its hash. A folder named by a hash is therefore always complete; a
  killed fetch leaves only `.tmp-*` names, which nothing reads.
- **Errors:** a wrong hash names the URL, the expected hash and the actual one. `-DOFFLINE` makes a package missing from
  the cache an error that names its URL, with no fetch. build.c runs on every `smith build`, so a package is fetched
  when build.c asks for it, before the executor runs.
- **`smith fetch <url>`** downloads the archive, prints `.hash = "sha256:...",` to paste into build.c, and leaves the
  package in the cache.
- **Download** is smith's own HTTPS client (`lib/http.c` over `io.net`, Network.framework with the system's trust;
  `file://` copies a local file, for tests). It follows redirects, https only (a redirect to another scheme is an
  error), streams the body to the temporary file a block at a time, fails a server that sends nothing for 60 s with
  `ETIMEDOUT`, and reports statuses, redirect loops, short bodies and TLS failures as messages that name the URL.
  smith.c and the build runtime include the client with the libraries it needs (`arena`, `random`, `io`,
  `http.c`; the runtime gets the headers from `.smith/include/`, `.smith/internal/http.c`, and links the `.smith` modules
  `.smith/lib/smith/*.smith` that its manifest compiles (knowledge/smith-bootstrap.md), and `smith_library` does not offer
  http until a second project needs it); Network.framework is
  loaded with `dlopen` on the first download, so nothing else pays for it. SHA-256 is in smith, checked against the NIST
  test vectors in `tests/build_*_test.smith`. The network cases are tested in `tests/package_checks_test.smith` (a real cache, a scripted
  network: redirects, a refused cross-scheme redirect, 404, a short body, a broken connection, a wrong hash, a TLS
  failure, leftovers of a killed fetch). macOS only for now.
- **Extraction** is smith's own code and runs no process: see [Archives](archives.md) below.
