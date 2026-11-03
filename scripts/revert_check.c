// CI's `changes` job (#1473): fails a change that undoes a commit already on main, the way #1455 did when a runner's
// squash carried a stale working tree and deleted 800 lines of merged work with CI still green.
//
// Usage: revert_check BASE HEAD [BODY_FILE]
//
// BASE is the change's merge base with main, HEAD its head. For each of the last 50 first-parent commits C reachable
// from BASE, and each file f that both C and BASE..HEAD change: the change reverts C in f when it removes at least 20
// non-trivial lines that C added to f and still present in BASE, and those are at least 90% of the non-trivial lines C
// added to f. A rewrite of a few lines is normal editing and passes; a stale tree takes whole files back. A line is
// non-trivial when it has 4 or more characters after trimming (so `}` and `*/` count for nothing). A deliberate revert
// or rewrite says so: BODY_FILE (the pull request's body, or the merge group's head commit message) with `Reverts #N`
// or `Rewrites #N` skips the commit whose subject ends in `(#N)` (a rewrite: a later piece of a chain replaces the
// lines an earlier piece added, as #1875 did to #1855's).
//
// Prints one line per file and exits 1 on a hit, 0 otherwise, 2 on a usage error. Plain C23, libc and git through
// popen (so build it with -std=gnu2x: glibc hides popen under -std=c2x), no allocation: the tables are static. Files
// with more than max_lines lines are skipped.
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int window = 50;
constexpr int min_lines = 20;
constexpr int min_percent = 90;
constexpr int max_lines = 40000;
constexpr int line_size = 4096;
constexpr int path_size = 1024;
constexpr int command_size = 2048;
constexpr int body_size = 65536;
constexpr int trivial = 4;

typedef struct {
    char sha[48];
    char subject[160];
} Commit;

// A multiset of line hashes, sorted once; `take` consumes one equal entry.
typedef struct {
    uint64_t hash[max_lines];
    bool used[max_lines];
    int count;
} Lines;

static Commit commits[window];
static int commit_count;
static Lines removed;
static Lines before;
static Lines added;
static char body[body_size];

static int compare_hash(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static bool nontrivial(const char *text, uint64_t *hash) {
    while (*text == ' ' || *text == '\t')
        text++;
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        length--;
    if (length < trivial) return false;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < length; i++)
        h = (h ^ (unsigned char)text[i]) * 1099511628211ULL;
    *hash = h;
    return true;
}

static void lines_add(Lines *lines, const char *text) {
    uint64_t hash = 0;
    if (lines->count == max_lines || !nontrivial(text, &hash)) return;
    lines->hash[lines->count] = hash;
    lines->used[lines->count++] = false;
}

static void lines_sort(Lines *lines) {
    qsort(lines->hash, (size_t)lines->count, sizeof lines->hash[0], compare_hash);
}

static bool lines_has(const Lines *lines, uint64_t hash, int *index) {
    int low = 0;
    int high = lines->count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (lines->hash[mid] < hash)
            low = mid + 1;
        else
            high = mid;
    }
    for (int i = low; i < lines->count && lines->hash[i] == hash; i++) {
        if (!lines->used[i]) {
            *index = i;
            return true;
        }
    }
    return false;
}

static void lines_reset(Lines *lines) {
    for (int i = 0; i < lines->count; i++)
        lines->used[i] = false;
}

// Reads `command`'s output into `into`: the added lines ('+') or the removed ones ('-') of a -U0 diff, or every line
// ('*').
static void read_lines(const char *command, char mode, Lines *into) {
    into->count = 0;
    // NOLINTNEXTLINE(bugprone-command-processor): git, with paths checked for quotes and shas from git
    FILE *pipe = popen(command, "r");
    if (pipe == nullptr) return;
    char line[line_size];
    bool in_hunk = false;
    while (fgets(line, sizeof line, pipe) != nullptr) {
        if (mode == '*') {
            lines_add(into, line);
        } else if (strncmp(line, "@@", 2) == 0) {
            in_hunk = true;
        } else if (in_hunk && line[0] == mode) {
            lines_add(into, line + 1);
        }
    }
    pclose(pipe);
    lines_sort(into);
}

static bool quotable(const char *path) {
    return strchr(path, '\'') == nullptr && strlen(path) < path_size;
}

