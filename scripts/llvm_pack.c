// The LLVM pack (issue #797, knowledge/llvm-build.md): the part of an LLVM build that smith links and embeds, published
// as a GitHub Release `llvm-<name>-<os>-<arch>` (for example `llvm-23.1.3-r13-darwin-arm64`), so that a machine with
// no LLVM (a hosted CI runner, a fresh clone) downloads it in minutes instead of building it for hours. Plain C23 and
// libc only (as scripts/llvm_prune.c and scripts/llvm_linux.c are: Linux has no smith before this runs); it runs tar,
// curl (a fetch is plain HTTPS from the public release URL: no gh, no login, no GraphQL, #829) and gh (publish).
//
//   llvm_pack tag NAME               print the release's tag
//   llvm_pack exists NAME            exit 0 when the release exists (its URL answers), else 1
//   llvm_pack pack CACHE NAME OUT    write the pack into OUT: <tag>.tar.xz.000, .001, ... and <tag>.sha256
//   llvm_pack publish CACHE NAME     pack, then `gh release create`; nothing when the release exists
//   llvm_pack fetch CACHE NAME STAMP download, check and unpack into CACHE; write STAMP; exit 3 when there is no
//                                    pack to take (the release is not published (HTTP 404), or a build already
//                                    in that folder); any other failure is an error naming the URL
// CACHE is the package cache's smith folder (~/.cache/smith) and NAME the configuration (23.1.3-r13), the folder
// CACHE/llvm/NAME that `smith build llvm` makes. The pack holds only what smith links and what a build of smith reads:
//   llvm/NAME/install/lib/*.a, smith-clang.id, libLTO.dylib, clang/ (the resource directory), cmake/
//   llvm/NAME/install/include/ and bin/clang (bin/clang-23, the links bin/clang and bin/clang++)
//   llvm/NAME/prove/{lib,include}              Z3, Alive2 and Bitwuzla's libraries (macOS)
//   enzyme/<version>-NAME/lib                  Enzyme's library (macOS)
// and not the build folder, the sources or the other 2.4 GB of programs. It is compressed with xz and cut into parts
// of at most LLVM_PACK_PART_BYTES (default 1.9 GB, under GitHub's 2 GiB per asset), with the SHA-256 of each part in
// <tag>.sha256 (the lines `shasum -a 256 -c` reads). A fetch checks each part before it unpacks, unpacks beside the
// final folders and moves them in with one rename each, so an interrupted fetch leaves nothing that looks built.
#if defined(__linux__)
#define _GNU_SOURCE // glibc declares nftw, mkdtemp and setenv only for X/Open or GNU code, not plain -std=c23
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <glob.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sha256.h"

extern char **environ;

constexpr size_t path_max = 4096;
constexpr size_t parts_max = 64;  // 64 parts of 1.9 GB: far more than an LLVM pack ever is
constexpr size_t chunk = 1 << 20; // the copy buffer
constexpr unsigned long long part_default = 1900000000ULL;
constexpr int exit_unavailable = 3; // fetch: nothing to take, build instead
constexpr int walk_files = 64;

static char scratch_folder[path_max]; // a folder of this run's own, removed when it ends, whether it worked or not

[[noreturn]] static void die(const char *message, const char *detail) {
    fprintf(stderr, "llvm_pack: %s%s%s\n", message, detail != nullptr ? " " : "", detail != nullptr ? detail : "");
    exit(1);
}

// Formats into a buffer of path_max bytes; a path that does not fit stops the program.
static void join(char *out, const char *format, const char *a, const char *b) {
    int written = snprintf(out, path_max, format, a, b);
    if (written < 0 || (size_t)written >= path_max) die("a path is too long:", a);
}

// Runs a program found on PATH and waits; its exit status, or -1 when it did not start or did not exit. A quiet child's
// output goes nowhere.
static int run(const char *const *argv, bool quiet) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (quiet) {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    }
    pid_t child = 0;
    int started = posix_spawnp(&child, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (started != 0) return -1;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) { // only a signal interrupts the wait
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void run_or_die(const char *const *argv) {
    if (run(argv, false) != 0) die("failed:", argv[0]);
}

static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *where) {
    (void)info;
    (void)type;
    (void)where;
    return remove(path);
}

static void remove_tree(const char *path) {
    if (nftw(path, remove_entry, walk_files, FTW_DEPTH | FTW_PHYS) != 0) remove(path);
}

static void remove_scratch(void) {
    if (scratch_folder[0] != '\0') remove_tree(scratch_folder);
}

