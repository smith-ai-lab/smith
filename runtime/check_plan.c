// The plan of `smith check` (#3163): what a change touches decides what runs. These are the rules that were three
// regexes in ci.yml's `changes` job and a list in the pre-push hook.
//
// The file stands alone (libc and git only): ci.yml's `changes` job runs before any smith is built, so it compiles this
// file with -DSMITH_CHECK_PLAN_MAIN into a tool of its own, as it does for scripts/revert_check.c, and `smith check
// --plan` is the same code.
#include "check_plan.h"

#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static bool smith_plan_ends(const char *text, const char *suffix) {
    size_t length = strlen(text);
    size_t tail = strlen(suffix);
    return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

static bool smith_plan_starts(const char *path, const char *prefix) {
    return strncmp(path, prefix, strlen(prefix)) == 0;
}

static bool smith_plan_top_level(const char *path) {
    return strchr(path, '/') == nullptr;
}

// A top-level file no build reads: the license, CONTRIBUTING.md and images.
static bool smith_plan_is_document(const char *path) {
    if (!smith_plan_top_level(path)) return false;
    if (smith_plan_starts(path, "LICENSE") || strcmp(path, "CONTRIBUTING.md") == 0) return true;
    static const char *const images[] = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp"};
    for (size_t i = 0; i < sizeof images / sizeof images[0]; i++) {
        if (smith_plan_ends(path, images[i])) return true;
    }
    return false;
}

// The documentation `smith docs` reads: top-level Markdown, knowledge/, the skills.
static bool smith_plan_is_gated(const char *path) {
    return (smith_plan_top_level(path) && smith_plan_ends(path, ".md")) || smith_plan_starts(path, "knowledge/") ||
           smith_plan_starts(path, ".agents/") || smith_plan_starts(path, ".claude/skills/");
}

// ci.yml and the actions it uses change what it runs; the other workflows change nothing it runs.
static bool smith_plan_is_own(const char *path) {
    return strcmp(path, ".github/workflows/ci.yml") == 0 || smith_plan_starts(path, ".github/actions/");
}

void smith_check_plan_add(SmithCheckPlan *plan, const char *path) {
    plan->changed++;
    if (smith_plan_starts(path, "README.md") || smith_plan_starts(path, ".agents/") ||
        smith_plan_starts(path, ".claude/skills/"))
        plan->docs_tests = true;
    bool workflow = smith_plan_starts(path, ".github/workflows/");
    bool built = smith_plan_is_own(path) || (!smith_plan_is_document(path) && !workflow);
    if (!built) return;
    plan->built++;
    if (!smith_plan_is_gated(path)) plan->code++;
}

SmithCheckScope smith_check_plan_scope(const SmithCheckPlan *plan) {
    if (plan->built == 0) return SmithCheckScope_None;
    return plan->code == 0 ? SmithCheckScope_Docs : SmithCheckScope_Full;
}

const char *smith_check_scope_name(SmithCheckScope scope) {
    switch (scope) {
    case SmithCheckScope_None:
        return "none";
    case SmithCheckScope_Docs:
        return "docs";
    case SmithCheckScope_Full:
        return "full";
    }
    return "full";
}

// A ref git can read: letters, digits and the characters of a name or a sha, never an option.
static bool smith_plan_name_ok(const char *name) {
    if (name[0] == '\0' || name[0] == '-') return false;
    for (const char *c = name; *c != '\0'; c++) { // bounded by the name
        bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                  strchr("._/-~^@", *c) != nullptr;
        if (!ok) return false;
    }
    return true;
}

