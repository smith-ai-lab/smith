#!/bin/sh
# Tests of smith's own archive extraction (runtime/gzip.c, runtime/xz.c, runtime/untar.c), the part of `smith_package`
# that used to run tar. Archives are made with the system's tar, gzip and xz, and read back by `smith fetch` and, under
# AddressSanitizer and UndefinedBehaviorSanitizer, by tests/untar.c. Every case uses a cache of its own through
# XDG_CACHE_HOME, so the real ~/.cache/smith is never touched. Usage: sh tests/untar.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
here=$(cd "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
work=$(mktemp -d)
trap '[ -n "${KEEP_WORK:-}" ] || rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache
export COPYFILE_DISABLE=1 # macOS tar would add AppleDouble ._ entries for extended attributes

failures=0
total=0
out=
err=
rc=0
cases=0

capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
}

expect() { # name, expected, actual
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected: $2"
        echo "  actual:   $3"
    fi
}

expect_contains() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*) echo "ok    $1" ;;
    *)
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected to contain: $3"
        echo "  actual:              $2"
        ;;
    esac
}

# The files of a tree: type, permission bits and size, then the bytes by checksum. Equal listings are equal trees.
listing() {
    (cd "$1" && find . -type f -exec sh -c 'for f; do printf "%s %s %s " "$(stat -f "%Sp %z" "$f")" "$f" "$(shasum -a 256 <"$f" | cut -c1-16)"; echo; done' sh {} + | sort
        find . -type l -exec sh -c 'for f; do echo "link $f -> $(readlink "$f")"; done' sh {} + | sort
        find . -type d | sort)
}

