// Builds smith in a clone that has no smith (issue #112, #293, knowledge/smith-bootstrap.md): smith is built by its
// previous release. This file is plain C23 and Apple's clang builds it:
//   mkdir -p .smith && clang -std=c23 -x c scripts/stage0.c -o .smith/stage0-tool && .smith/stage0-tool [arguments
//   of smith build]
//   1. finds the release `stage0-<commit>` of the newest ancestor commit of HEAD that has one (git tags, walking
//      `git rev-list HEAD`, after fetching the tags);
//   2. downloads it with curl over plain HTTPS from the release's public URL (no gh, no login, no GraphQL: Claude
//   Code's
//      cloud sessions refuse that, #829), checks its SHA-256 (scripts/sha256.h) and unpacks it with tar into
//      .smith/stage0/;
//   3. runs .smith/stage0/bin/smith build, with that bin/ first on PATH so that its smith-cc is the CC of the build.
// It needs git, curl, tar, the network, and `~/.cache/smith/llvm` (`smith build llvm`, about 9 minutes once). With a
// smith installed, `smith build` does all of this already. SMITH_STAGE0_RELEASE=<tag> names the release to use.
// A release is one per commit holding a tarball per platform (macOS and Linux jobs add theirs, #799), so the newest
// ancestor "with a release" is the newest one that has the asset of this platform: a ranged request for its URL says.
// On Linux, when no release can be had (no ancestor has a Linux build yet, or the download fails), it builds smith
// from source instead: scripts/seed_linux.c, the seed of the Linux release chain (issue #305), which takes the
// published LLVM pack (minutes), or builds LLVM when there is none (about two hours). SMITH_STAGE0_SEED=1 asks for the
// seed directly, and SMITH_STAGE0_SEED=0 never builds it. It compiles as strict ISO C (gcc's -std=c2x), so the POSIX
// names it uses are asked for here.
#ifndef __APPLE__
#define _POSIX_C_SOURCE 200809L
#endif
#include <fcntl.h>
#include <limits.h>
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

constexpr size_t output_max = 1 << 22; // the output of one child that is read: the tags or the history of a repository
constexpr size_t path_max = PATH_MAX;
constexpr size_t hex_length = 64; // a SHA-256 in hex
static char output[output_max + 1];
static bool seed_on_failure;  // Linux, before a release is in hand: a failure builds smith from source instead
static char **seed_arguments; // this program's arguments, the arguments of smith build

[[noreturn]] static void seed(void);

[[noreturn]] static void die(const char *message, const char *detail) {
    fprintf(stderr, "stage0: %s%s%s\n", message, detail != nullptr ? " " : "", detail != nullptr ? detail : "");
    if (seed_on_failure) seed();
    exit(1);
}

