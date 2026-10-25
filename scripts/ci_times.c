// `smith build ci-times` (issue #360): where CI's time goes. It reads a folder with one subfolder per CI run (named by the ids
// on its command line), each holding what `smith build ci-times` downloaded: `smith-log/test.log` and `check.log` from the
// `check-log` artifact, and `steps.tsv`, the job's steps in seconds (name, tab, seconds). It prints, for the runs found:
// the job's steps, the stages of `smith check`, the test tasks by median wall and CPU, the slowest tests the runner
// listed, and the 2-way split of the tasks by CPU time. Plain C23, libc only, no allocation: the tables are static.
//
// Usage: ci_times FOLDER RUN_ID...
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int max_runs = 64;
constexpr int max_series = 160;
constexpr int name_size = 128;

typedef struct {
    char name[name_size];
    double wall[max_runs];
    double cpu[max_runs];
    int count;
} Series;

typedef struct {
    Series items[max_series];
    int count;
} Table;

static Table steps;
static Table stages;
static Table tasks;
static Table tests;
static int runs_read;
static int runs_without_log;

static Series *series_of(Table *table, const char *name) {
    for (int i = 0; i < table->count; i++)
        if (strcmp(table->items[i].name, name) == 0) return &table->items[i];
    if (table->count == max_series) return nullptr;
    Series *series = &table->items[table->count++];
    snprintf(series->name, sizeof series->name, "%s", name);
    series->count = 0;
    return series;
}

static void add(Table *table, const char *name, double wall, double cpu) {
    Series *series = series_of(table, name);
    if (series == nullptr || series->count == max_runs) return;
    series->wall[series->count] = wall;
    series->cpu[series->count] = cpu;
    series->count++;
}

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

// The median and maximum of the first `count` values; the array is sorted in place.
static double median(double *values, int count) {
    if (count == 0) return 0;
    qsort(values, (size_t)count, sizeof values[0], compare_double);
    return count % 2 == 1 ? values[count / 2] : (values[count / 2 - 1] + values[count / 2]) / 2;
}

static double maximum(const double *values, int count) {
    double best = 0;
    for (int i = 0; i < count; i++)
        best = values[i] > best ? values[i] : best;
    return best;
}

static double wall_median(Series *series) {
    return median(series->wall, series->count);
}

static double cpu_median(Series *series) {
    return median(series->cpu, series->count);
}

static void read_steps(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    char line[512];
    while (fgets(line, sizeof line, file) != nullptr) {
        char *tab = strchr(line, '\t');
        if (tab == nullptr) continue;
        *tab = '\0';
        add(&steps, line, atof(tab + 1), 0);
    }
    fclose(file);
}

// A line of check.log: `smith check: test ok: 57 tasks, 2616 passed (276.7 s wall, 1179.5 s cpu)`.
static void read_stage(const char *line) {
    if (strncmp(line, "smith check: ", 13) != 0) return;
    char name[name_size];
    double wall = 0;
    double cpu = 0;
    const char *times = strchr(line, '(');
    if (sscanf(line + 13, "%127[a-z]", name) != 1 || times == nullptr) return;
    if (sscanf(times, "(%lf s wall, %lf s cpu)", &wall, &cpu) == 2) add(&stages, name, wall, cpu);
}

static void read_check_log(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    char line[1024];
    while (fgets(line, sizeof line, file) != nullptr)
        read_stage(line);
    fclose(file);
}

// The task table `smith test` prints (task, wall, cpu, system, smiths), up to its `all tasks` row.
static void read_task_row(const char *line) {
    char name[name_size];
    double wall = 0;
    double cpu = 0;
    double system = 0;
    int smiths = 0;
    if (sscanf(line, " %127s %lf %lf %lf %d", name, &wall, &cpu, &system, &smiths) == 5) add(&tasks, name, wall, cpu);
}

// `     97.56 s  smith::inflate_test::prop_inflate_round_trips_stored_blocks`
static void read_slowest_row(const char *line) {
    char name[name_size];
    double seconds = 0;
    if (sscanf(line, " %lf s %127s", &seconds, name) == 2) add(&tests, name, seconds, seconds);
}

static void read_test_log(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    char line[1024];
    enum { Other, Table, Slowest } mode = Other;
    while (fgets(line, sizeof line, file) != nullptr) {
        if (strstr(line, "the tasks by wall time") != nullptr) {
            mode = Table;
        } else if (strncmp(line, "slowest:", 8) == 0) {
            mode = Slowest;
        } else if (mode == Table && strstr(line, "all tasks") != nullptr) {
            mode = Other;
        } else if (mode == Table && strstr(line, "task ") == nullptr) {
            read_task_row(line);
        } else if (mode == Slowest && line[0] == '\n') {
            mode = Other;
        } else if (mode == Slowest) {
            read_slowest_row(line);
        }
    }
    fclose(file);
}

