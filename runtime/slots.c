// Slots (issue #360): a host-wide limit on heavy jobs, so that local builds, checks and tests cannot starve CI. `smith
// build`, `smith check` and `smith test` take a slot before they start; a slot is a file under a folder of the host
// that smith holds an flock on, so the OS drops it when the process dies (no pid files, no daemon, no stale lock).
// knowledge/slots.md. Included by smith.c.
constexpr int smith_slots_max = 64;
constexpr long smith_slots_poll_ms = 200;
constexpr long smith_slots_report_ms = 30'000;
constexpr long smith_slots_wait_max_ms = 4L * 60 * 60 * 1000; // a backstop: after it the job runs without a slot
constexpr size_t smith_slots_text_max = 1024;

static int smith_slots_fd = -1; // the slot this process holds; closed by the OS when the process ends

// The number of slots: SMITH_SLOTS, else a quarter of the cores (at least 1). 0 turns the limit off.
static int smith_slots_count(void) {
    const char *text = getenv("SMITH_SLOTS");
    if (text != nullptr && text[0] != '\0') {
        char *end = nullptr;
        errno = 0;
        long value = strtol(text, &end, 10);
        if (*end != '\0' || errno != 0 || value < 0) smith_die(smith_exit_usage, "SMITH_SLOTS needs a number", text);
        return value > smith_slots_max ? smith_slots_max : (int)value;
    }
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores < 8 ? 1 : (int)(cores / 4 > smith_slots_max ? smith_slots_max : cores / 4);
}

// The folder of the slots: SMITH_SLOTS_DIR, else one per user in /tmp (a launchd service and a shell have different
// $TMPDIR, and the slots are the host's).
static void smith_slots_folder(char *out, size_t size) {
    const char *given = getenv("SMITH_SLOTS_DIR");
    int written = given != nullptr && given[0] != '\0' ? snprintf(out, size, "%s", given)
                                                       : snprintf(out, size, "/tmp/smith-slots-%u", (unsigned)getuid());
    SMITH_ASSERT(written > 0 && (size_t)written < size);
    if (mkdir(out, 0700) != 0 && errno != EEXIST) smith_die(smith_exit_missing, "cannot make the slots folder", out);
}

// Whether the process that holds the slot we inherited is still running. A child of a smith that holds a slot has
// SMITH_SLOT_HELD=<pid of the holder> and takes none; a stale value (the holder is gone) is ignored.
static bool smith_slots_inherited(void) {
    const char *text = getenv("SMITH_SLOT_HELD");
    if (text == nullptr || text[0] == '\0') return false;
    char *end = nullptr;
    long pid = strtol(text, &end, 10);
    if (*end != '\0' || pid <= 0) return false;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

// Whether a CI job is waiting for a slot: it holds a shared lock on `ci-waiting`, so an exclusive one fails.
static bool smith_slots_ci_waiting(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/ci-waiting", folder);
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool waiting = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd); // this drops a lock we got, too
    return waiting;
}

// Tries each slot once. On success the lock is held in smith_slots_fd and the label is written in the file. Otherwise
// `holders` lists the labels of the slots taken and `*running` counts them.
static bool smith_slots_try(const char *folder, int count, const char *label, char *holders, int *running) {
    holders[0] = '\0';
    *running = 0;
    for (int i = 0; i < count; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/slot-%d", folder, i);
        int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) continue;
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            if (ftruncate(fd, 0) == 0) (void)!pwrite(fd, label, strlen(label), 0);
            smith_slots_fd = fd;
            return true;
        }
        char line[256];
        ssize_t got = pread(fd, line, sizeof line - 1, 0);
        line[got > 0 ? got : 0] = '\0';
        size_t used = strlen(holders);
        snprintf(holders + used, smith_slots_text_max - used, "%s%s", *running > 0 ? "; " : "", line);
        *running += 1;
        close(fd);
    }
    return false;
}

static void smith_slots_sleep(long milliseconds) {
    struct timespec pause = {.tv_sec = milliseconds / 1000, .tv_nsec = (milliseconds % 1000) * 1'000'000L};
    nanosleep(&pause, nullptr);
}

// Waits for a slot, telling why on stderr at the start and every 30 s. A CI job (SMITH_PRIORITY=ci) marks itself as
// waiting, and until it has a slot the other jobs do not take one.
static bool smith_slots_wait(const char *folder, int count, const char *label, bool ci) {
    char holders[smith_slots_text_max];
    int running = 0;
    int mark = -1;
    if (ci) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/ci-waiting", folder);
        mark = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (mark >= 0) flock(mark, LOCK_SH);
    }
    bool got = false;
    bool behind_ci = false;
    for (long waited = 0; waited < smith_slots_wait_max_ms; waited += smith_slots_poll_ms) { // bounded by the backstop
        behind_ci = !ci && smith_slots_ci_waiting(folder);
        if (!behind_ci && smith_slots_try(folder, count, label, holders, &running)) {
            got = true;
            break;
        }
        if (waited % smith_slots_report_ms == 0)
            fprintf(stderr, "smith: waiting for a slot (%s%d jobs running: %s)\n",
                    behind_ci ? "a CI job is waiting; " : "", running, holders);
        smith_slots_sleep(smith_slots_poll_ms);
    }
    if (mark >= 0) close(mark);
    return got;
}

// Takes a slot for this process unless it has one from its parent (a smith that runs smith) or the limit is off.
// `label` says who holds it ("pid verb in folder"). The children inherit SMITH_SLOT_HELD and take none.
static void smith_slots_take(const char *verb) {
    int count = smith_slots_count();
    if (count == 0 || smith_slots_inherited()) return;
    char folder[PATH_MAX];
    smith_slots_folder(folder, sizeof folder);
    char where[PATH_MAX];
    if (getcwd(where, sizeof where) == nullptr) where[0] = '\0';
    char label[256];
    snprintf(label, sizeof label, "%ld smith %s in %s", (long)getpid(), verb, where);
    const char *priority = getenv("SMITH_PRIORITY");
    bool ci = priority != nullptr && strcmp(priority, "ci") == 0;
    if (!smith_slots_wait(folder, count, label, ci)) {
        fprintf(stderr, "smith: no slot after %ld hours; running without one\n", smith_slots_wait_max_ms / 3'600'000);
        return;
    }
    char held[32];
    snprintf(held, sizeof held, "%ld", (long)getpid());
    setenv("SMITH_SLOT_HELD", held, 1);
}

// The verbs that do heavy work.
static bool smith_slots_verb(const char *name) {
    return strcmp(name, "build") == 0 || strcmp(name, "check") == 0 || strcmp(name, "test") == 0;
}

// `smith --smith-slots <program> [arguments]`: takes a slot as a heavy verb does, runs the program and exits with its
// status. The tests of the slots use it.
static int smith_slots_main(int count, char **args) {
    if (count < 1) smith_die(smith_exit_usage, "--smith-slots needs a program", nullptr);
    smith_slots_take("run");
    pid_t pid = 0;
    if (posix_spawnp(&pid, args[0], nullptr, nullptr, args, environ) != 0) return smith_exit_missing;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) // bounded: a signal ends the wait once
        ;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