// Runs a program (found on PATH) and waits for it. With capture, its standard output is read into `output` and
// returned (nullptr for a program that failed); without, it shares ours and the result is "" for success.
static const char *run(const char *const *argv, bool capture, bool quiet) {
    int pipes[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (capture) {
        if (pipe(pipes) != 0) die("cannot make a pipe", nullptr);
        posix_spawn_file_actions_adddup2(&actions, pipes[1], 1);
        posix_spawn_file_actions_addclose(&actions, pipes[0]);
    }
    if (quiet) posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    pid_t child = 0;
    int started = posix_spawnp(&child, argv[0], &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    size_t used = 0;
    if (capture) {
        close(pipes[1]);
        for (ssize_t got = 1; started == 0 && got > 0 && used < output_max;) { // bounded by the size of the buffer
            got = read(pipes[0], output + used, output_max - used);
            if (got > 0) used += (size_t)got;
        }
        close(pipes[0]);
    }
    output[used] = '\0';
    int status = 0;
    if (started != 0 || waitpid(child, &status, 0) < 0) return nullptr;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return nullptr;
    return capture ? output : "";
}

static bool succeeds(const char *const *argv) {
    return run(argv, false, true) != nullptr;
}

// Whether `line` is one of the lines of `text`.
static bool has_line(const char *text, const char *line) {
    size_t length = strlen(line);
    for (const char *at = text; *at != '\0';) { // each pass moves past one line
        const char *end = strchr(at, '\n');
        size_t size = end != nullptr ? (size_t)(end - at) : strlen(at);
        if (size == length && strncmp(at, line, length) == 0) return true;
        if (end == nullptr) break;
        at = end + 1;
    }
    return false;
}

// The public URL of a release's file: the repository is public, so no login is needed.
static void asset_url(char *url, size_t capacity, const char *tag, const char *name) {
    snprintf(url, capacity, "https://github.com/smith-ai-lab/smith/releases/download/%s/%s", tag, name);
}

// Whether the URL answers: one byte of it is asked for, since a signed storage URL refuses a HEAD request.
static bool url_exists(const char *url) {
    return succeeds((const char *[]){"curl", "-fsSL", "--retry", "3", "-r", "0-0", "-o", "/dev/null", url, nullptr});
}

// Whether the release has the tarball and its checksum for this platform.
static bool has_asset(const char *tag, const char *name) {
    char sums[256];
    snprintf(sums, sizeof sums, "%s.sha256", name);
    char url[path_max];
    asset_url(url, sizeof url, tag, name);
    if (!url_exists(url)) return false;
    asset_url(url, sizeof url, tag, sums);
    return url_exists(url);
}

// The newest ancestor of HEAD with a release that has `name`: the tags name commits, so the first commit of
// `git rev-list HEAD` that a tag names, and whose release has the asset, is the one. A shallow clone has no history to
// walk, so it is made whole first.
static const char *find_release(char *tag, size_t capacity, const char *name) {
    const char *named = getenv("SMITH_STAGE0_RELEASE");
    if (named != nullptr && *named != '\0') {
        snprintf(tag, capacity, "%s", named);
        return tag;
    }
    if (!succeeds(
            (const char *[]){"git", "fetch", "--quiet", "--tags", "--prune", "--prune-tags", "origin", nullptr})) {
        fputs("stage0: cannot fetch the tags; using the ones here\n", stderr);
    }
    const char *shallow = run((const char *[]){"git", "rev-parse", "--is-shallow-repository", nullptr}, true, true);
    if (shallow != nullptr && strncmp(shallow, "true", 4) == 0 &&
        !succeeds((const char *[]){"git", "fetch", "--quiet", "--unshallow", "origin", nullptr})) {
        die("cannot fetch the history of a shallow clone", nullptr);
    }
    static char tags[output_max + 1];
    const char *listed = run((const char *[]){"git", "tag", "--list", "stage0-*", nullptr}, true, true);
    snprintf(tags, sizeof tags, "%s", listed != nullptr ? listed : "");
    static char history[output_max + 1]; // a copy: has_asset reads into `output` too
    const char *listed_history = run((const char *[]){"git", "rev-list", "HEAD", nullptr}, true, true);
    if (listed_history == nullptr) die("cannot read the history of HEAD", nullptr);
    snprintf(history, sizeof history, "%s", listed_history);
    for (const char *at = history; *at != '\0';) { // each pass moves past one commit
        const char *end = strchr(at, '\n');
        size_t size = end != nullptr ? (size_t)(end - at) : strlen(at);
        char candidate[128];
        snprintf(candidate, sizeof candidate, "stage0-%.*s", (int)size, at);
        if (has_line(tags, candidate) && has_asset(candidate, name)) {
            snprintf(tag, capacity, "%s", candidate);
            return tag;
        }
        if (end == nullptr) break;
        at = end + 1;
    }
    die("no ancestor of HEAD has a release (stage0-<commit>) with the tarball for this platform:", name);
}

static bool read_file(const char *path, char *into, size_t capacity) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    size_t got = fread(into, 1, capacity - 1, file);
    into[got] = '\0';
    fclose(file);
    return true;
}

// The SHA-256 of a file, as 64 lowercase hex digits (scripts/sha256.h, the same on every platform).
static bool file_digest(const char *path, char hex[hex_length + 1]) {
    return sha256_file(path, hex);
}

// Downloads the release (curl, plain HTTPS) into the folder, checks the tarball against its .sha256 and unpacks it.
static void fetch(const char *tag, const char *folder, const char *name) {
    char sums[path_max];
    char tarball[path_max];
    snprintf(sums, sizeof sums, "%s.sha256", name);
    snprintf(tarball, sizeof tarball, "%s/%s", folder, name);
    char sums_path[path_max];
    snprintf(sums_path, sizeof sums_path, "%s/%s", folder, sums);
    if (!succeeds((const char *[]){"rm", "-rf", folder, nullptr}) ||
        !succeeds((const char *[]){"mkdir", "-p", folder, nullptr}))
        die("cannot make", folder);
    char url[path_max];
    asset_url(url, sizeof url, tag, name);
    if (!succeeds((const char *[]){"curl", "-fsSL", "--retry", "3", "-o", tarball, url, nullptr}))
        die("cannot download", url);
    asset_url(url, sizeof url, tag, sums);
    if (!succeeds((const char *[]){"curl", "-fsSL", "--retry", "3", "-o", sums_path, url, nullptr}))
        die("cannot download", url);
    char expected[256];
    char actual[hex_length + 1];
    if (!read_file(sums_path, expected, sizeof expected) || strlen(expected) < hex_length ||
        !file_digest(tarball, actual) || strncmp(expected, actual, hex_length) != 0) {
        fprintf(stderr, "stage0: the checksum of %s does not match\n", tag);
        die("cannot get", tag);
    }
    if (!succeeds((const char *[]){"tar", "-C", folder, "-xJf", tarball, nullptr})) die("cannot unpack", tarball);
}

// Builds scripts/seed_linux.c with the system's C compiler ($CC, else cc) and runs it in this process's place.
[[noreturn]] static void seed(void) {
    seed_on_failure = false;
    fputs("stage0: no Linux release to use: building smith from source (scripts/seed_linux.c)\n", stderr);
    const char *cc = getenv("CC") != nullptr && *getenv("CC") != '\0' ? getenv("CC") : "cc";
    if (!run((const char *[]){cc, "-std=gnu2x", "-O1", "-x", "c", "scripts/seed_linux.c", "-o", ".smith/seed-tool",
                              nullptr},
             false, false)) {
        die("cannot compile scripts/seed_linux.c with", cc);
    }
    seed_arguments[0] = (char *)".smith/seed-tool";
    fflush(stdout);
    execv(seed_arguments[0], seed_arguments);
    die("cannot run", seed_arguments[0]);
}

int main(int argc, char **argv) {
    // The repository: the program runs from its root, or from a folder inside it.
    const char *top = run((const char *[]){"git", "rev-parse", "--show-toplevel", nullptr}, true, true);
    if (top == nullptr) die("this is not a git clone", nullptr);
    char root[path_max];
    snprintf(root, sizeof root, "%s", top);
    root[strcspn(root, "\n")] = '\0';
    if (chdir(root) != 0) die("cannot enter", root);
    struct utsname system;
    if (uname(&system) != 0) die("cannot tell the system", nullptr);
    seed_arguments = argv;
    seed_on_failure = strcmp(system.sysname, "Linux") == 0;
    const char *wanted = getenv("SMITH_STAGE0_SEED");
    if (seed_on_failure && wanted != nullptr && strcmp(wanted, "1") == 0) seed();
    if (wanted != nullptr && strcmp(wanted, "0") == 0) seed_on_failure = false; // a failure is only reported
    char name[128];
    snprintf(name, sizeof name, "smith-%s-%s.tar.xz", system.sysname, system.machine);
    for (char *c = name; *c != '\0'; c++)
        *c = (char)((*c >= 'A' && *c <= 'Z') ? *c + ('a' - 'A') : *c);

    char tag[128];
    find_release(tag, sizeof tag, name);
    const char *folder = ".smith/stage0";
    char recorded[128] = "";
    char tag_file[path_max];
    char program[path_max];
    snprintf(tag_file, sizeof tag_file, "%s/tag", folder);
    snprintf(program, sizeof program, "%s/bin/smith", folder);
    (void)read_file(tag_file, recorded, sizeof recorded);
    recorded[strcspn(recorded, "\n")] = '\0';
    if (strcmp(recorded, tag) == 0 && access(program, X_OK) == 0) {
        printf("stage0: %s is already in %s\n", tag, folder);
    } else {
        printf("stage0: downloading %s\n", tag);
        fflush(stdout);
        fetch(tag, folder, name);
        FILE *file = fopen(tag_file, "wb");
        if (file == nullptr) die("cannot write", tag_file);
        fprintf(file, "%s\n", tag);
        fclose(file);
    }
    seed_on_failure = false; // the release is here: a failure from now on is its own
    char path[path_max * 2];
    const char *old_path = getenv("PATH");
    snprintf(path, sizeof path, "%s/%s/bin:%s", root, folder, old_path != nullptr ? old_path : "/usr/bin:/bin");
    setenv("PATH", path, 1);
    fflush(stdout); // before the child writes beside it
    if (!run((const char *[]){program, "version", nullptr}, false, false)) die("the release does not run:", program);
    char *build[argc + 2];
    build[0] = program;
    build[1] = (char *)"build";
    for (int i = 1; i < argc; i++)
        build[i + 1] = argv[i];
    build[argc + 1] = nullptr;
    fflush(stdout);
    execv(program, build);
    die("cannot run", program);
}
