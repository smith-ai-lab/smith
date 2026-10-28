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

// The lowering of a `.smith` file without a compiler (runtime/lang*.c, as `smith cc --smith-lower` and `smith cc
// --smith-header` run it, issue #646), in process: the C text it prints (smith_core_lower) or the header it writes
// (smith_core_header) in `out`, and its errors, one `file:line:column: error: message` a line, in `errors`; both are
// NUL-terminated. `file` is the path the errors name and the imports are found from, relative to `folder` (nullptr:
// the current folder); `text` is the source, or nullptr to read `file`. The result is false when the lowering reports
// an error (as the command's status 1), a buffer is too small or the file cannot be read. The header pass does not
// ask the compiler which import declared a C name (the command does, with a process): a module with several C imports
// keeps them all, so such a module's header is compared through the command. A module that imports more modules than
// smith_raw_modules_max ends the process, as the command does.
bool smith_core_lower(const char *folder, const char *file, const char *text, char *out, size_t out_size, char *errors,
                      size_t errors_size);
bool smith_core_header(const char *folder, const char *file, const char *text, char *out, size_t out_size, char *errors,
                       size_t errors_size);

// The order in which smith writes the headers of its .smith modules (smith.c, smith_write_lang_headers): `names[i]` is
// module i and `texts[i]` its source. waves[i] is the wave module i's header is written in: 0 when it imports no other
// of the modules, else one more than the latest of those it imports; modules of one wave are written at the same time.
// An import cycle ends in one last wave of the modules left. Returns the number of
// waves (0 for more than 32 modules).
size_t smith_core_header_waves(const char *const *names, const char *const *texts, size_t count, size_t *waves);
