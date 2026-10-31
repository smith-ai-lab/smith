// What a run costs (issue #31): the wall and CPU time of each task of a build and how many times smith started under
// it, so that the slow ones are seen, and what to port or shard is chosen by numbers. Included by smith.c.
//
// `smith test` (and so `smith check`) names a file in SMITH_ACCOUNT_FILE; the executor then runs each task (an edge
// that makes `.smith/task/<name>`) through `smith --smith-account <file> <name> <command...>`, which runs the command,
// waits for it with wait4 (the CPU time is that of the command and everything it ran and waited for), and appends
// one line to the file: name, wall, user and system milliseconds, and the smith starts. A smith start is any smith
// process (a compile, a link, a verb, a test's own) that began while SMITH_START_LOG named a file: smith appends a
// byte to it as main begins, so a count needs no parent to know the child. A test that clears the environment is not
// counted, and neither is a start of a smith that is older than this one.
constexpr size_t smith_account_tasks_max = 1024;
constexpr size_t smith_account_summary = 8;
constexpr uint64_t smith_account_ms_per_s = 1000;

// Called as main begins: one more smith start, when a start log is named.
static void smith_account_start(void) {
    const char *path = getenv("SMITH_START_LOG");
    if (path == nullptr || *path == '\0') return;
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    (void)!write(fd, "x", 1);
    close(fd);
}

static uint64_t smith_account_ms(struct timeval time) {
    return (uint64_t)time.tv_sec * smith_account_ms_per_s + (uint64_t)time.tv_usec / smith_account_ms_per_s;
}

static uint64_t smith_account_now_ms(void) {
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * smith_account_ms_per_s + (uint64_t)now.tv_nsec / 1'000'000;
}

// `smith --smith-account <file> <name> <program> [arguments]`: runs the program and appends its line to the file.
static int smith_account_main(int count, char **args) {
    if (count < 3)
        smith_die(smith_exit_usage, "usage: smith --smith-account <file> <name> <program> [arguments]", nullptr);
    char starts[PATH_MAX];
    snprintf(starts, sizeof starts, "%s.%d.starts", args[0], (int)getpid());
    (void)unlink(starts);
    setenv("SMITH_START_LOG", starts, 1);
    unsetenv("SMITH_ACCOUNT_FILE"); // a build inside the command is not this run's task
    uint64_t begin = smith_account_now_ms();
    pid_t pid = 0;
    if (posix_spawn(&pid, args[2], nullptr, nullptr, args + 2, environ) != 0)
        smith_die(smith_exit_missing, "cannot execute", args[2]);
    int wait_status = 0;
    struct rusage usage = {};
    pid_t waited = -1;
    for (int i = 0; i < 1000 && waited < 0; i++) // bounded; only a signal interrupts the wait
        waited = wait4(pid, &wait_status, 0, &usage);
    uint64_t wall = smith_account_now_ms() - begin;
    struct stat info = {};
    uint64_t smiths = stat(starts, &info) == 0 ? (uint64_t)info.st_size : 0;
    (void)unlink(starts);
    char line[PATH_MAX];
    int length = snprintf(line, sizeof line, "%s\t%llu\t%llu\t%llu\t%llu\n", args[1], (unsigned long long)wall,
                          (unsigned long long)smith_account_ms(usage.ru_utime),
                          (unsigned long long)smith_account_ms(usage.ru_stime), (unsigned long long)smiths);
    int fd = open(args[0], O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd >= 0 && length > 0 && (size_t)length < sizeof line) (void)!write(fd, line, (size_t)length);
    if (fd >= 0) close(fd);
    if (waited < 0) return smith_exit_missing;
    return WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128 + WTERMSIG(wait_status);
}

typedef struct {
    char name[96];
    uint64_t wall;
    uint64_t user;
    uint64_t system;
    uint64_t smiths;
} SmithAccountTask;

static SmithAccountTask smith_account_tasks[smith_account_tasks_max];

static int smith_account_slower(const void *left, const void *right) {
    const SmithAccountTask *a = left;
    const SmithAccountTask *b = right;
    return a->wall < b->wall ? 1 : a->wall > b->wall ? -1 : 0;
}

static size_t smith_account_read(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == nullptr) return 0;
    size_t count = 0;
    char line[512];
    for (size_t guard = 0; guard < smith_account_tasks_max * 2 + 2 && fgets(line, sizeof line, file) != nullptr;
         guard++) {
        if (count >= smith_account_tasks_max) {
            fprintf(stderr,
                    "smith: %s has more than %zu tasks (the limit), so the slowest list and the totals would be wrong; "
                    "raise smith_account_tasks_max in runtime/account.c\n",
                    path, smith_account_tasks_max);
            exit(smith_exit_usage);
        }
        SmithAccountTask *task = &smith_account_tasks[count];
        char *cursor = strchr(line, '\t');
        if (cursor == nullptr || (size_t)(cursor - line) >= sizeof task->name) continue;
        memcpy(task->name, line, (size_t)(cursor - line));
        task->name[cursor - line] = '\0';
        uint64_t *fields[] = {&task->wall, &task->user, &task->system, &task->smiths};
        for (size_t i = 0; i < 4; i++) { // the four numbers, tab separated
            *fields[i] = strtoull(cursor + 1, &cursor, 10);
        }
        count++;
    }
    fclose(file);
    return count;
}

// Prints the tasks of the file, the slowest first (all of them, or the slowest few), and the totals. `what` names the
// verb. The CPU time of tasks that ran side by side adds up to more than the wall time of the run, which is the point.
static void smith_account_report(const char *path, const char *what, bool all) {
    size_t count = smith_account_read(path);
    if (count == 0) return;
    qsort(smith_account_tasks, count, sizeof smith_account_tasks[0], smith_account_slower);
    uint64_t user = 0, system = 0, smiths = 0;
    for (size_t i = 0; i < count; i++) {
        user += smith_account_tasks[i].user;
        system += smith_account_tasks[i].system;
        smiths += smith_account_tasks[i].smiths;
    }
    size_t shown = all || count < smith_account_summary ? count : smith_account_summary;
    fprintf(stderr, "%s: %zu tasks, the %s by wall time (seconds; cpu is user + system, smiths the smith processes):\n",
            what, count, shown == count ? "tasks" : "slowest");
    fprintf(stderr, "  %-28s %8s %8s %8s %8s\n", "task", "wall", "cpu", "system", "smiths");
    for (size_t i = 0; i < shown; i++) {
        const SmithAccountTask *task = &smith_account_tasks[i];
        fprintf(stderr, "  %-28s %8.1f %8.1f %8.1f %8llu\n", task->name, (double)task->wall / 1000.0,
                (double)(task->user + task->system) / 1000.0, (double)task->system / 1000.0,
                (unsigned long long)task->smiths);
    }
    fprintf(stderr, "  %-28s %8s %8.1f %8.1f %8llu\n", "all tasks", "", (double)(user + system) / 1000.0,
            (double)system / 1000.0, (unsigned long long)smiths);
}
