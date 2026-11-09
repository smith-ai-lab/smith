// CI's `changes` job (#2851, knowledge/ci-hosted.md): the batch wait of a merge-queue run. A queue group's branch is
// gh-readonly-queue/main/pr-<N>-<sha>, where <sha> is the head of the group ahead of it (or main's commit). So a later
// entry is being built on this one exactly when a branch named pr-*-<HEAD_SHA> exists, where HEAD_SHA is the group's
// own head (the merge_group event's head_sha, a commit the queue made), never the pull request's head. While one
// does, this run waits on its one runner and builds nothing: with HEADGREEN the later group's passing run merges this
// entry with it. When none does (at the start, or once the later groups failed or left), it returns and the run
// builds the full checks of its own tree. It never makes `ci` pass by itself: returning only lets the checks start.
//
// Usage: batch_wait OWN_REF HEAD_SHA REPO_URL
//
// A group that is last waits the batching window (window_seconds) first, so later groups of a burst can form.
// Prints `batch=alone` (no later group), `batch=waited` (waited, now last) or `batch=capped` (waited BATCH_WAIT_CAP
// seconds, 3600 by default, and builds anyway, so a wait and a full run stay under the ruleset's 120-minute check
// timeout) on stdout and exits 0. Exits 3 when its own branch is gone on two reads BATCH_WAIT_MISS_SLEEP seconds
// apart (5; the group merged or was dropped, as a jump drops every group): the caller cancels the run. A failed read
// builds (it costs runners, never safety). It polls every BATCH_WAIT_POLL seconds (30) and logs one line on stderr
// per change of state. BATCH_WAIT_LS_REMOTE replaces `git ls-remote` (the test's fake queue). Exits 2 on a usage
// error. Plain C23, libc and git through popen (build it with -std=gnu2x), no allocation.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

constexpr int exit_gone = 3;
constexpr int exit_usage = 2;
constexpr int line_size = 1024;
constexpr int later_size = 4096;
constexpr int command_size = 2048;
// the batching window (#2851): a group waits this long for later groups before it decides it is alone, since the
// lead enqueues in bursts and the queue creates their groups one after another (2026-11-09: 3 of 4 groups in a burst
// started alone and built in full). BATCH_WAIT_WINDOW replaces it (the test).
constexpr int window_seconds = 180;

typedef enum { Read_Failed, Read_Gone, Read_Last, Read_Later } Read;

static int setting(const char *name, int fallback) {
    const char *value = getenv(name);
    if (value == nullptr || value[0] == '\0') return fallback;
    char *end = nullptr;
    long n = strtol(value, &end, 10);
    return end == value || *end != '\0' || n < 0 || n > 86400 ? fallback : (int)n;
}

static int ends_with(const char *text, const char *suffix) {
    size_t a = strlen(text);
    size_t b = strlen(suffix);
    return a >= b && strcmp(text + a - b, suffix) == 0;
}

// One read of the queue's branches: whether OWN_REF is there, and the branches built on HEAD_SHA (into `later`).
static Read read_queue(const char *own, const char *head, const char *url, char later[later_size]) {
    const char *ls = getenv("BATCH_WAIT_LS_REMOTE");
    char command[command_size];
    int n = snprintf(command, sizeof command, "%s '%s' 'refs/heads/gh-readonly-queue/main/*'",
                     ls != nullptr && ls[0] != '\0' ? ls : "git ls-remote", url);
    if (n < 0 || n >= (int)sizeof command || strchr(url, '\'') != nullptr) return Read_Failed;
    // NOLINTNEXTLINE(bugprone-command-processor): git (or the test's fake), with the url checked for quotes
    FILE *pipe = popen(command, "r");
    if (pipe == nullptr) return Read_Failed;
    char suffix[128];
    snprintf(suffix, sizeof suffix, "-%s", head);
    char line[line_size];
    int own_seen = 0;
    later[0] = '\0';
    while (fgets(line, sizeof line, pipe) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        const char *tab = strchr(line, '\t');
        const char *ref = tab != nullptr ? tab + 1 : line;
        if (strcmp(ref, own) == 0)
            own_seen = 1;
        else if (strstr(ref, "/main/pr-") != nullptr && ends_with(ref, suffix) &&
                 strlen(later) + strlen(ref) + 2 < (size_t)later_size) {
            if (later[0] != '\0') strcat(later, " ");
            strcat(later, ref);
        }
    }
    if (pclose(pipe) != 0) return Read_Failed;
    if (!own_seen) return Read_Gone;
    return later[0] != '\0' ? Read_Later : Read_Last;
}

static int finish(const char *state, const char *why) {
    fprintf(stderr, "batch-wait: %s\n", why);
    printf("batch=%s\n", state);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: batch_wait OWN_REF HEAD_SHA REPO_URL\n");
        return exit_usage;
    }
    int poll = setting("BATCH_WAIT_POLL", 30);
    int cap = setting("BATCH_WAIT_CAP", 3600);
    int miss_sleep = setting("BATCH_WAIT_MISS_SLEEP", 5);
    int polls = poll > 0 ? cap / poll : cap;
    int window = setting("BATCH_WAIT_WINDOW", window_seconds);
    int window_polls = poll > 0 ? window / poll : window;
    int elapsed = 0;
    char later[later_size];
    char last[later_size] = "";
    int waited = 0;
    int misses = 0;
    // each pass is a poll or a miss; two misses in a row end it, so the bound is twice the polls of the cap
    for (int pass = 0; pass <= 2 * (polls + window_polls) + 2; pass++) {
        Read read = read_queue(argv[1], argv[2], argv[3], later);
        if (read == Read_Failed) return finish(waited ? "waited" : "alone", "ls-remote failed; running full checks");
        if (read == Read_Gone) {
            if (++misses >= 2) {
                fprintf(stderr, "batch-wait: %s is gone (merged or dropped)\n", argv[1]);
                return exit_gone;
            }
            sleep((unsigned)miss_sleep);
            continue;
        }
        misses = 0;
        if (read == Read_Last && elapsed < window_polls) {
            if (elapsed == 0) fprintf(stderr, "batch-wait: last for now; waiting %d s for later groups\n", window);
            elapsed += 1;
            sleep((unsigned)poll);
            continue;
        }
        if (read == Read_Last)
            return finish(waited ? "waited" : "alone",
                          waited ? "now last; running full checks" : "last entry; running full checks");
        if (waited >= polls) return finish("capped", "capped; running full checks");
        if (strcmp(later, last) != 0) fprintf(stderr, "batch-wait: waiting on %s\n", later);
        strcpy(last, later);
        waited += 1;
        elapsed += 1;
        sleep((unsigned)poll);
    }
    return finish("capped", "capped; running full checks");
}
