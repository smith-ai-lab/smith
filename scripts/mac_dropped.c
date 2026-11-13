// Starts again a hosted macOS job that GitHub dropped without ever giving it a runner (#3589). GitHub ends such a job
// `cancelled` after 15 minutes in its queue (0 steps, empty runner name, 901 s from creation to end; #3568), and the
// push run on main then ends `failure` though no step of ours ran. The workflow queue-heal.yml reads the jobs of the
// newest push runs with `gh api` and calls this tool for each; the tool never touches the network, so a test can replay
// the jobs. A job that ran and failed, or was cancelled after it got a runner, is never a drop.
//
// Usage: mac_dropped < RECORDS
//
// RECORDS is tab separated, one record a line:
//   run  ID  ATTEMPT  NEWER              (the run; NEWER is 1 when a newer push run of main exists, else 0)
//   job  ID  NAME  CONCLUSION  RUNNER  STEPS  WAIT   (a job; RUNNER may be empty; WAIT = seconds from created to ended)
//
// Prints a verdict on the first line and exits 0:
//   rerun  every job that did not succeed is a drop (or waits for one: warm, publish, ci); re-run the failed jobs
//   bound  the same, but the run was already started again 3 times (ATTEMPT 4): it stays red, which is then true
//   none   nothing was dropped, or a job of the run really failed or was cancelled by us: do nothing
// then one line `drop  JOB_ID  NAME  WAIT` per dropped job. Exits 2 on a usage error. Plain C23 and libc.
//
// A drop: conclusion `cancelled`, no runner, 0 steps and a wait of 900 to 960 s. A `warm` replaced by a newer push
// (its concurrency group, #3069) is cancelled the same way, after any wait, so a `warm` counts only while no newer push
// run exists.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int max_jobs = 64;
constexpr int max_restarts = 3;
constexpr int drop_least = 900;
constexpr int drop_most = 960;

struct Job {
    char id[32];
    char name[160];
    int wait;
    bool drop;
    bool other;
    bool waits;
};

static struct Job jobs[max_jobs];
static int job_count;
static int attempt = 1;
static int newer;

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

static bool starts(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

// A job that waits for a macOS job of the run: its failure may only be the drop's consequence.
static bool waits_for_mac(const char *name) {
    return starts(name, "warm") || strcmp(name, "publish") == 0 || strcmp(name, "ci") == 0;
}

static void take_job(char **f, int n) {
    if (n < 7 || job_count >= max_jobs) {
        return;
    }
    const char *conclusion = f[3];
    bool bad = strcmp(conclusion, "failure") == 0 || strcmp(conclusion, "cancelled") == 0;
    if (!bad) {
        return;
    }
    struct Job *j = &jobs[job_count++];
    snprintf(j->id, sizeof j->id, "%s", f[1]);
    snprintf(j->name, sizeof j->name, "%s", f[2]);
    j->wait = atoi(f[6]);
    bool replaced = starts(f[2], "warm") && newer != 0;
    j->drop = strcmp(conclusion, "cancelled") == 0 && f[4][0] == '\0' && atoi(f[5]) == 0 && j->wait >= drop_least &&
              j->wait <= drop_most && !replaced;
    j->waits = !j->drop && waits_for_mac(j->name);
    j->other = !j->drop && !j->waits;
}

static const char *verdict(void) {
    int drops = 0;
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].other) {
            return "none";
        }
        drops += jobs[i].drop ? 1 : 0;
    }
    if (drops == 0) {
        return "none";
    }
    return attempt - 1 >= max_restarts ? "bound" : "rerun";
}

int main(void) {
    char line[1024];
    while (fgets(line, sizeof line, stdin) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        char *f[8];
        int n = split(line, f, 8);
        if (strcmp(f[0], "run") == 0 && n >= 4) {
            attempt = atoi(f[2]);
            newer = atoi(f[3]);
        } else if (strcmp(f[0], "job") == 0) {
            take_job(f, n);
        } else {
            fprintf(stderr, "mac_dropped: unknown record '%s'\n", f[0]);
            return 2;
        }
    }
    const char *v = verdict();
    printf("%s\n", v);
    if (strcmp(v, "none") != 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].drop) {
                printf("drop\t%s\t%s\t%d\n", jobs[i].id, jobs[i].name, jobs[i].wait);
            }
        }
    }
    return 0;
}