// Makes a folder from a template ending in XXXXXX, and removes it when the program ends.
static void make_scratch(char *folder) {
    if (mkdtemp(folder) == nullptr) die("cannot make a folder:", folder);
    snprintf(scratch_folder, sizeof scratch_folder, "%s", folder);
    atexit(remove_scratch);
}

static bool exists(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0;
}

// The release's tag: llvm-<name>-<os>-<arch>, with the machine as `uname` names it (darwin-arm64, linux-x86_64).
static void tag_of(const char *name, char tag[path_max]) {
    struct utsname system;
    if (uname(&system) != 0) die("cannot tell the machine", nullptr);
    char os[64];
    size_t length = strnlen(system.sysname, sizeof os - 1);
    for (size_t i = 0; i < length; i++)
        os[i] = (char)(system.sysname[i] >= 'A' && system.sysname[i] <= 'Z' ? system.sysname[i] + ('a' - 'A')
                                                                            : system.sysname[i]);
    os[length] = '\0';
    int written = snprintf(tag, path_max, "llvm-%s-%s-%s", name, os, system.machine);
    if (written < 0 || (size_t)written >= path_max) die("a name is too long:", name);
}

// A name from the command line or from a manifest is one path element: no folder, nothing hidden.
static void check_element(const char *name) {
    if (name[0] == '\0' || name[0] == '.' || strchr(name, '/') != nullptr) die("not a plain name:", name);
}

// The public URL of a file of a release.
static void asset_url(char *url, const char *tag, const char *name) {
    int written = snprintf(url, path_max, "https://github.com/smith-ai-lab/smith/releases/download/%s/%s", tag, name);
    if (written < 0 || (size_t)written >= path_max) die("a URL is too long:", name);
}

static void download(const char *url, const char *path) {
    if (run((const char *const[]){"curl", "-fsSL", "--retry", "3", "-o", path, url, nullptr}, false) != 0)
        die("cannot download", url);
}

// The HTTP status of one byte of the URL (a signed storage URL refuses a HEAD request), read from a file curl writes.
static int http_status(const char *url, const char *scratch_file) {
    char format[path_max + 32];
    snprintf(format, sizeof format, "%%output{%s}%%{http_code}", scratch_file);
    run((const char *const[]){"curl", "-sSL", "--retry", "3", "-r", "0-0", "-o", "/dev/null", "-w", format, url,
                              nullptr},
        false);
    FILE *file = fopen(scratch_file, "r");
    char text[16] = "";
    if (file != nullptr && fgets(text, sizeof text, file) == nullptr) text[0] = '\0';
    if (file != nullptr) fclose(file);
    int status = (int)strtol(text, nullptr, 10);
    remove(scratch_file);
    return status;
}

// Whether the release is published: its checksum file answers 200 or 206. 404 is "no"; a network that does not answer
// or any other status is an error naming the URL, since nothing says there is no pack.
static bool release_exists(const char *tag) {
    char url[path_max];
    char status_file[path_max];
    char name[path_max];
    snprintf(name, sizeof name, "%s.sha256", tag);
    asset_url(url, tag, name);
    const char *temporary = getenv("TMPDIR") != nullptr ? getenv("TMPDIR") : "/tmp";
    snprintf(status_file, sizeof status_file, "%s/llvm_pack-status-%d", temporary, (int)getpid());
    int status = http_status(url, status_file);
    if (status == 200 || status == 206) return true;
    if (status == 404) return false;
    char message[64];
    snprintf(message, sizeof message, "cannot fetch the pack (HTTP status %d):", status);
    die(message, url);
}

static void make_folder(const char *path) {
    if (mkdir(path, 0755) != 0 && !exists(path)) die("cannot make the folder", path);
}

static void make_folders(const char *path) {
    run_or_die((const char *const[]){"mkdir", "-p", path, nullptr});
}

// ---- pack ---------------------------------------------------------------------------------------------------------

// What the pack holds, as globs under the cache (llvm_pack's header says why each); a pattern that matches nothing is
// a file this platform does not have (libLTO.dylib on Linux, the prover's libraries, Enzyme).
static const char *const wanted[] = {
    "llvm/%s/install/lib/*.a",
    "llvm/%s/install/lib/smith-clang.id",
    "llvm/%s/install/lib/libLTO.dylib",
    "llvm/%s/install/lib/clang",
    "llvm/%s/install/lib/cmake",
    "llvm/%s/install/include",
    "llvm/%s/install/bin/clang",
    "llvm/%s/install/bin/clang++",
    "llvm/%s/install/bin/clang-[0-9]*",
    "llvm/%s/prove/lib",
    "llvm/%s/prove/include",
    "enzyme/[0-9]*-%s/lib",
};

