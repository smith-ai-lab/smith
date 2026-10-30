// `smith build ci-times` (issue #360): where CI's time goes. It reads a folder with one subfolder per CI run (named by
// the ids on its command line), each holding what `smith build ci-times` downloaded from ci.yml's run: `steps.tsv`, the
// steps of every job in seconds (`<job> / <step>`, tab, seconds; `queued` is the wait for a runner, `job` the whole
// job), and the `test-log-*` artifacts, one folder for each part of the test matrix, each with the `test.log` of
// `smith test` (its `cpu<TAB>seconds<TAB>name` lines when the run set SMITH_TEST_STATS=1). It prints, for the runs
// found: the steps of the jobs, the CPU seconds of each part of the test matrix (how balanced the split is), and the
// tests of 2 CPU seconds or more by median. Plain C23, libc only, no allocation: the tables are static.
//
// Usage: ci_times FOLDER RUN_ID...
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int max_runs = 64;
constexpr int max_series = 400;
constexpr int name_size = 160;
constexpr double test_floor = 2.0; // a test under this many CPU seconds is not listed

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
static Table parts;
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

// The number at the start of *text, after spaces; false when there is none. *text moves past it.
static bool take_number(const char **text, double *value) {
    char *end = nullptr;
    *value = strtod(*text, &end);
    if (end == *text) return false;
    *text = end;
    return true;
}

// Skips spaces and one word, copying the word to name when it is not null.
static const char *take_word(const char *text, char *name) {
    while (*text == ' ')
        text++;
    size_t length = strcspn(text, " \n");
    if (name != nullptr) {
        length = length < name_size - 1 ? length : name_size - 1;
        memcpy(name, text, length);
        name[length] = '\0';
    }
    return text + length;
}
static void read_steps(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    char line[512];
    while (fgets(line, sizeof line, file) != nullptr) {
        char *tab = strchr(line, '\t');
        if (tab == nullptr) continue;
        *tab = '\0';
        const char *seconds = tab + 1;
        double value = 0;
        if (take_number(&seconds, &value)) add(&steps, line, value, 0);
    }
    fclose(file);
}

// The stats of one part's test.log: a line `cpu<TAB>seconds<TAB>name` for each test. Returns the CPU seconds of the
// whole part, or a negative number when the log has none (the run did not set SMITH_TEST_STATS).
static double read_test_log(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) return -1;
    char line[1024];
    double total = -1;
    while (fgets(line, sizeof line, file) != nullptr) {
        if (strncmp(line, "cpu\t", 4) != 0) continue;
        const char *rest = line + 4;
        double seconds = 0;
        if (!take_number(&rest, &seconds) || *rest != '\t') continue;
        total = (total < 0 ? 0 : total) + seconds;
        rest++;
        char name[name_size];
        take_word(rest, name);
        if (seconds >= test_floor && name[0] != '\0') add(&tests, name, seconds, seconds);
    }
    fclose(file);
    return total;
}

// A run: its steps, and each `test-log-*` folder of its artifacts as one part of the matrix.
static void read_run(const char *folder, const char *id) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s/steps.tsv", folder, id);
    read_steps(path);
    snprintf(path, sizeof path, "%s/%s", folder, id);
    DIR *run = opendir(path);
    bool found = false;
    for (struct dirent *entry = run != nullptr ? readdir(run) : nullptr; entry != nullptr; entry = readdir(run)) {
        if (strncmp(entry->d_name, "test-log-", 9) != 0) continue;
        char log[PATH_MAX];
        snprintf(log, sizeof log, "%s/%s/%s/test.log", folder, id, entry->d_name);
        double total = read_test_log(log);
        if (total < 0) continue;
        add(&parts, entry->d_name, total, total);
        found = true;
    }
    if (run != nullptr) closedir(run);
    if (found)
        runs_read++;
    else
        runs_without_log++;
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

int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: ci_times FOLDER RUN_ID...\n", stderr);
        return 2;
    }
    for (int i = 2; i < argc; i++)
        read_run(argv[1], argv[i]);
    printf(
        "ci-times: %d runs with test logs, %d without (no tests ran, the logs expired or SMITH_TEST_STATS was off)\n",
        runs_read, runs_without_log);
    if (steps.count == 0 && runs_read == 0) return 1;
    print_table("Job steps (seconds; `queued` is the wait for a runner, `job` the whole job)", &steps, 0, 25, false);
    print_table("CPU seconds of each part of the test matrix (a balanced split has equal parts)", &parts, 1, 10, true);
    print_table("Tests of 2 CPU seconds or more (what .github/shard-costs.tsv is made from)", &tests, 1, 25, true);
    return 0;
}
