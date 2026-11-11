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
// A later group whose run has a failed job (#2903) cannot merge this entry with it, so the wait ends at once and the
// run builds: each poll reads the latest `ci.yml` merge_group run of every later group's branch (`gh api`, with
// GH_TOKEN; BATCH_WAIT_GH replaces `gh`) and its jobs, and prints `batch=waited` with a line on stderr naming the
// group and the job. Without that, the 2026-11-09 waits (runs 42582094132, 42582096340) lasted the cap while later
// groups failed one after another. A failed read of the run or its jobs is "no failure": the wait goes on.
//
// With the ruleset `main`'s `max_entries_to_build` at 1 (#2925) no later group can be built while this one runs, so
// the window and the polling can only wait: it logs `batch: alone (build entries 1)`, prints `batch=alone` and
// returns at once. The ruleset is read with `gh api` (repos/R/rules/branches/main); 2 or more, or a
// failed read, waits as below. (The knob is the ruleset, set with queue-ops `set-build-entries`, #2922.)
//
// A batch has one deadline, never restarted (#2925; owner, 2026-11-09: "Whatever gets in gets in. We wait no more
// then n minutes then we start."). The batch is the groups in a row ahead of this one that have not started their full
// build (a heavy job, any job but `changes`, in progress or done); its first group's run was created at T, and the
// deadline is T + window_seconds (60 s). A group that is last waits for the deadline, or for a later group, and builds
// at once if the deadline is past; the group ahead walks back by the chain of `pr-<N>-<sha>` branches. A run that
// cannot be read counts this group's own start as T, as before. It logs `batch: deadline <time> (batch from <ref>)`
// once and `batch: deadline reached, building`. BATCH_WAIT_NOW (an epoch) and BATCH_WAIT_TICK (seconds per pass) fake
// the clock in the test.
//
// One full build at a time (#3057). The window alone joined only entries that arrive within a minute: measured over
// 48 hours, 5 to 6 entries were in the queue when a passing run started and it merged 1.6, because entries arrive
// minutes apart and each group that was last for a minute built in full beside the one ahead. So a group that is last
// also waits while a group ahead of it has started its full build (found once by the walk back, then only looked for
// in the branch listing, so the wait costs no API call): it logs `batch-wait: group ahead <ref> is building; waiting
// for it`. When that branch goes (its group merged, or failed and was removed) the newest group builds after
// BATCH_WAIT_SETTLE seconds (15; the queue is still creating the groups that the merge made room for) and its tree
// holds every entry that queued meanwhile. The cap bounds this wait too.
//
// A failed full build splits (#3057). The run that failed held every entry of its batch, so the group ahead of it
// stops waiting and builds (`batch=split`), and the workflow's step `Split: a later queue group failed` then runs in
// its `changes` job; the group ahead of that one reads the step in the jobs of its later group's run and splits too,
// and so on to the front: every group ahead of the failed one builds at once, the longest that passes merges, and
// the first that fails names the bad entry. A split does not wait for a group ahead.
//
// Prints `batch=alone` (no later group), `batch=waited` (waited, now last), `batch=split` or `batch=capped` (waited
// BATCH_WAIT_CAP seconds, 3600 by default, and builds anyway, so a wait and a full run stay under the ruleset's
// 120-minute check timeout) on stdout and exits 0. Exits 3 when its own branch is gone on two reads
// BATCH_WAIT_MISS_SLEEP seconds apart (5; the group merged or was dropped, as a jump drops every group): the caller
// cancels the run. A failed read builds (it costs runners, never safety). It polls every BATCH_WAIT_POLL seconds (30)
// and logs one line on stderr per change of state. BATCH_WAIT_LS_REMOTE replaces `git ls-remote` (the test's fake
// queue). Exits 2 on a usage error. Plain C23, libc and git through popen (build it with -std=gnu2x), no allocation.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