static int read_commits(const char *base) {
    char command[command_size];
    snprintf(command, sizeof command, "git log --first-parent -%d --format='%%H%%x09%%s' %s", window, base);
    // NOLINTNEXTLINE(bugprone-command-processor): git, with paths checked for quotes and shas from git
    FILE *pipe = popen(command, "r");
    if (pipe == nullptr) return 1;
    char line[line_size];
    while (commit_count < window && fgets(line, sizeof line, pipe) != nullptr) {
        char *tab = strchr(line, '\t');
        if (tab == nullptr) continue;
        *tab = '\0';
        tab[1 + strcspn(tab + 1, "\n")] = '\0';
        Commit *commit = &commits[commit_count++];
        snprintf(commit->sha, sizeof commit->sha, "%s", line);
        snprintf(commit->subject, sizeof commit->subject, "%s", tab + 1);
    }
    pclose(pipe);
    return 0;
}

// `(#N)` at the end of a subject, 0 when there is none.
static long subject_number(const char *subject) {
    const char *open = strrchr(subject, '(');
    if (open == nullptr || open[1] != '#') return 0;
    char *end = nullptr;
    long number = strtol(open + 2, &end, 10);
    return end != nullptr && strcmp(end, ")") == 0 ? number : 0;
}

// `word #N` in the body, not followed by another digit.
static bool body_names(const char *word, long number) {
    char want[48];
    snprintf(want, sizeof want, "%s #%ld", word, number);
    for (const char *at = body; (at = strstr(at, want)) != nullptr; at++) {
        if (!isdigit((unsigned char)at[strlen(want)])) return true;
    }
    return false;
}

static bool declared_revert(const Commit *commit) {
    long number = subject_number(commit->subject);
    if (number == 0) return false;
    return body_names("Reverts", number) || body_names("Rewrites", number);
}

// How many non-trivial lines `commit` added to `path` the change removes (and BASE still has), and how many it added.
static int reverted_lines(const Commit *commit, const char *path, int *total) {
    char command[command_size];
    snprintf(command, sizeof command, "git diff -U0 '%s^' '%s' -- '%s' 2>/dev/null", commit->sha, commit->sha, path);
    read_lines(command, '+', &added);
    *total = added.count;
    if (added.count < min_lines) return 0;
    lines_reset(&removed);
    lines_reset(&before);
    int matched = 0;
    for (int i = 0; i < added.count; i++) {
        int in_removed = 0;
        int in_before = 0;
        if (lines_has(&removed, added.hash[i], &in_removed) && lines_has(&before, added.hash[i], &in_before)) {
            removed.used[in_removed] = true;
            before.used[in_before] = true;
            matched++;
        }
    }
    return matched;
}

static bool check_file(const char *base, const char *head, const char *path) {
    char command[command_size];
    snprintf(command, sizeof command, "git diff -U0 '%s' '%s' -- '%s'", base, head, path);
    read_lines(command, '-', &removed);
    if (removed.count < min_lines) return false;
    snprintf(command, sizeof command, "git show '%s:%s' 2>/dev/null", base, path);
    read_lines(command, '*', &before);
    for (int i = 0; i < commit_count; i++) {
        if (declared_revert(&commits[i])) continue;
        int total = 0;
        int matched = reverted_lines(&commits[i], path, &total);
        if (matched < min_lines || matched * 100 < total * min_percent) continue;
        printf("%s: this change deletes %d lines that %.7s (%s) added; a stale working tree? rebase onto main and "
               "keep only your ticket's files (a deliberate rewrite says `Rewrites #N` in the pull request's body)\n",
               path, matched, commits[i].sha, commits[i].subject);
        return true;
    }
    return false;
}

static void read_body(const char *file) {
    FILE *in = fopen(file, "r");
    if (in == nullptr) return;
    size_t n = fread(body, 1, sizeof body - 1, in);
    body[n] = '\0';
    fclose(in);
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: revert_check BASE HEAD [BODY_FILE]\n");
        return 2;
    }
    if (argc == 4) read_body(argv[3]);
    if (read_commits(argv[1]) != 0) return 2;
    char command[command_size];
    snprintf(command, sizeof command, "git diff --name-only '%s' '%s'", argv[1], argv[2]);
    // NOLINTNEXTLINE(bugprone-command-processor): git, with paths checked for quotes and shas from git
    FILE *names = popen(command, "r");
    if (names == nullptr) return 2;
    static char paths[2048][path_size];
    int count = 0;
    while (count < 2048 && fgets(paths[count], path_size, names) != nullptr) {
        paths[count][strcspn(paths[count], "\n")] = '\0';
        if (quotable(paths[count])) count++;
    }
    pclose(names);
    int hits = 0;
    for (int i = 0; i < count; i++)
        hits += check_file(argv[1], argv[2], paths[i]) ? 1 : 0;
    return hits > 0 ? 1 : 0;
}
