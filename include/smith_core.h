// The core of smith as a library (issue #646): smith.c compiled with -DSMITH_CORE has no main and exports the entry
// points below, which the units runner links so that a test calls them in process. Not shipped to users (the headers
// smith writes into a project are listed in build.c; this one is not). Each entry point is safe to call again in the
// same process.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The translation of a Makefile of `folder` (runtime/make*.c, as `smith build --dump-config` run there reads it, but
// not formatted): the build.c in `build` and what is not translated (`smith: Makefile:1: not translated: ...`, one
// line each) in `errors`, both NUL-terminated (and cut short when they do not fit). `makefile` is its name, or nullptr
// for the one `smith build` finds (GNUmakefile, makefile, Makefile). False when a buffer cannot be made, the folder
// cannot be entered or it has no Makefile. A Makefile smith refuses ends the process.
bool smith_core_make_translate(const char *folder, const char *makefile, char *build, size_t build_size, char *errors,
                               size_t errors_size);

// An archive that is in memory (a .tar, .tar.gz, .tar.xz or a .gz of one file, runtime/untar.c, gzip.c and xz.c, as
// `smith fetch` reads the download) extracted into the existing folder `folder`, and the package it makes of it (the
// one top-level folder when there is exactly one, else `folder`) put in `contents`. `url` is where it came from: it is
// the name of the file a .gz of one file becomes (the last part without ".gz") and it begins the error. On a refusal
// (a name that leaves the package, a damaged or cut-off archive, a special file) the result is false and `error` has
// the message, `cannot extract <url>: <reason>`; both are NUL-terminated and cut short when they do not fit. What was
// extracted before the refusal stays in `folder`.
bool smith_core_extract(const uint8_t *archive, size_t size, const char *url, const char *folder, char *contents,
                        size_t contents_size, char *error, size_t error_size);

// The files under `folder` as text for a test to compare, one line for each entry sorted as bytes, with paths relative
// to `folder`: `<path> d`, `<path> l -> <target>` and `<path> f <mode, octal> <size> <CRC-32 of the bytes, hex>
// <mtime>`. False when it does not fit or `folder` cannot be read.
bool smith_core_tree(const char *folder, char *out, size_t out_size);