// Runs git with `argv` and gives each line it prints to `line`; false when git fails.
static bool smith_plan_git(const char *const *argv, void (*line)(char *, void *), void *context) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int started = posix_spawnp(&pid, "git", &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    FILE *out = fdopen(fds[0], "r");
    if (started != 0 || out == nullptr) {
        close(fds[0]);
        return false;
    }
    char text[PATH_MAX + 8];
    while (fgets(text, sizeof text, out) != nullptr) { // bounded by git's output
        text[strcspn(text, "\n")] = '\0';
        if (text[0] != '\0') line(text, context);
    }
    fclose(out);
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void smith_plan_on_path(char *text, void *plan) {
    smith_check_plan_add(plan, text);
}

static void smith_plan_on_base(char *text, void *base) {
    snprintf(base, 256, "%.255s", text);
}

// The paths that differ from `base` (default: where the branch left origin/main) to HEAD. False when git cannot say.
bool smith_check_plan_read(const char *base, SmithCheckPlan *plan) {
    char merged[256] = "";
    if (base == nullptr) {
        const char *const merge[] = {"git", "merge-base", "HEAD", "origin/main", nullptr};
        if (!smith_plan_git(merge, smith_plan_on_base, merged) || merged[0] == '\0') return false;
        base = merged;
    }
    if (!smith_plan_name_ok(base)) return false;
    const char *const diff[] = {"git", "diff", "--name-only", "--no-renames", base, "HEAD", "--", nullptr};
    *plan = (SmithCheckPlan){};
    return smith_plan_git(diff, smith_plan_on_path, plan);
}

// The scope an event runs. A push to main is a commit the queue already checked: it only publishes the release, unless
// the push changed only documents (#3044), which publishes nothing (none). Any other event runs what the diff touches.
static const char *smith_plan_event_scope(const char *event, bool read, const SmithCheckPlan *plan, bool *docs_push) {
    SmithCheckScope scope = read ? smith_check_plan_scope(plan) : SmithCheckScope_Full;
    *docs_push = false;
    if (strcmp(event, "push") != 0) return smith_check_scope_name(scope);
    if (read && plan->changed != 0 && scope != SmithCheckScope_Full) {
        *docs_push = true;
        return "none";
    }
    return "release";
}

int smith_check_plan_print(const char *base, const char *event) {
    SmithCheckPlan plan = {};
    bool read = smith_check_plan_read(base, &plan);
    if (!read)
        fputs("smith check --plan: the diff cannot be read (no base: pass --base <ref>): the scope is full\n", stderr);
    bool docs_push = false;
    const char *scope = smith_plan_event_scope(event, read, &plan, &docs_push);
    // the macOS jobs run after the merge (#3172): on a push that changed more than documents, and in the nightly and
    // by-hand runs of the whole check; a queue run has none, and a documents-only change none at all
    bool mac_run =
        (strcmp(event, "push") == 0 && strcmp(scope, "release") == 0) ||
        ((strcmp(event, "schedule") == 0 || strcmp(event, "workflow_dispatch") == 0) && strcmp(scope, "full") == 0);
    bool docs_tests = read && strcmp(scope, "docs") == 0 && plan.docs_tests;
    printf("scope=%s\ndocs_tests=%s\ndocs_push=%s\nmac_run=%s\n", scope, docs_tests ? "true" : "false",
           docs_push ? "true" : "false", mac_run ? "true" : "false");
    if (read)
        printf("changed=%zu\n", plan.changed);
    else
        puts("changed=unknown");
    // Linux carries the tests: --skip-macos in parts; the Mac runs the few tests only macOS can run, in one part
    puts("test_parts=8\nlinux_flags=--skip-macos\nmac_flags=--macos-only --shard 1/1");
    return 0;
}

#ifdef SMITH_CHECK_PLAN_MAIN
int main(int argc, char **argv) {
    const char *base = nullptr;
    const char *event = "";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--base") == 0 && i + 1 < argc)
            base = argv[++i];
        else if (strcmp(argv[i], "--event") == 0 && i + 1 < argc)
            event = argv[++i];
        else {
            fputs("usage: check_plan [--base <ref>] [--event <name>]\n", stderr);
            return 2;
        }
    }
    return smith_check_plan_print(base, event);
}
#endif
