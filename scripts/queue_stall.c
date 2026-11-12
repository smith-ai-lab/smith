// The merge queue's stall alarm (#3444): decides from the last finished `merge_group` runs of ci whether the queue is
// stalled, and writes the body of the issue that says so. On 2026-11-12 the queue merged nothing from 19:17Z for over
// an hour while main stayed green, and nothing measured it. The workflow queue-stall.yml fetches the runs with `gh api`
// and calls this tool; the tool never touches the network, so a test can replay the runs.
//
// Usage: queue_stall < RUNS
//
// RUNS is tab separated, one record a line, runs newest first, only runs that have finished:
//   run  ID  PR  CONCLUSION  URL  FIRST_FAILING_JOB   (a run; FIRST_FAILING_JOB may be empty)
//   err  TEXT                                         (a `##[error]` or `frontend-*` line of the run just above)
//   held TEXT                                         (a gate held in report-only, listed when the issue is open)
//
// Prints one verdict on the first line and exits 0:
//   stalled  three or more runs and none of the newest three ended `success`; the issue body follows on later lines
//   green    the newest run ended `success` (the workflow closes the issue when it is open)
//   quiet    anything else: fewer than three runs, or a mix with a success among the newest three but not the newest
// Exits 2 on a usage error. Plain C23 and libc, no allocation.
#include <stdio.h>
#include <string.h>

constexpr int window = 3;
constexpr int max_errors = 8;
constexpr int max_held = 8;
constexpr int line_size = 1024;

struct Run {
    char id[32];
    char pr[32];
    char conclusion[32];
    char url[256];
    char job[160];
    char errors[max_errors][line_size];
    int error_count;
};

static struct Run runs[window];
static int run_count;
static char held[max_held][line_size];
static int held_count;

static void copy(char *to, size_t size, const char *from) {
    snprintf(to, size, "%s", from);
}

// Splits `line` at tabs into at most `max` fields (modified in place); returns how many.
static int split(char *line, char **fields, int max) {
    int n = 0;
    fields[n++] = line;
    for (char *c = line; *c != '\0' && n < max; c++) {
        if (*c == '\t') {
            *c = '\0';
            fields[n++] = c + 1;
        }
    }
    return n;
}

static void take_run(char **f, int n) {
    if (n < 5) {
        return;
    }
    if (run_count >= window) {
        run_count++; // older than the window: counted so err lines stop attaching, then clamped in main
        return;
    }
    struct Run *r = &runs[run_count++];
    copy(r->id, sizeof r->id, f[1]);
    copy(r->pr, sizeof r->pr, f[2]);
    copy(r->conclusion, sizeof r->conclusion, f[3]);
    copy(r->url, sizeof r->url, f[4]);
    copy(r->job, sizeof r->job, n > 5 ? f[5] : "");
}

static void take_line(char *line) {
    char *f[6];
    int n = split(line, f, 6);
    if (strcmp(f[0], "run") == 0) {
        take_run(f, n);
    } else if (strcmp(f[0], "err") == 0 && n > 1 && run_count > 0 && run_count <= window) {
        struct Run *r = &runs[run_count - 1];
        if (r->error_count < max_errors) {
            copy(r->errors[r->error_count++], line_size, f[1]);
        }
    } else if (strcmp(f[0], "held") == 0 && n > 1 && held_count < max_held) {
        copy(held[held_count++], line_size, f[1]);
    }
}

static void write_body(void) {
    printf("The merge queue merged nothing in its last %d runs. This issue is opened and edited by "
           "`.github/workflows/queue-stall.yml` and closed by it when a queue run ends `success`; it only reports.\n",
           window);
    for (int i = 0; i < window; i++) {
        const struct Run *r = &runs[i];
        printf("\n### %d. #%s: %s\n\n- run: %s\n- conclusion: `%s`\n- first failing job: %s\n", i + 1, r->pr, r->id,
               r->url, r->conclusion,
               r->job[0] != '\0' ? r->job : "none (the run was cancelled or never started a job)");
        for (int e = 0; e < r->error_count; e++) {
            printf("- `%s`\n", r->errors[e]);
        }
    }
    if (held_count > 0) {
        printf("\n### Gates held in report-only\n\n");
        for (int h = 0; h < held_count; h++) {
            printf("- %s\n", held[h]);
        }
    }
}

int main(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: queue_stall < RUNS\n");
        return 2;
    }
    char line[line_size];
    while (fgets(line, sizeof line, stdin) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') {
            take_line(line);
        }
    }
    if (run_count > window) {
        run_count = window;
    }
    if (run_count > 0 && strcmp(runs[0].conclusion, "success") == 0) {
        puts("green");
        return 0;
    }
    int successes = 0;
    for (int i = 0; i < run_count; i++) {
        successes += strcmp(runs[i].conclusion, "success") == 0 ? 1 : 0;
    }
    if (run_count < window || successes > 0) {
        puts("quiet");
        return 0;
    }
    puts("stalled");
    write_body();
    return 0;
}
