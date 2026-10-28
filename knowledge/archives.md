---
type: design
title: smith reads its archives itself
description: Gzip, xz and tar are read by smith's own C, and extraction refuses any name or link that could leave the package.
tags: [archives, untar]
---
# smith reads its archives itself

**smith reads its archives itself, in its own C** (`runtime/gzip.c`, `xz.c`, `untar.c`): DEFLATE is the library `smith::inflate` (`lib/smith/inflate.smith`, skill smith-inflate; also learner's), gzip's members loop is `gzip.c`, then LZMA2 and tar, no
vendored library and no zip, which no package uses. Extraction refuses absolute names, `..`, a path through a
symlink, and a symlink that points outside the package; it keeps file modes and times. Tests: `tests/untar_readers_test.smith`
(the readers as functions in process, #646), `tests/untar_*_test.smith` for the fetch around them and the sanitizers. See [the build file](build-file.md).

`smith_package` reads `.tar`, `.tar.gz` (or `.tgz`) and `.tar.xz`, told apart by their first bytes, never by the name.
These are the formats today's packages use: CMake's `.tar.gz`, Python's standalone build as `.tar.gz`, and LLVM's source
as `.tar.xz`. A `.gz` whose content is not a tar (the first 512 bytes are no tar header) is one file: the package is a
folder holding it, named as the URL without `.gz` (`file` when nothing is left), mode 0644, as tree-sitter's release is. A
zip is refused by name (`zip archives are not supported`), as are bzip2 and zstd; nothing needs them.

- **The code is smith's own**, not a vendored library: `lib/smith/inflate.smith` (DEFLATE, the zlib and gzip wrappers, CRC-32 and Adler-32: the library `smith::inflate`, skill smith-inflate, which learner uses too; `runtime/gzip.c` is the loop over a file's members),
  `runtime/xz.c` (the xz container, LZMA2, CRC-64 and SHA-256 checks; about 670) and `runtime/untar.c` (tar and the safe
  extraction; about 720). They are written in the rules of the rest of smith: no malloc (the LZMA2 dictionary is one
  anonymous `mmap` of the size the block asks for, at most 1 GiB, and the rest is static), every loop bounded by an input
  size that was checked, errors as values. Reasons: the formats are small and fully specified; the smaller vendored
  decoders (a 0BSD xz decoder such as xz-embedded, roughly 2,000 lines in its own style, or a public-domain inflate such
  as puff, roughly 800 lines that decode a bit at a time and run several times slower) would still need adapting to the
  rules above and to a streaming sink, and a vendored tree is a second style, license and update to keep; and every
  field the formats let a reader verify is verified, which a general-purpose library does not promise. The cost is the
  code itself, which the tests below hold to account.
- **What is verified:** gzip's header CRC, CRC-32 and size of every member (members may follow one another, and zero
  padding may follow the last); xz's header, block header and index CRC-32s, the sizes a block header states, each
  block's check (none, CRC-32, CRC-64 or SHA-256), the index against the blocks and the footer against the header (several
  streams with the padding xz allows between them are read); tar's header checksums and its two closing zero blocks.
  An archive that is cut off anywhere, in a header, a name, a file, a block or a trailer, is an error that says so.
  Filters other than LZMA2 (BCJ, delta) and other checks are refused by name.
- **Where it writes:** every entry's name is made relative and must not be absolute or contain `..`; every folder on the
  way is opened with `O_NOFOLLOW` from a descriptor of the extraction folder, so a symlink made earlier in the same
  archive cannot send a later file elsewhere; a symlink's target must be relative, may have `..` only at its start and no
  more of them than its folder is deep, so it cannot leave the package, whatever other symlinks exist; when the package
  turns out to be one top-level folder, a symlink must also stay inside that folder. A hard link is made with `linkat`
  to an entry already extracted. A device, a FIFO, a sparse file or any other type is refused. Each refusal names the
  entry and the reason, and a package that fails is never put in the cache.
- **What is kept:** file modes (owner, group and other bits, with the process's umask, as `tar` does for an ordinary
  user; no setuid or sticky bit) and file modification times (pax times to the nanosecond). Folders are always made
  owner-readable, -writable and -searchable, so that a package can be removed even if the archive says otherwise, and
  folder times are not restored. Owners are not kept. Pax and GNU long names, the ustar prefix field and non-ASCII names
  work; the other pax records (extended attributes, ACLs) are skipped.
- **Speed**, the best of three runs on an Apple M-series Mac, extracting into a fresh folder, smith (built as smith
  builds itself, `-O1`) against `/usr/bin/tar -xf` (bsdtar with zlib and liblzma): LLVM 23.1.3's source (180,598 files,
  2.3 GB of tar in 96 xz blocks) 24.3 s against 27.5 s; CMake 4.4.4 (260 MB, `.tar.gz`) 2.2 s against 1.3 s; Python
  3.13's standalone build (67 MB, `.tar.gz`) 0.38 s against 0.24 s. Decoding alone, to a sink that counts, runs at
  about 240 MB/s for xz and 270 MB/s for gzip, single-threaded; bsdtar's gzip is about 1.4 times faster per core (zlib
  is hand-tuned) and its xz the same. Files take most of the time, so the difference shows only on gzip.
  The timings vary by a factor of two with what else the machine is doing.
- **Not done:** `._name` AppleDouble companions that macOS tar writes (they begin with the AppleDouble magic number) are
  dropped, as bsdtar turns them into attributes and smith keeps none. Extended attributes, ACLs and file flags are not
  kept. There is no limit on how much an archive may write, as with `tar`: the SHA-256 in build.c is what vouches for it.
- **Tests:** `tests/untar_readers_test.smith` calls the readers in process (`smith_core_extract` and `smith_core_tree`,
  include/smith_core.h; no process starts, #646): every format, check and block layout above, each extracted and
  compared with the listing of its source tree (every path, byte, mode, time and link; the archives are the files of
  `tests/fixtures/untar` made with GNU tar, gzip and xz, and tars, gzips and xzs of stored data written by the test);
  hostile archives written by the test (traversal, absolute names, escaping and absolute symlinks, a file written
  through a symlink, a FIFO and a device, AppleDouble entries) and damaged ones (truncated, wrong gzip CRC-32 and size,
  a changed byte under each xz check, damaged xz headers and index, a damaged tar header), each refused with its reason.
  `tests/untar_formats_test.smith` and `tests/untar_refuse_test.smith` keep the wiring of `smith fetch` with the system's
  tar (a tar.gz extracts, a refusal names the archive and leaves the cache empty, a single .gz). The sanitizer tests:
  `tests/untar.c` under AddressSanitizer and UBSan, with known answers (including fixed-Huffman gzip), every shorter
  prefix of an archive refused, and thousands of randomly damaged archives that must not crash, hang, write outside
  the folder or leave a symlink that points out (for a plain tar the damage rewrites names, targets and types and then
  fixes the checksum, so the name checks are what stop it); and the same extractions with `tar`, `gzip` and `xz`
  forbidden to run, through `sandbox-exec`.