constexpr int exit_gone = 3;
constexpr int exit_usage = 2;
constexpr int line_size = 1024;
constexpr int later_size = 4096;
constexpr int command_size = 2048;
constexpr int listing_size = 8192;
constexpr int chain_depth = 32;
// the batching window (#2851): a batch waits this long for later groups before its last group decides it is alone,
// since the lead enqueues in bursts and the queue creates their groups one after another (2026-11-09: 3 of 4 groups
// in a burst started alone and built in full). It is one deadline per batch, counted from the batch's first run and
// never restarted (#2925). BATCH_WAIT_WINDOW replaces it (the test).
constexpr int window_seconds = 60;

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
static Read read_queue(const char *own, const char *head, const char *url, char later[later_size],
                       char listing[listing_size]) {
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
    listing[0] = '\0';
    while (fgets(line, sizeof line, pipe) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        const char *tab = strchr(line, '\t');
        const char *ref = tab != nullptr ? tab + 1 : line;
        size_t used = strlen(listing);
        if (tab != nullptr && used + strlen(line) + 2 < (size_t)listing_size)
            snprintf(listing + used, listing_size - used, "%.*s %s\n", (int)(tab - line), line, ref);
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

// The text of one command's first line of output, empty when it fails (a failed read is "no failure").
static int first_line(const char *command, char out[line_size]) {
    out[0] = '\0';
    // NOLINTNEXTLINE(bugprone-command-processor): gh (or the test's fake), with the branch and repo checked for quotes
    FILE *pipe = popen(command, "r");
    if (pipe == nullptr) return 0;
    if (fgets(out, line_size, pipe) == nullptr) out[0] = '\0';
    out[strcspn(out, "\r\n")] = '\0';
    return pclose(pipe) == 0 && out[0] != '\0';
}

// The name of a failed job in the latest merge_group run of `branch` (a ref of a later group), `split` when it has
// none but its split step ran (#3057), or "" when there is neither or a read failed. `repo` is owner/name.
static int failed_job(const char *repo, const char *branch, char name[line_size]) {
    const char *gh = getenv("BATCH_WAIT_GH");
    const char *tool = gh != nullptr && gh[0] != '\0' ? gh : "gh";
    name[0] = '\0';
    if (strchr(repo, '\'') != nullptr || strchr(branch, '\'') != nullptr) return 0;
    char command[command_size];
    char id[line_size];
    int n = snprintf(command, sizeof command,
                     "%s api -X GET 'repos/%s/actions/runs' -f event=merge_group -f branch='%s' "
                     "--jq '.workflow_runs[0].id // empty'",
                     tool, repo, branch);
    if (n < 0 || n >= (int)sizeof command || !first_line(command, id) || strchr(id, '\'') != nullptr) return 0;
    n = snprintf(command, sizeof command,
                 "%s api 'repos/%s/actions/runs/%s/jobs?filter=latest&per_page=100' "
                 "--jq '([.jobs[]|select(.conclusion==\"failure\")|.name]|first) // ([.jobs[].steps[]?|"
                 "select((.name|startswith(\"Split:\")) and .conclusion==\"success\")|\"split\"]|first) // empty'",
                 tool, repo, id);
    return n >= 0 && n < (int)sizeof command && first_line(command, name);
}

// Whether any group of `later` (refs separated by spaces) has a failed job or is splitting; names the group and the
// job on stderr.
static int later_failed(const char *url, const char *later) {
    const char *repo = strstr(url, "github.com/");
    repo = repo != nullptr ? repo + strlen("github.com/") : url;
    char refs[later_size];
    snprintf(refs, sizeof refs, "%s", later);
    char name[line_size];
    for (char *ref = strtok(refs, " "); ref != nullptr; ref = strtok(nullptr, " ")) {
        const char *branch = strncmp(ref, "refs/heads/", 11) == 0 ? ref + 11 : ref;
        if (!failed_job(repo, branch, name)) continue;
        if (strcmp(name, "split") == 0)
            fprintf(stderr, "batch-wait: later group %s is splitting (a group behind it failed), building now\n", ref);
        else
            fprintf(stderr, "batch-wait: later group %s failed (job %s), building now\n", ref, name);
        return 1;
    }
    return 0;
}

// The `max_entries_to_build` of the merge_queue rule that applies to main (the rules of the branch, metadata read
// only), or 0 when a read failed.
static int build_entries(const char *url) {
    const char *gh = getenv("BATCH_WAIT_GH");
    const char *tool = gh != nullptr && gh[0] != '\0' ? gh : "gh";
    const char *repo = strstr(url, "github.com/");
    repo = repo != nullptr ? repo + strlen("github.com/") : url;
    if (strchr(repo, '\'') != nullptr) return 0;
    char command[command_size];
    int n = snprintf(command, sizeof command,
                     "%s api 'repos/%s/rules/branches/main' --jq '[.[]|select(.type==\"merge_queue\")|"
                     ".parameters.max_entries_to_build]|first // empty'",
                     tool, repo);
    char value[line_size];
    if (n < 0 || n >= (int)sizeof command || !first_line(command, value)) return 0;
    char *end = nullptr;
    long entries = strtol(value, &end, 10);
    return end == value || *end != '\0' || entries < 1 || entries > 100 ? 0 : (int)entries;
}

static long clock_now(int pass) {
    const char *fake = getenv("BATCH_WAIT_NOW");
    if (fake == nullptr || fake[0] == '\0') return (long)time(nullptr);
    return strtol(fake, nullptr, 10) + (long)pass * setting("BATCH_WAIT_TICK", 1);
}

// The latest merge_group run of `branch`: its creation time (epoch) and whether its full build started (a job other
// than `changes` is in progress or done). False when a read failed.
static int group_run(const char *repo, const char *branch, long *created, int *building) {
    const char *gh = getenv("BATCH_WAIT_GH");
    const char *tool = gh != nullptr && gh[0] != '\0' ? gh : "gh";
    if (strchr(repo, '\'') != nullptr || strchr(branch, '\'') != nullptr) return 0;
    char command[command_size];
    char text[line_size];
    int n = snprintf(command, sizeof command,
                     "%s api -X GET 'repos/%s/actions/runs' -f event=merge_group -f branch='%s' "
                     "--jq '.workflow_runs[0]|select(.!=null)|\"\\(.id)\\t\\(.created_at|fromdateiso8601)\"'",
                     tool, repo, branch);
    if (n < 0 || n >= (int)sizeof command || !first_line(command, text)) return 0;
    // "<id>\t<epoch>"
    char *end = nullptr;
    long number = strtol(text, &end, 10);
    if (end == text || *end != '\t' || number <= 0) return 0;
    char id[64];
    snprintf(id, sizeof id, "%ld", number);
    *created = strtol(end + 1, &end, 10);
    if (*end != '\0' || *created <= 0) return 0;
    n = snprintf(command, sizeof command,
                 "%s api 'repos/%s/actions/runs/%s/jobs?filter=latest&per_page=100' --jq '[.jobs[]|"
                 "select(.name!=\"changes\" and (.status==\"in_progress\" or .status==\"completed\"))]|length'",
                 tool, repo, id);
    if (n < 0 || n >= (int)sizeof command || !first_line(command, text)) return 0;
    *building = strtol(text, nullptr, 10) > 0;
    return 1;
}

// The sha of `ref`'s line in `listing` ("sha ref" lines), or "" when there is none.
static const char *ref_of_sha(const char *listing, const char *sha, char ref[line_size]) {
    ref[0] = '\0';
    size_t length = strlen(sha);
    for (const char *line = listing; *line != '\0';
         line = strchr(line, '\n') != nullptr ? strchr(line, '\n') + 1 : line + strlen(line)) {
        if (strncmp(line, sha, length) != 0 || line[length] != ' ') continue;
        snprintf(ref, line_size, "%.*s", (int)strcspn(line + length + 1, "\n"), line + length + 1);
        break;
    }
    return ref;
}

// The first group of the batch `own` is in: walk back through the groups ahead (the sha after the last `-` of a ref
// is the head of the group ahead) while they have not started their full build. Returns the ref of the first group
// and its run's creation time in `created`, or 0 in `created` when that is this group or a read failed. `blocker` is
// the ref of the group ahead that stopped the walk because its full build started, or "".
static void batch_first(const char *url, const char *own, const char *listing, char first[line_size], long *created,
                        char blocker[line_size]) {
    const char *repo = strstr(url, "github.com/");
    repo = repo != nullptr ? repo + strlen("github.com/") : url;
    snprintf(first, line_size, "%s", own);
    *created = 0;
    blocker[0] = '\0';
    char ref[line_size];
    char ahead[line_size];
    snprintf(ref, sizeof ref, "%s", own);
    for (int depth = 0; depth < chain_depth; depth++) {
        const char *dash = strrchr(ref, '-');
        if (dash == nullptr || ref_of_sha(listing, dash + 1, ahead)[0] == '\0') return;
        const char *branch = strncmp(ahead, "refs/heads/", 11) == 0 ? ahead + 11 : ahead;
        long at = 0;
        int building = 0;
        if (!group_run(repo, branch, &at, &building)) return;
        if (building) {
            snprintf(blocker, line_size, "%s", ahead);
            return;
        }
        snprintf(first, line_size, "%s", ahead);
        *created = at;
        snprintf(ref, sizeof ref, "%s", ahead);
    }
}

// The batch's deadline (epoch), logged once: its first group's run creation plus the window, or this run's own start
// plus the window when this group is first or a read failed. `blocker` is the group ahead that is building, or "".
static long batch_deadline(const char *url, const char *own, const char *listing, long start, int window,
                           char blocker[line_size]) {
    char first[line_size];
    long created = 0;
    batch_first(url, own, listing, first, &created, blocker);
    long base = created > 0 ? created : start;
    time_t when = (time_t)(base + window);
    char text[64];
    struct tm parts;
    gmtime_r(&when, &parts);
    strftime(text, sizeof text, "%H:%M:%SZ", &parts);
    fprintf(stderr, "batch: deadline %s (batch from %s)\n", text, first);
    return base + window;
}

// Whether `ref` is a branch of `listing` ("sha ref" lines).
static int listed(const char *listing, const char *ref) {
    size_t length = strlen(ref);
    for (const char *at = strstr(listing, ref); at != nullptr; at = strstr(at + 1, ref))
        if (at > listing && at[-1] == ' ' && at[length] == '\n') return 1;
    return 0;
}

static int finish(const char *state, const char *why) {
    fprintf(stderr, "batch-wait: %s\n", why);
    printf("batch=%s\n", state);
    return 0;
}

// What the last group knows between polls: the batch's deadline (-1 until computed), the end of the settle time, and
// the group ahead whose full build it waits for ("" for none).
typedef struct {
    long start;
    long deadline;
    long settled;
    int window;
    int settle;
    char blocker[line_size];
} Last;

// One poll as the last group: 0 to build now, 1 to wait for the full build ahead, 2 to wait for the deadline.
static int last_waits(Last *last, const char *own, const char *url, const char *listing, long now) {
    if (last->blocker[0] != '\0' && !listed(listing, last->blocker)) {
        fprintf(stderr, "batch-wait: %s is gone; no group ahead is building\n", last->blocker);
        last->blocker[0] = '\0';
        last->deadline = -1;
        last->settled = now + last->settle;
    }
    if (last->deadline < 0) {
        // walked again after a group ahead went: another one may be building (a split builds several)
        last->deadline = batch_deadline(url, own, listing, last->start, last->window, last->blocker);
        if (last->deadline < last->settled) last->deadline = last->settled;
        if (last->blocker[0] != '\0')
            fprintf(stderr, "batch-wait: group ahead %s is building; waiting for it\n", last->blocker);
    }
    if (last->blocker[0] != '\0') return 1;
    if (now >= last->deadline) {
        if (last->window > 0) fprintf(stderr, "batch: deadline reached, building\n");
        return 0;
    }
    fprintf(stderr, "batch-wait: last for now; waiting %ld s for later groups\n", last->deadline - now);
    return 2;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: batch_wait OWN_REF HEAD_SHA REPO_URL\n");
        return exit_usage;
    }
    if (build_entries(argv[3]) == 1) {
        fprintf(stderr, "batch: alone (build entries 1)\n");
        printf("batch=alone\n");
        return 0;
    }
    int poll = setting("BATCH_WAIT_POLL", 30);
    int cap = setting("BATCH_WAIT_CAP", 3600);
    int miss_sleep = setting("BATCH_WAIT_MISS_SLEEP", 5);
    int polls = poll > 0 ? cap / poll : cap;
    int window = setting("BATCH_WAIT_WINDOW", window_seconds);
    int window_polls = poll > 0 ? window / poll : window;
    Last last = {.start = clock_now(0), .deadline = -1, .window = window, .settle = setting("BATCH_WAIT_SETTLE", 15)};
    char later[later_size];
    char listing[listing_size];
    char logged[later_size] = "";
    int waited = 0;
    int misses = 0;
    // each pass is a poll or a miss; two misses in a row end it, so the bound is twice the polls of the cap
    for (int pass = 0; pass <= 2 * (polls + window_polls) + 2; pass++) {
        Read read = read_queue(argv[1], argv[2], argv[3], later, listing);
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
        if (read == Read_Last) {
            int wait = last_waits(&last, argv[1], argv[3], listing, clock_now(pass));
            if (wait == 0)
                return finish(waited ? "waited" : "alone",
                              waited ? "now last; running full checks" : "last entry; running full checks");
            if (wait == 1 && waited >= polls) return finish("capped", "capped; running full checks");
            if (wait == 1) waited += 1;
            sleep((unsigned)poll);
            continue;
        }
        if (waited >= polls) return finish("capped", "capped; running full checks");
        if (later_failed(argv[3], later)) return finish("split", "a later group failed; running full checks");
        if (strcmp(later, logged) != 0) fprintf(stderr, "batch-wait: waiting on %s\n", later);
        strcpy(logged, later);
        waited += 1;
        sleep((unsigned)poll);
    }
    return finish("capped", "capped; running full checks");
}