// Writes the paths (relative to the cache) of everything the pack holds into a file for `tar -T`, and says how many
// there are.
static size_t write_list(const char *cache, const char *name, const char *list) {
    FILE *file = fopen(list, "w");
    if (file == nullptr) die("cannot write", list);
    size_t count = 0;
    size_t prefix = strlen(cache) + 1;
    for (size_t i = 0; i < sizeof wanted / sizeof wanted[0]; i++) {
        char relative[path_max];
        char pattern[path_max];
        join(relative, wanted[i], name, "");
        join(pattern, "%s/%s", cache, relative);
        glob_t found = {};
        if (glob(pattern, 0, nullptr, &found) == 0) {
            for (size_t j = 0; j < found.gl_pathc; j++) {
                fprintf(file, "%s\n", found.gl_pathv[j] + prefix);
                count++;
            }
        }
        globfree(&found);
    }
    fclose(file);
    return count;
}

// Reads the part size limit: LLVM_PACK_PART_BYTES, for a test that wants several parts of a small pack.
static unsigned long long part_bytes(void) {
    const char *text = getenv("LLVM_PACK_PART_BYTES");
    unsigned long long size = text != nullptr ? strtoull(text, nullptr, 10) : 0;
    return size > 0 ? size : part_default;
}

// Cuts the archive into parts of at most part_bytes() each, and writes the manifest: one `<sha256>  <part>` line per
// part.
static void split(const char *archive, const char *out, const char *tag) {
    FILE *input = fopen(archive, "rb");
    if (input == nullptr) die("cannot read", archive);
    char manifest_path[path_max];
    join(manifest_path, "%s/%s.sha256", out, tag);
    FILE *manifest = fopen(manifest_path, "w");
    if (manifest == nullptr) die("cannot write", manifest_path);
    static unsigned char buffer[chunk];
    unsigned long long limit = part_bytes();
    bool more = true;
    for (size_t part = 0; more; part++) { // each pass writes one part, until the archive ends
        if (part >= parts_max) die("the pack needs more parts than", "64: raise LLVM_PACK_PART_BYTES");
        char name[path_max];
        char path[path_max];
        snprintf(name, sizeof name, "%s.tar.xz.%03zu", tag, part);
        join(path, "%s/%s", out, name);
        FILE *output = fopen(path, "wb");
        if (output == nullptr) die("cannot write", path);
        Sha256 sha;
        sha256_init(&sha);
        unsigned long long written = 0;
        while (written < limit) { // each pass copies up to one buffer of this part
            size_t want = limit - written < sizeof buffer ? (size_t)(limit - written) : sizeof buffer;
            size_t got = fread(buffer, 1, want, input);
            if (got == 0) break;
            if (fwrite(buffer, 1, got, output) != got) die("cannot write", path);
            sha256_update(&sha, buffer, got);
            written += got;
        }
        more = written == limit && !feof(input) && fgetc(input) != EOF;
        if (more) fseek(input, -1, SEEK_CUR);
        if (fclose(output) != 0) die("cannot write", path);
        char hex[65];
        sha256_final(&sha, hex);
        fprintf(manifest, "%s  %s\n", hex, name);
    }
    fclose(manifest);
    fclose(input);
}

static void pack(const char *cache, const char *name, const char *out) {
    check_element(name);
    char tag[path_max];
    tag_of(name, tag);
    make_folders(out);
    char list[path_max];
    char archive[path_max];
    join(list, "%s/%s.list", out, tag);
    join(archive, "%s/%s.tar.xz", out, tag);
    char id[path_max];
    join(id, "%s/llvm/%s/install/lib/smith-clang.id", cache, name);
    if (!exists(id)) die("no LLVM build to pack, no smith-clang.id at", id);
    size_t count = write_list(cache, name, list);
    printf("llvm_pack: %zu entries of %s/llvm/%s\n", count, cache, name);
    setenv("COPYFILE_DISABLE", "1", 1); // macOS's tar: no ._ files for extended attributes
#if defined(__APPLE__)
    run_or_die((const char *const[]){"tar", "--options", "xz:compression-level=6", "-C", cache, "-cJf", archive, "-T",
                                     list, nullptr});
#else
    setenv("XZ_OPT", "-6 -T0", 1);
    run_or_die((const char *const[]){"tar", "-C", cache, "-cJf", archive, "-T", list, nullptr});
#endif
    split(archive, out, tag);
    remove(archive);
    remove(list);
}

