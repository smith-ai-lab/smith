// `smith build bench` (issue #639): measures each budget line of the speed table on this machine and prints it. Every
// step runs one command and reports its wall seconds and the CPU seconds (user plus system) of the command and every
// process it waited for, from wait4's rusage. The table is also written as tab-separated lines for the train to record
// and compare: step, wall s, CPU s, budget s (or -), verdict (ok, over, tbd, -), load average at the start.
//
// Usage: bench SMITH TSV [full] [preflight]
// SMITH is the smith under test (an absolute or repository-relative path); TSV is the file written. `full` also runs
// `smith test` (slow; the line is CPU seconds), `preflight` also runs `smith build preflight`.
// The commands run with SMITH_SLOTS=0 and SMITH_CORES=4: a wait for a slot is the queue's, not the step's.
// Plain C23, libc only, no allocation of its own beyond the static tables.
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

constexpr int max_rows = 32;
constexpr int module_functions = 110;

typedef struct {
    char name[64];
    double wall;
    double cpu;
    double budget; // seconds; negative: none ("TBD" when tbd)
    bool by_cpu;   // the budget is on CPU seconds, else on wall
    bool tbd;
    double load;
    int status;
} Row;

static Row rows[max_rows];
static int row_count;
static const char *smith_path;

static double seconds_of(struct timeval t) {
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

// Runs argv in `folder` (or here), output to /dev/null; returns the exit status, wall and CPU through the pointers.
static const char *capture_path; // when set, the next measure's stdout goes here

static int measure(char *const argv[], const char *folder, double *wall, double *cpu) {
    fflush(stdout);
    double start = now();
    pid_t pid = fork();
    if (pid == 0) {
        if (folder != nullptr && chdir(folder) != 0) _exit(126);
        freopen(capture_path != nullptr ? capture_path : "/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    struct rusage usage;
    while (wait4(pid, &status, 0, &usage) < 0)
        if (errno != EINTR) return 127;
    *wall = now() - start;
    *cpu = seconds_of(usage.ru_utime) + seconds_of(usage.ru_stime);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static Row *record(const char *name, double budget, bool by_cpu) {
    Row *row = &rows[row_count < max_rows ? row_count++ : max_rows - 1];
    snprintf(row->name, sizeof row->name, "%s", name);
    row->budget = budget;
    row->by_cpu = by_cpu;
    row->tbd = false;
    double load[1] = {};
    getloadavg(load, 1);
    row->load = load[0];
    return row;
}

static void step(const char *name, double budget, bool by_cpu, char *const argv[], const char *folder) {
    Row *row = record(name, budget, by_cpu);
    row->status = measure(argv, folder, &row->wall, &row->cpu);
    fprintf(stderr, "bench: %s: %.2f s wall, %.2f s cpu, status %d\n", name, row->wall, row->cpu, row->status);
}

static void run_quiet(char *const argv[], const char *folder) {
    double wall = 0, cpu = 0;
    (void)measure(argv, folder, &wall, &cpu);
}

static bool read_file(const char *path, char *text, size_t size, size_t *used) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    *used = fread(text, 1, size - 1, file);
    fclose(file);
    text[*used] = '\0';
    return true;
}

static bool write_file(const char *path, const char *text, size_t used) {
    FILE *file = fopen(path, "wb");
    if (file == nullptr) return false;
    bool ok = fwrite(text, 1, used, file) == used;
    return fclose(file) == 0 && ok;
}

// build with nothing changed: one warm-up build first so the measured one is a true no-op.
static void bench_noop(void) {
    char *const argv[] = {(char *)smith_path, "build", nullptr};
    run_quiet(argv, nullptr);
    step("build no-op", 0.5, false, argv, nullptr);
}

// A one-line comment appended to runtime/slots.c (restored after), then built.
static void bench_one_file(void) {
    static char original[1 << 20];
    static char changed[(1 << 20) + 64];
    const char *path = "runtime/slots.c";
    size_t used = 0;
    if (!read_file(path, original, sizeof original, &used)) return;
    size_t length = (size_t)snprintf(changed, sizeof changed, "%s\n// bench %ld\n", original, (long)time(nullptr));
    char *const argv[] = {(char *)smith_path, "build", nullptr};
    if (write_file(path, changed, length)) step("build after a one-file change", 3.0, false, argv, nullptr);
    write_file(path, original, used);
    run_quiet(argv, nullptr); // the tree is as it was, rebuilt
}

// A fresh worktree of HEAD, built with the caches this machine has already filled.
static void bench_worktree(void) {
    const char *folder = ".smith/bench-worktree";
    char *const clean[] = {"git", "worktree", "remove", "--force", (char *)folder, nullptr};
    char *const add[] = {"git", "worktree", "add", "--detach", (char *)folder, "HEAD", nullptr};
    run_quiet(clean, nullptr);
    double wall = 0, cpu = 0;
    if (measure(add, nullptr, &wall, &cpu) != 0) return;
    char absolute[PATH_MAX];
    snprintf(absolute, sizeof absolute, "%s", smith_path);
    char *const argv[] = {absolute, "build", nullptr};
    step("build in a fresh worktree", 10.0, false, argv, folder);
    run_quiet(clean, nullptr);
}

// fmt of the files the last commit changed (at most five), and lint of the project.
static void bench_fmt_lint(void) {
    char *argv[16] = {(char *)smith_path, "fmt", nullptr};
    int count = 2;
    static char names[5][PATH_MAX];
    const char *path = ".smith/bench/changed.txt";
    char *const diff[] = {"git", "diff", "--name-only", "HEAD~1", "HEAD", nullptr};
    double wall = 0, cpu = 0;
    capture_path = path;
    (void)measure(diff, nullptr, &wall, &cpu);
    capture_path = nullptr;
    FILE *list = fopen(path, "r");
    while (list != nullptr && count < 7 && fgets(names[count - 2], PATH_MAX, list) != nullptr) {
        char *name = names[count - 2];
        name[strcspn(name, "\n")] = '\0';
        size_t length = strlen(name);
        bool source = length > 2 && (strcmp(name + length - 2, ".c") == 0 || strcmp(name + length - 2, ".h") == 0 ||
                                     (length > 6 && strcmp(name + length - 6, ".smith") == 0));
        if (source && access(name, R_OK) == 0) argv[count++] = name;
    }
    if (list != nullptr) fclose(list);
    if (count == 2) argv[count++] = "runtime/slots.c";
    argv[count] = nullptr;
    step("fmt on a typical change", 0.5, false, argv, nullptr);
    char *const whole[] = {(char *)smith_path, "fmt", "--dry-run", nullptr};
    step("fmt of the whole project (dry run)", -1, false, whole, nullptr);
    char *const changed[] = {(char *)smith_path, "lint", "--changed=HEAD~1", "--quiet", nullptr};
    step("lint on a typical change (--changed=HEAD~1)", 3.0, false, changed, nullptr);
    char *const lint[] = {(char *)smith_path, "lint", "--quiet", nullptr};
    step("lint of the whole project", -1, false, lint, nullptr);
    char *const docs[] = {(char *)smith_path, "docs", nullptr};
    step("docs (AGENTS.md, knowledge, skills)", -1, false, docs, nullptr);
}

// A module of about 1,000 lines: module_functions functions of ten lines, generated, compiled to an object.
static void bench_module(void) {
    static char text[1 << 18];
    const char *folder = ".smith/bench";
    mkdir(".smith", 0755);
    mkdir(folder, 0755);
    size_t length = (size_t)snprintf(text, sizeof text,
                                     "module bench::big;\n\nexport struct Point {\n    I32 x;\n    I32 y;\n};\n\n");
    for (int i = 0; i < module_functions; i++)
        length +=
            (size_t)snprintf(text + length, sizeof text - length,
                             "export I32 f%d(Point p, I32 n) {\n    I32 total = 0;\n    for (I32 i = 0; i < n; i++) {\n"
                             "        if (i %% 2 == 0) total += p.x * i + %d;\n        else total -= p.y + i;\n    }\n"
                             "    return total;\n}\n\n",
                             i, i);
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/big.smith", folder);
    if (!write_file(path, text, length)) return;
    int lines = 0;
    for (size_t i = 0; i < length; i++)
        lines += text[i] == '\n';
    char *const argv[] = {(char *)smith_path, "cc", "-c", path, "-o", ".smith/bench/big.o", nullptr};
    char name[64];
    snprintf(name, sizeof name, "compile a .smith module of %d lines", lines);
    run_quiet(argv, nullptr); // the first run writes the embedded files
    step(name, 0, false, argv, nullptr);
    rows[row_count - 1].tbd = true;
}

static void bench_long(bool full, bool preflight) {
    if (full) {
        char *const argv[] = {(char *)smith_path, "test", nullptr};
        step("full test suite (CPU s)", 600.0, true, argv, nullptr);
    }
    if (preflight) {
        char *const argv[] = {(char *)smith_path, "build", "preflight", nullptr};
        step("preflight end to end", 300.0, false, argv, nullptr);
    }
}

static const char *verdict(const Row *row) {
    if (row->status != 0) return "failed";
    if (row->tbd) return "tbd";
    if (row->budget < 0) return "-";
    return (row->by_cpu ? row->cpu : row->wall) < row->budget ? "ok" : "over";
}

static void print_budget(char *text, size_t size, const Row *row) {
    if (row->tbd)
        snprintf(text, size, "TBD");
    else if (row->budget < 0)
        snprintf(text, size, "-");
    else
        snprintf(text, size, "<%.1f%s", row->budget, row->by_cpu ? " cpu" : "");
}

static int report(const char *tsv) {
    FILE *file = fopen(tsv, "w");
    double load[3] = {0, 0, 0};
    getloadavg(load, 3);
    printf("%-46s %8s %8s  %-10s %s\n", "step", "wall s", "cpu s", "budget s", "verdict");
    int failed = 0;
    for (int i = 0; i < row_count; i++) {
        const Row *row = &rows[i];
        char budget[32];
        print_budget(budget, sizeof budget, row);
        printf("%-46s %8.2f %8.2f  %-10s %s\n", row->name, row->wall, row->cpu, budget, verdict(row));
        failed += row->status != 0;
        if (file != nullptr)
            fprintf(file, "%s\t%.3f\t%.3f\t%.3f\t%s\t%.2f\n", row->name, row->wall, row->cpu,
                    row->tbd || row->budget < 0 ? -1.0 : row->budget, verdict(row), row->load);
    }
    printf("load average at the end: %.2f %.2f %.2f (the machine is shared; compare the CPU column)\n", load[0],
           load[1], load[2]);
    if (file != nullptr) fclose(file);
    return failed != 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: bench SMITH TSV [full] [preflight]\n", stderr);
        return 2;
    }
    static char absolute[PATH_MAX];
    smith_path = realpath(argv[1], absolute) != nullptr ? absolute : argv[1];
    bool full = false;
    bool preflight = false;
    for (int i = 3; i < argc; i++) {
        full = full || strcmp(argv[i], "full") == 0;
        preflight = preflight || strcmp(argv[i], "preflight") == 0;
    }
    // The steps measure the work, not the queue: no wait for a slot (a CI job holding them would add minutes of wall),
    // and the share of one slot, four cores, which is what the budgets are stated for.
    setenv("SMITH_SLOTS", "0", 1);
    setenv("SMITH_CORES", "4", 1);
    bench_noop();
    bench_one_file();
    bench_worktree();
    bench_fmt_lint();
    bench_module();
    bench_long(full, preflight);
    return report(argv[2]);
}