# Fetches an archive into a cache of its own; the package folder is in $package, or empty when it failed.
fetch() { # archive
    cases=$((cases + 1))
    export XDG_CACHE_HOME=$work/cache-$cases
    capture "$smith" fetch "file://$1"
    package=
    if [ "$rc" -eq 0 ]; then
        hash=${out#*\"sha256:}
        hash=${hash%%\"*}
        package=$XDG_CACHE_HOME/smith/packages/$hash
    fi
}

# An archive that must extract to the same tree as the source folder.
same() { # name, archive, expected folder
    fetch "$2"
    expect "$1: extracts" 0 "$rc"
    expect "$1: the same tree, modes and bytes" "$(listing "$3")" "$(listing "${package:-/nonexistent}" 2>/dev/null)"
}

packages_in_cache() {
    ls "$XDG_CACHE_HOME/smith/packages" 2>/dev/null | grep -c '^[0-9a-f]\{64\}$'
}

# An archive that must be refused: a failing status, a message with the reason, and nothing left in the cache.
refuse() { # name, archive, needle
    fetch "$2"
    expect "$1: fails" yes "$([ "$rc" -ne 0 ] && echo yes || echo no)"
    expect_contains "$1: says why" "$err" "$3"
    expect_contains "$1: names the archive" "$err" "cannot extract file://$2"
    expect "$1: leaves no package" 0 "$(packages_in_cache)"
    expect "$1: and no temporary" 0 "$(ls -A "$XDG_CACHE_HOME/smith/packages" 2>/dev/null | grep -c '^\.tmp')"
}

# 1. The source tree: modes, an empty file, a big compressible file, a random file, relative symlinks.
src=$work/src
mkdir -p "$src/pkg-1.0/bin" "$src/pkg-1.0/lib/deep" "$src/pkg-1.0/empty-folder"
printf 'hello package\n' >"$src/pkg-1.0/hello.txt"
printf '#!/bin/sh\necho tool ran\n' >"$src/pkg-1.0/bin/tool"
chmod 755 "$src/pkg-1.0/bin/tool"
: >"$src/pkg-1.0/empty"
printf 'private\n' >"$src/pkg-1.0/lib/secret"
chmod 640 "$src/pkg-1.0/lib/secret"
printf 'read only\n' >"$src/pkg-1.0/lib/readonly"
chmod 444 "$src/pkg-1.0/lib/readonly"
awk 'BEGIN { for (i = 0; i < 2000; i++) printf "line %d of text that repeats and compresses well\n", i % 17 }' >"$src/pkg-1.0/lib/text.txt"
head -c 3000 /dev/urandom >"$src/pkg-1.0/lib/deep/random.bin"
ln -s ../hello.txt "$src/pkg-1.0/lib/hello-link"
ln -s lib "$src/pkg-1.0/libs"
ln -s ../../hello.txt "$src/pkg-1.0/lib/deep/up-two"
arc=$work/arc
mkdir "$arc"
(cd "$src" && tar --format ustar -cf "$arc/a.tar" pkg-1.0 && tar --format pax -cf "$arc/pax.tar" pkg-1.0 &&
    tar --format gnutar -cf "$arc/gnu.tar" pkg-1.0)
gzip -c "$arc/a.tar" >"$arc/a.tar.gz"
gzip -1 -c "$arc/a.tar" >"$arc/fast.tar.gz"
gzip -9 -c "$arc/a.tar" >"$arc/best.tar.gz"
cp "$arc/a.tar" "$arc/named.tar" && gzip -c "$arc/named.tar" >"$arc/named.tgz" # keeps the file name in its header
xz -c "$arc/a.tar" >"$arc/a.tar.xz"
xz --check=crc32 -c "$arc/a.tar" >"$arc/crc32.tar.xz"
xz --check=crc64 -c "$arc/a.tar" >"$arc/crc64.tar.xz"
xz --check=sha256 -c "$arc/a.tar" >"$arc/sha256.tar.xz"
xz --check=none -c "$arc/a.tar" >"$arc/none.tar.xz"
xz -T2 --block-size=8192 -c "$arc/a.tar" >"$arc/blocks.tar.xz"

same "tar (ustar)" "$arc/a.tar" "$src/pkg-1.0"
same "tar (pax)" "$arc/pax.tar" "$src/pkg-1.0"
same "tar (gnu)" "$arc/gnu.tar" "$src/pkg-1.0"
same "tar.gz" "$arc/a.tar.gz" "$src/pkg-1.0"
same "tar.gz (gzip -1)" "$arc/fast.tar.gz" "$src/pkg-1.0"
same "tar.gz (gzip -9)" "$arc/best.tar.gz" "$src/pkg-1.0"
same "tgz with the file name in its header" "$arc/named.tgz" "$src/pkg-1.0"
same "tar.xz (CRC-64)" "$arc/a.tar.xz" "$src/pkg-1.0"
same "tar.xz (CRC-32)" "$arc/crc32.tar.xz" "$src/pkg-1.0"
same "tar.xz (explicit CRC-64)" "$arc/crc64.tar.xz" "$src/pkg-1.0"
same "tar.xz (SHA-256)" "$arc/sha256.tar.xz" "$src/pkg-1.0"
same "tar.xz (no check)" "$arc/none.tar.xz" "$src/pkg-1.0"
same "tar.xz (many blocks)" "$arc/blocks.tar.xz" "$src/pkg-1.0"

# Members and streams one after the other: cat a.gz b.gz, and xz streams with the padding xz allows between them.
head -c 40960 "$arc/a.tar" >"$arc/first"
tail -c +40961 "$arc/a.tar" >"$arc/second"
gzip -c "$arc/first" >"$arc/two.tar.gz" && gzip -c "$arc/second" >>"$arc/two.tar.gz"
same "tar.gz of two members" "$arc/two.tar.gz" "$src/pkg-1.0"
xz -c "$arc/first" >"$arc/two.tar.xz" && printf '\0\0\0\0' >>"$arc/two.tar.xz" && xz -c "$arc/second" >>"$arc/two.tar.xz"
same "tar.xz of two streams with padding" "$arc/two.tar.xz" "$src/pkg-1.0"
cp "$arc/a.tar.gz" "$arc/padded.tar.gz" && printf '\0\0\0\0\0\0\0' >>"$arc/padded.tar.gz"
same "tar.gz with zero padding after it" "$arc/padded.tar.gz" "$src/pkg-1.0"
cp "$arc/a.tar.xz" "$arc/padded.tar.xz" && printf '\0\0\0\0\0\0\0\0' >>"$arc/padded.tar.xz"
same "tar.xz with zero padding after it" "$arc/padded.tar.xz" "$src/pkg-1.0"

# Data that does not compress is stored: gzip's stored blocks, and LZMA2's uncompressed chunks.
mkdir -p "$work/rand/pkg-1.0"
head -c 200000 /dev/urandom >"$work/rand/pkg-1.0/random.bin"
(cd "$work/rand" && tar -cf "$arc/rand.tar" pkg-1.0)
gzip -c "$arc/rand.tar" >"$arc/rand.tar.gz"
xz -c "$arc/rand.tar" >"$arc/rand.tar.xz"
same "tar.gz of random data (stored blocks)" "$arc/rand.tar.gz" "$work/rand/pkg-1.0"
same "tar.xz of random data (uncompressed chunks)" "$arc/rand.tar.xz" "$work/rand/pkg-1.0"

# File times are kept, to the second.
fetch "$arc/a.tar.gz"
expect "times: a file keeps its modification time" "$(stat -f %m "$src/pkg-1.0/hello.txt")" "$(stat -f %m "$package/hello.txt")"

# Several top-level entries: the package is the extraction folder, as it was with tar.
mkdir -p "$work/multi/one" "$work/multi/two"
echo 1 >"$work/multi/one/a" && echo 2 >"$work/multi/two/b"
(cd "$work/multi" && tar -czf "$arc/multi.tar.gz" one two)
fetch "$arc/multi.tar.gz"
expect "several top-level folders: the first is kept under its name" "1" "$(cat "$package/one/a" 2>/dev/null)"
expect "several top-level folders: so is the second" "2" "$(cat "$package/two/b" 2>/dev/null)"

# 2. Long names: a ustar name split over its prefix and name fields, a pax path, a GNU long name, and non-ASCII names.
long=$work/long
d=pkg-1.0/$(printf 'a%.0s' $(seq 80))/$(printf 'b%.0s' $(seq 80))/$(printf 'c%.0s' $(seq 80))
mkdir -p "$long/$d" "$long/pkg-1.0/ü"
echo deep >"$long/$d/file.txt"
echo accent >"$long/pkg-1.0/ü/é.txt"
(cd "$long" && tar --format pax -cf "$arc/long-pax.tar" pkg-1.0 && tar --format gnutar -cf "$arc/long-gnu.tar" pkg-1.0)
same "long names (pax)" "$arc/long-pax.tar" "$long/pkg-1.0"
same "long names (gnu)" "$arc/long-gnu.tar" "$long/pkg-1.0"
gzip -c "$arc/long-pax.tar" >"$arc/long-pax.tar.gz"
xz -c "$arc/long-gnu.tar" >"$arc/long-gnu.tar.xz"
same "long names (pax, gzip)" "$arc/long-pax.tar.gz" "$long/pkg-1.0"
same "long names (gnu, xz)" "$arc/long-gnu.tar.xz" "$long/pkg-1.0"
p90=$(printf 'p%.0s' $(seq 90))
q90=$(printf 'q%.0s' $(seq 90))
mkdir -p "$work/prefix/top/$p90/$q90"
echo prefixed >"$work/prefix/top/$p90/$q90/file"
(cd "$work/prefix" && tar --format ustar -cf "$arc/prefix.tar" top)
fetch "$arc/prefix.tar"
expect "long names (ustar prefix field)" prefixed "$(cat "$package/$p90/$q90/file" 2>/dev/null)"

# 3. Hostile archives, made with tar's -P and -s so that it writes the names it would normally refuse. Each is refused
# with the reason, leaves nothing in the cache, and writes nothing outside the extraction folder.
h=$work/hostile
mkdir -p "$h/pkg-1.0" "$work/target"
echo hi >"$h/pkg-1.0/a.txt"
ln -s ../../outside "$h/pkg-1.0/escape"
ln -s /etc/passwd "$h/pkg-1.0/absolute"
ln -s . "$h/pkg-1.0/self"
ln -s a/../b "$h/pkg-1.0/dotdot"
ln -s ../../../../../x "$h/pkg-1.0/far"
mkfifo "$h/pkg-1.0/fifo"
ln -s ../../../../.. "$h/pkg-1.0/to-root"
abs=$work/target/abs-evil
(cd "$h" &&
    tar -cf "$arc/up.tar" -P -s ',^pkg-1.0/a.txt$,../evil,' pkg-1.0/a.txt &&
    tar -cf "$arc/up-deep.tar" -P -s ',^pkg-1.0/a.txt$,pkg-1.0/x/../../../evil,' pkg-1.0/a.txt &&
    tar -cf "$arc/absolute.tar" -P -s ",^pkg-1.0/a.txt\$,$abs," pkg-1.0/a.txt &&
    tar -cf "$arc/dotdot-only.tar" -P -s ',^pkg-1.0/a.txt$,..,' pkg-1.0/a.txt &&
    tar -cf "$arc/escape.tar" -P pkg-1.0/escape &&
    tar -cf "$arc/absolute-link.tar" -P pkg-1.0/absolute &&
    tar -cf "$arc/dotdot-link.tar" -P pkg-1.0/dotdot &&
    tar -cf "$arc/far-link.tar" -P pkg-1.0/far &&
    tar -cf "$arc/through.tar" -P -s ',^pkg-1.0/a.txt$,pkg-1.0/self/under,' pkg-1.0/self pkg-1.0/a.txt)
# tar warns that it cannot pack the extended attributes of these two, and still writes them.
(cd "$h" && tar -cf "$arc/fifo.tar" -P pkg-1.0/fifo 2>/dev/null; tar -cf "$arc/device.tar" -P -s ',^/dev/null$,pkg-1.0/null,' /dev/null 2>/dev/null)
refuse "traversal" "$arc/up.tar" "goes up out of the package"
refuse "traversal inside a path" "$arc/up-deep.tar" "goes up out of the package"
refuse "a name that is .." "$arc/dotdot-only.tar" "goes up out of the package"
refuse "an absolute path" "$arc/absolute.tar" "absolute path"
refuse "a symlink out of the package" "$arc/escape.tar" "points outside the package"
refuse "an absolute symlink" "$arc/absolute-link.tar" "absolute path"
refuse "a symlink that goes up after going down" "$arc/dotdot-link.tar" "goes up after going down"
refuse "a symlink far above" "$arc/far-link.tar" "points outside the package"
refuse "a file written through a symlink" "$arc/through.tar" "through a symlink"
refuse "a FIFO" "$arc/fifo.tar" "special file"
refuse "a device" "$arc/device.tar" "special file"
expect "hostile: nothing was written beside the cache's folders" 0 "$(find "$work" -name 'evil*' -o -name 'abs-evil' | wc -l | tr -d ' ')"

# A symlink that stays in its top-level folder is fine, and one that reaches the folder above it is refused only when
# the package is that one folder, since it then leaves the package.
mkdir -p "$work/top/pkg-1.0/a/b" "$work/top/other"
echo data >"$work/top/pkg-1.0/a/data"
ln -s ../data "$work/top/pkg-1.0/a/b/inside"
ln -s ../../../other "$work/top/pkg-1.0/a/b/sibling"
(cd "$work/top" && tar -n -cf "$arc/inside.tar" pkg-1.0/a pkg-1.0/a/b pkg-1.0/a/data pkg-1.0/a/b/inside &&
    tar -n -cf "$arc/sibling-one.tar" pkg-1.0/a pkg-1.0/a/b pkg-1.0/a/data pkg-1.0/a/b/sibling &&
    tar -n -cf "$arc/sibling-two.tar" pkg-1.0/a pkg-1.0/a/b pkg-1.0/a/b/sibling other)
fetch "$arc/inside.tar"
expect "symlinks: one inside the package is kept" "../data" "$(readlink "$package/a/b/inside" 2>/dev/null)"
refuse "symlinks: one that leaves the single top-level folder" "$arc/sibling-one.tar" "points outside the package folder"
fetch "$arc/sibling-two.tar"
expect "symlinks: the same link beside a second top-level entry is kept" "../../../other" "$(readlink "$package/pkg-1.0/a/b/sibling" 2>/dev/null)"

# Hard links to an earlier entry.
mkdir -p "$work/hard/pkg-1.0"
echo shared >"$work/hard/pkg-1.0/one"
ln "$work/hard/pkg-1.0/one" "$work/hard/pkg-1.0/two"
(cd "$work/hard" && tar -cf "$arc/hard.tar" pkg-1.0)
fetch "$arc/hard.tar"
expect "hard links: both names have the bytes" "shared shared" "$(cat "$package/one" "$package/two" 2>/dev/null | tr '\n' ' ' | sed 's/ $//')"

# macOS tar writes extended attributes as "._name" entries in the AppleDouble format and bsdtar turns them back into
# attributes, so they never become files, and the package is still one folder. smith drops them too, by the format's
# magic number; a "._" file of anything else is a file like any other.
mkdir -p "$work/apple/pkg-1.0" "$work/apple-plain/pkg-1.0"
echo first >"$work/apple/pkg-1.0/a"
echo second >"$work/apple/pkg-1.0/b"
printf 'a plain file that only has a name like metadata\n' >"$work/apple-plain/pkg-1.0/dot_plain"
printf 'tiny\n' >"$work/apple-plain/pkg-1.0/dot_tiny"
xattr -w com.example.test value "$work/apple/pkg-1.0/a" 2>/dev/null
xattr -w com.example.test value "$work/apple/pkg-1.0/b" 2>/dev/null
(cd "$work/apple" && env -u COPYFILE_DISABLE tar -cf "$arc/apple.tar" pkg-1.0)
# Two files whose names start with "._" and that are not AppleDouble, appended under those names.
(cd "$work/apple-plain" && tar -rf "$arc/apple.tar" -s ',dot_,._,' pkg-1.0/dot_plain pkg-1.0/dot_tiny)
mkdir -p "$work/apple-expect"
cp -R "$work/apple/pkg-1.0" "$work/apple-expect/" && xattr -cr "$work/apple-expect"
cp "$work/apple-plain/pkg-1.0/dot_plain" "$work/apple-expect/pkg-1.0/._plain"
cp "$work/apple-plain/pkg-1.0/dot_tiny" "$work/apple-expect/pkg-1.0/._tiny"
# (tar -t hides these entries, since it reads them as the attributes of the next one, so look at the bytes.)
if [ "$(grep -a -c '\._a' "$arc/apple.tar")" -gt 0 ]; then
    fetch "$arc/apple.tar"
    expect "AppleDouble entries become no files, and the package is still one folder" "$(listing "$work/apple-expect/pkg-1.0")" "$(listing "${package:-/nonexistent}" 2>/dev/null)"
    gzip -c "$arc/apple.tar" >"$arc/apple.tar.gz"
    same "AppleDouble entries in a tar.gz" "$arc/apple.tar.gz" "$work/apple-expect/pkg-1.0"
else
    echo "skip  AppleDouble entries: this tar writes none"
fi

# 4. Damaged archives.
refuse "not an archive" "$work/src/pkg-1.0/hello.txt" "not a tar, tar.gz or tar.xz archive"
: >"$arc/empty"
refuse "an empty file" "$arc/empty" "the archive is empty"
(cd "$work/src" && zip -qr "$arc/pkg.zip" pkg-1.0)
refuse "a zip" "$arc/pkg.zip" "zip archives are not supported"
bzip2 -c "$arc/a.tar" >"$arc/a.tar.bz2"
refuse "a bzip2" "$arc/a.tar.bz2" "bzip2 archives are not supported"
size=$(wc -c <"$arc/a.tar.gz" | tr -d ' ')
head -c $((size / 2)) "$arc/a.tar.gz" >"$arc/cut.tar.gz"
refuse "a truncated gzip" "$arc/cut.tar.gz" "gzip:"
head -c $((size - 4)) "$arc/a.tar.gz" >"$arc/cut-trailer.tar.gz"
refuse "a gzip without its trailer" "$arc/cut-trailer.tar.gz" "trailer is cut off"
size=$(wc -c <"$arc/a.tar.xz" | tr -d ' ')
head -c $((size / 2)) "$arc/a.tar.xz" >"$arc/cut.tar.xz"
refuse "a truncated xz" "$arc/cut.tar.xz" "xz:"
head -c $((size - 6)) "$arc/a.tar.xz" >"$arc/cut-footer.tar.xz"
refuse "an xz without its footer" "$arc/cut-footer.tar.xz" "footer"
size=$(wc -c <"$arc/a.tar" | tr -d ' ')
head -c 3000 "$arc/a.tar" >"$arc/cut.tar"
refuse "a truncated tar" "$arc/cut.tar" "cut off"

# A byte changed at a place that only a check can notice: gzip's CRC-32 and size, and xz's check of each kind. The
# random archive stores its data, so a changed byte in the middle is still well-formed LZMA2 or DEFLATE.
flip() { # file, offset (negative counts from the end), output
    size=$(wc -c <"$1" | tr -d ' ')
    at=$2
    [ "$at" -lt 0 ] && at=$((size + at))
    cp "$1" "$3"
    byte=$(dd if="$1" bs=1 skip="$at" count=1 2>/dev/null | od -An -tu1 | tr -d ' ')
    printf "$(printf '\\%03o' $(((byte + 1) % 256)))" | dd of="$3" bs=1 seek="$at" conv=notrunc 2>/dev/null
}
flip "$arc/a.tar.gz" -8 "$arc/bad-crc.tar.gz"
refuse "a gzip with a wrong CRC-32" "$arc/bad-crc.tar.gz" "CRC-32 of the data does not match"
flip "$arc/a.tar.gz" -4 "$arc/bad-size.tar.gz"
refuse "a gzip with a wrong size" "$arc/bad-size.tar.gz" "size of the data does not match"
flip "$arc/rand.tar.gz" 100000 "$arc/bad-stored.tar.gz"
refuse "a gzip with a changed byte in a stored block" "$arc/bad-stored.tar.gz" "CRC-32 of the data does not match"
for check in crc32 crc64 sha256; do
    xz --check=$check -c "$arc/rand.tar" >"$arc/rand-$check.tar.xz"
    flip "$arc/rand-$check.tar.xz" 100000 "$arc/bad-$check.tar.xz"
    refuse "an xz with a changed byte under $check" "$arc/bad-$check.tar.xz" "check does not match its data"
done
flip "$arc/a.tar.xz" 7 "$arc/bad-flags.tar.xz"
refuse "an xz with a damaged stream header" "$arc/bad-flags.tar.xz" "stream header"
flip "$arc/a.tar.xz" 13 "$arc/bad-block.tar.xz"
refuse "an xz with a damaged block header" "$arc/bad-block.tar.xz" "block header"
flip "$arc/a.tar.xz" -14 "$arc/bad-index.tar.xz"
refuse "an xz with a damaged index" "$arc/bad-index.tar.xz" "index"
flip "$arc/a.tar" 522 "$arc/bad-header.tar" # in the second header, since the first decides that this is a tar
refuse "a tar with a damaged header" "$arc/bad-header.tar" "checksum does not match"

# 5. The readers under AddressSanitizer and UndefinedBehaviorSanitizer: extraction, every shorter prefix of an archive
# refused, and random damage that may not crash, hang, write outside the folder or leave a symlink that points out.
flags="-std=gnu23 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Werror -Wno-unused-function"
if ! $cc $flags "$here/tests/untar.c" -o "$work/untar" 2>"$work/compile.log"; then
    cat "$work/compile.log"
    echo "FAIL  tests/untar.c does not compile"
    exit 1
fi
# Smaller seeds, so that every prefix is quick: a few files and a short random one.
mkdir -p "$work/seed/pkg-1.0/lib"
printf 'hello\n' >"$work/seed/pkg-1.0/hello.txt"
awk 'BEGIN { for (i = 0; i < 400; i++) printf "line %d of text that repeats\n", i % 7 }' >"$work/seed/pkg-1.0/lib/text.txt"
head -c 1500 /dev/urandom >"$work/seed/pkg-1.0/lib/random.bin"
ln -s ../hello.txt "$work/seed/pkg-1.0/lib/link"
mkdir -p "$work/seeds"
(cd "$work/seed" && tar --format pax -cf "$work/seeds/seed.tar" pkg-1.0)
gzip -c "$work/seeds/seed.tar" >"$work/seeds/seed.tar.gz"
xz --check=crc64 -c "$work/seeds/seed.tar" >"$work/seeds/seed.tar.xz"
xz --check=sha256 -c "$work/seeds/seed.tar" >"$work/seeds/sha256.tar.xz"
mkdir "$work/scratch"
mkdir "$work/untar-out"
capture "$work/untar" vectors
expect "sanitizers: known gzip (fixed Huffman codes) and xz streams, and each of their prefixes refused" "0 vectors ok" "$rc $out"
for seed in seed.tar seed.tar.gz seed.tar.xz sha256.tar.xz; do
    capture "$work/untar" extract "$work/seeds/$seed" "$work/untar-out"
    expect "sanitizers: $seed extracts" "0 ok" "$rc $out"
    rm -rf "$work/untar-out" && mkdir "$work/untar-out"
    capture "$work/untar" prefixes "$work/seeds/$seed" "$work/scratch"
    expect_contains "sanitizers: every shorter prefix of $seed is refused" "$rc $out $err" "0 "
    expect_contains "sanitizers: ... and the count says so" "$out" "prefixes refused"
    capture "$work/untar" fuzz "$work/seeds/$seed" "$work/scratch" 1500 1
    expect_contains "sanitizers: 1500 damaged copies of $seed do no harm" "$rc $out $err" "0 1500 damaged archives"
done
if [ -f "$arc/apple.tar.gz" ]; then
    capture "$work/untar" prefixes "$arc/apple.tar.gz" "$work/scratch"
    expect_contains "sanitizers: every shorter prefix of the AppleDouble tar.gz is refused" "$rc $out" "0 "
    capture "$work/untar" prefixes "$arc/apple.tar" "$work/scratch"
    expect_contains "sanitizers: every shorter prefix of the AppleDouble tar is refused" "$rc $out" "0 "
    capture "$work/untar" fuzz "$arc/apple.tar" "$work/scratch" 800 6
    expect_contains "sanitizers: damaged AppleDouble entries do no harm" "$rc $out $err" "0 800 damaged archives"
fi
capture "$work/untar" fuzz "$arc/pax.tar" "$work/scratch" 800 2
expect_contains "sanitizers: damaged headers of the pax tar do no harm" "$rc $out $err" "0 800 damaged archives"
capture "$work/untar" fuzz "$arc/long-gnu.tar" "$work/scratch" 800 3
expect_contains "sanitizers: damaged headers of the GNU tar do no harm" "$rc $out $err" "0 800 damaged archives"
capture "$work/untar" fuzz "$arc/blocks.tar.xz" "$work/scratch" 800 4
expect_contains "sanitizers: damaged xz blocks do no harm" "$rc $out $err" "0 800 damaged archives"
capture "$work/untar" fuzz "$arc/rand.tar.gz" "$work/scratch" 300 5
expect_contains "sanitizers: damaged stored blocks do no harm" "$rc $out $err" "0 300 damaged archives"

# 6. No process runs tar: nothing in smith names it, and with tar forbidden to run the same extractions work.
expect "no tar: nothing in smith names it" 0 "$(cat "$here/smith.c" "$here"/runtime/*.c | grep -c 'bin/tar\|"tar"')"
if command -v sandbox-exec >/dev/null 2>&1; then
    profile='(version 1)(allow default)(deny process-exec (literal "/usr/bin/tar") (literal "/usr/bin/bsdtar") (literal "/usr/bin/gzip") (literal "/usr/bin/gunzip") (literal "/opt/homebrew/bin/xz") (literal "/usr/bin/xz"))'
    export XDG_CACHE_HOME=$work/cache-sandbox
    capture sandbox-exec -p "$profile" /usr/bin/tar -tf "$arc/a.tar"
    forbidden=$rc
    capture sandbox-exec -p "$profile" /usr/bin/true
    if [ "$rc" -eq 0 ]; then
        expect "no tar: the sandbox does forbid tar" yes "$([ "$forbidden" -ne 0 ] && echo yes || echo no)"
        capture sandbox-exec -p "$profile" "$smith" fetch "file://$arc/a.tar.xz"
        expect "no tar: a .tar.xz extracts with tar, gzip and xz forbidden to run" 0 "$rc"
        capture sandbox-exec -p "$profile" "$smith" fetch "file://$arc/a.tar.gz"
        expect "no tar: a .tar.gz extracts with tar, gzip and xz forbidden to run" 0 "$rc"
    fi
fi

echo
echo "$total archive checks, $failures failed"
[ "$failures" -eq 0 ]