// The identity inside the pack, for the release's notes.
static void read_identity(const char *cache, const char *name, char id[path_max]) {
    char path[path_max];
    join(path, "%s/llvm/%s/install/lib/smith-clang.id", cache, name);
    FILE *file = fopen(path, "r");
    id[0] = '\0';
    if (file != nullptr && fgets(id, (int)path_max, file) != nullptr) id[strcspn(id, "\n")] = '\0';
    if (file != nullptr) fclose(file);
}

static void publish(const char *cache, const char *name) {
    char tag[path_max];
    check_element(name);
    tag_of(name, tag);
    if (release_exists(tag)) {
        printf("llvm_pack: %s exists\n", tag);
        return;
    }
    char folder[path_max];
    join(folder, "%s/llvm/.pack-XXXXXX", cache, "");
    make_scratch(folder);
    pack(cache, name, folder);
    char pattern[path_max];
    join(pattern, "%s/*", folder, "");
    glob_t files = {};
    if (glob(pattern, 0, nullptr, &files) != 0 || files.gl_pathc > parts_max + 1) die("no pack in", folder);
    char id[path_max];
    read_identity(cache, name, id);
    char notes[path_max];
    snprintf(notes, sizeof notes, "The LLVM that smith links, %s: the pack of knowledge/llvm-build.md.", id);
    const char *argv[parts_max + 16] = {"gh", "release", "create", tag};
    size_t count = 4;
    for (size_t i = 0; i < files.gl_pathc; i++)
        argv[count++] = files.gl_pathv[i];
    argv[count++] = "--title";
    argv[count++] = tag;
    argv[count++] = "--notes";
    argv[count++] = notes;
    const char *sha = getenv("GITHUB_SHA");
    if (sha != nullptr && sha[0] != '\0') {
        argv[count++] = "--target";
        argv[count++] = sha;
    }
    argv[count] = nullptr;
    int status = run(argv, false);
    globfree(&files);
    if (status != 0) die("gh release create failed for", tag);
    printf("llvm_pack: published %s\n", tag);
}

// ---- fetch --------------------------------------------------------------------------------------------------------

typedef struct {
    char hex[parts_max][65];
    char name[parts_max][256];
    size_t count;
} Manifest;

static void read_manifest(const char *path, Manifest *manifest) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) die("cannot read", path);
    manifest->count = 0;
    char line[512];
    while (fgets(line, sizeof line, file) != nullptr) { // one part a line
        if (manifest->count == parts_max) die("too many parts in", path);
        size_t at = manifest->count;
        if (sscanf(line, "%64s %255s", manifest->hex[at], manifest->name[at]) != 2 || strlen(manifest->hex[at]) != 64)
            die("a line of the manifest is not a checksum and a name:", line);
        check_element(manifest->name[at]);
        manifest->count++;
    }
    fclose(file);
    if (manifest->count == 0) die("the manifest is empty:", path);
}

// Appends a file to the one being written.
static void append(FILE *output, const char *path) {
    FILE *input = fopen(path, "rb");
    if (input == nullptr) die("cannot read", path);
    static unsigned char buffer[chunk];
    size_t got = 1;
    while (got > 0) { // each pass copies one buffer
        got = fread(buffer, 1, sizeof buffer, input);
        if (got > 0 && fwrite(buffer, 1, got, output) != got) die("cannot write while copying", path);
    }
    fclose(input);
}

// Downloads every part named in the manifest into the stage, checks each SHA-256, and joins them into one archive.
static void download_parts(const char *tag, const char *stage, const Manifest *manifest, const char *archive) {
    FILE *output = fopen(archive, "wb");
    if (output == nullptr) die("cannot write", archive);
    for (size_t i = 0; i < manifest->count; i++) {
        char path[path_max];
        char hex[65];
        join(path, "%s/%s", stage, manifest->name[i]);
        char url[path_max];
        asset_url(url, tag, manifest->name[i]);
        download(url, path);
        if (!sha256_file(path, hex) || strcmp(hex, manifest->hex[i]) != 0)
            die("the checksum of a part does not match:", manifest->name[i]);
        append(output, path);
        remove(path);
    }
    if (fclose(output) != 0) die("cannot write", archive);
}