static void read_run(const char *folder, const char *id) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s/steps.tsv", folder, id);
    read_steps(path);
    snprintf(path, sizeof path, "%s/%s/smith-log/test.log", folder, id);
    FILE *probe = fopen(path, "r");
    if (probe == nullptr) {
        runs_without_log++;
        return;
    }
    fclose(probe);
    runs_read++;
    read_test_log(path);
    snprintf(path, sizeof path, "%s/%s/check.log", folder, id);
    read_check_log(path);
}

// Sorting the series of a table by the median of one of its two columns, largest first.
static int sort_column;

static int compare_series(const void *a, const void *b) {
    Series *x = (Series *)a;
    Series *y = (Series *)b;
    double mx = sort_column == 0 ? wall_median(x) : cpu_median(x);
    double my = sort_column == 0 ? wall_median(y) : cpu_median(y);
    return (mx < my) - (mx > my);
}

static void print_table(const char *title, Table *table, int by_column, int limit, bool show_cpu) {
    sort_column = by_column;
    qsort(table->items, (size_t)table->count, sizeof table->items[0], compare_series);
    printf("\n%s\n  %-70s %5s %9s %9s", title, "name", "runs", "wall med", "wall max");
    printf(show_cpu ? " %9s %9s\n" : "\n", "cpu med", "cpu max");
    double wall_total = 0;
    double cpu_total = 0;
    for (int i = 0; i < table->count; i++) {
        wall_total += wall_median(&table->items[i]);
        cpu_total += cpu_median(&table->items[i]);
    }
    for (int i = 0; i < table->count && i < limit; i++) {
        Series *s = &table->items[i];
        printf("  %-70.70s %5d %8.1fs %8.1fs", s->name, s->count, wall_median(s), maximum(s->wall, s->count));
        if (show_cpu) printf(" %8.1fs %8.1fs", cpu_median(s), maximum(s->cpu, s->count));
        printf("\n");
    }
    printf("  sum of medians: wall %.1f s%s", wall_total, show_cpu ? ", cpu " : "\n");
    if (show_cpu) printf("%.1f s\n", cpu_total);
}

// Each stage's share of the sum of the stages' median wall times.
static void print_shares(void) {
    double total = 0;
    for (int i = 0; i < stages.count; i++)
        total += wall_median(&stages.items[i]);
    printf("\nShare of `smith check` by stage (median wall)\n");
    for (int i = 0; i < stages.count; i++)
        printf("  %-8s %6.1f s  %5.1f%%\n", stages.items[i].name, wall_median(&stages.items[i]),
               total > 0 ? 100 * wall_median(&stages.items[i]) / total : 0);
}

// Longest processing time first: each task, largest CPU median first, to the shard with the smaller total. The
// check's tasks all start at once, so the longest single task bounds a shard; the report shows both.
static void print_shards(void) {
    sort_column = 1;
    qsort(tasks.items, (size_t)tasks.count, sizeof tasks.items[0], compare_series);
    constexpr int shard_count = 2;
    double cpu[shard_count] = {0, 0};
    double wall[shard_count] = {0, 0};
    int members[shard_count] = {0, 0};
    double longest[shard_count] = {0, 0};
    int in_split = 0;
    for (int i = 0; i < tasks.count; i++) {
        if (tasks.items[i].count * 2 < runs_read) continue; // a task that is gone, or new: not in most runs
        in_split++;
        int to = cpu[0] <= cpu[1] ? 0 : 1;
        double task_cpu = cpu_median(&tasks.items[i]);
        double task_wall = wall_median(&tasks.items[i]);
        cpu[to] += task_cpu;
        wall[to] += task_wall;
        members[to]++;
        longest[to] = task_wall > longest[to] ? task_wall : longest[to];
    }
    printf("\nA 2-way split of the test tasks that ran in at least half of the runs, balanced by median CPU time\n");
    printf("  (%d tasks of %d seen; tasks are the unit, so one task such as test-units stays whole)\n", in_split,
           tasks.count);
    for (int s = 0; s < shard_count; s++)
        printf("  shard %d/2: %2d tasks, cpu %7.1f s, sum of wall %7.1f s, longest task %6.1f s\n", s + 1, members[s],
               cpu[s], wall[s], longest[s]);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: ci_times FOLDER RUN_ID...\n", stderr);
        return 2;
    }
    for (int i = 2; i < argc; i++)
        read_run(argv[1], argv[i]);
    printf("ci-times: %d runs with a check log, %d without (the tree was checked already, or the log expired)\n",
           runs_read, runs_without_log);
    if (runs_read == 0) return 1;
    print_table("Job steps (seconds; `queued` is the wait for a runner)", &steps, 0, 20, false);
    print_table("Stages of `smith check`", &stages, 0, 10, true);
    print_shares();
    print_table("Test tasks (all start at once; the longest sets the stage's wall time)", &tasks, 0, 25, true);
    print_table("Slowest tests the runner listed (its five slowest per run, so a test appears only when it was one)",
                &tests, 0, 12, false);
    print_shards();
    return 0;
}