// Moves what was unpacked into the cache: llvm/NAME and each enzyme folder, one rename each; an Enzyme that is there
// stays.
static void move_in(const char *stage, const char *cache, const char *name) {
    char from[path_max];
    char to[path_max];
    join(from, "%s/llvm/%s", stage, name);
    join(to, "%s/llvm", cache, "");
    make_folders(to);
    join(to, "%s/llvm/%s", cache, name);
    if (rename(from, to) != 0) die("cannot move into place", to);
    join(from, "%s/enzyme", stage, "");
    DIR *dir = opendir(from);
    if (dir == nullptr) return;
    join(to, "%s/enzyme", cache, "");
    make_folders(to);
    for (const struct dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir)) { // one folder an entry
        char source[path_max];
        char target[path_max];
        if (entry->d_name[0] == '.') continue;
        join(source, "%s/enzyme/%s", stage, entry->d_name);
        join(target, "%s/enzyme/%s", cache, entry->d_name);
        if (!exists(target) && rename(source, target) != 0) die("cannot move into place", target);
    }
    closedir(dir);
}

static void write_stamp(const char *stamp, const char *cache, const char *name) {
    FILE *file = fopen(stamp, "w");
    if (file == nullptr) die("cannot write", stamp);
    fprintf(file, "%s/llvm/%s/install\n", cache, name);
    fclose(file);
}

static int fetch(const char *cache, const char *name, const char *stamp) {
    check_element(name);
    char folder[path_max];
    char id[path_max];
    join(folder, "%s/llvm/%s", cache, name);
    join(id, "%s/install/lib/smith-clang.id", folder, "");
    if (exists(id)) {
        printf("llvm_pack: %s is there already\n", folder);
        write_stamp(stamp, cache, name);
        return 0;
    }
    if (exists(folder)) {
        fprintf(stderr, "llvm_pack: %s has a build in it: not fetching over it\n", folder);
        return exit_unavailable;
    }
    char tag[path_max];
    tag_of(name, tag);
    if (!release_exists(tag)) {
        char url[path_max];
        asset_url(url, tag, tag);
        fprintf(stderr, "llvm_pack: no release %s published (HTTP 404 for %s.sha256 under %s)\n", tag, tag, url);
        return exit_unavailable;
    }
    char stage[path_max];
    join(stage, "%s/llvm/.fetch-XXXXXX", cache, "");
    char llvm[path_max];
    join(llvm, "%s/llvm", cache, "");
    make_folders(llvm);
    make_scratch(stage);
    char manifest_path[path_max];
    char archive[path_max];
    char unpacked[path_max];
    join(manifest_path, "%s/%s.sha256", stage, tag);
    join(archive, "%s/pack.tar.xz", stage, "");
    join(unpacked, "%s/unpacked", stage, "");
    char pattern[path_max];
    char url[path_max];
    join(pattern, "%s.sha256", tag, "");
    asset_url(url, tag, pattern);
    download(url, manifest_path);
    static Manifest manifest;
    read_manifest(manifest_path, &manifest);
    download_parts(tag, stage, &manifest, archive);
    make_folder(unpacked);
    run_or_die((const char *const[]){"tar", "-C", unpacked, "-xJf", archive, nullptr});
    join(id, "%s/llvm/%s/install/lib/smith-clang.id", unpacked, name);
    if (!exists(id)) die("the pack has no LLVM install named", name);
    move_in(unpacked, cache, name);
    write_stamp(stamp, cache, name);
    printf("llvm_pack: fetched %s into %s\n", tag, folder);
    return 0;
}

static int usage(void) {
    fputs(
        "usage: llvm_pack tag NAME | exists NAME | pack CACHE NAME OUT | publish CACHE NAME | fetch CACHE NAME STAMP\n",
        stderr);
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 3) return usage();
    const char *verb = argv[1];
    if (strcmp(verb, "tag") == 0 && argc == 3) {
        char tag[path_max];
        check_element(argv[2]);
        tag_of(argv[2], tag);
        puts(tag);
        return 0;
    }
    if (strcmp(verb, "exists") == 0 && argc == 3) {
        char tag[path_max];
        check_element(argv[2]);
        tag_of(argv[2], tag);
        return release_exists(tag) ? 0 : 1;
    }
    if (strcmp(verb, "pack") == 0 && argc == 5) {
        pack(argv[2], argv[3], argv[4]);
        return 0;
    }
    if (strcmp(verb, "publish") == 0 && argc == 4) {
        publish(argv[2], argv[3]);
        return 0;
    }
    if (strcmp(verb, "fetch") == 0 && argc == 5) return fetch(argv[2], argv[3], argv[4]);
    return usage();
}
