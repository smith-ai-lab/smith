// Slots (issue #360): a host-wide limit on heavy jobs, so that local builds, checks and tests cannot starve CI. `smith
// build`, `smith check` and `smith test` take a slot before they start; a slot is a file under a folder of the host
// that smith holds an flock on, so the OS drops it when the process dies (no pid files, no daemon, no stale lock).
// knowledge/slots.md. Included by smith.c.
constexpr int smith_slots_max = 64;
constexpr long smith_slots_poll_ms = 200;
constexpr long smith_slots_report_ms = 30'000;
constexpr long smith_slots_wait_max_ms = 4L * 60 * 60 * 1000; // a backstop: after it the job runs without a slot
constexpr size_t smith_slots_text_max = 1024;

static int smith_slots_fd = -1;        // the slot this process holds; closed by the OS when the process ends
static bool smith_slots_ci;            // this process runs for CI (SMITH_PRIORITY=ci)
static char smith_slots_dir[PATH_MAX]; // the slots folder, once a slot is held

// The cores smith may use: SMITH_CORES (a holder sets it for its children; a test sets it as a fake host), else the
// machine's. At least 1.
static int smith_slots_cores(void) {
    const char *text = getenv("SMITH_CORES");
    long cores = text != nullptr && text[0] != '\0' ? strtol(text, nullptr, 10) : 0;
    if (cores <= 0) cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores < 1 ? 1 : cores > INT_MAX ? INT_MAX : (int)cores;
}

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

// Whether a CI job is waiting for a slot: it holds a shared lock on `ci-waiting`, so an exclusive one fails. This is
// not atomic: a job that asks just before a CI job starts to wait may still take the free slot (the race), and the CI
// job gets the next one. A lock held through the whole take would make every local job queue behind each other.
static bool smith_slots_ci_waiting(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/ci-waiting", folder);
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool waiting = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd); // this drops a lock we got, too
    return waiting;
}

// What the system says about a process: whether it exists, when it started and its parent. `start` and `parent` are 0
// where the system cannot say (then the checks that need them pass).
static bool smith_slots_proc(pid_t pid, long *start, long *parent) {
    *start = 0;
    *parent = 0;
#ifdef __APPLE__
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
    struct kinfo_proc info = {};
    size_t size = sizeof info;
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0) return false;
    *start = (long)info.kp_proc.p_starttime.tv_sec * 1'000'000L + (long)info.kp_proc.p_starttime.tv_usec;
    *parent = (long)info.kp_eproc.e_ppid;
    return true;
#else
    return kill(pid, 0) == 0 || errno == EPERM;
#endif
}

// The first line of a slot file: "<pid> <parent pid> <start time>", written by the holder when it takes the slot.
static size_t smith_slots_meta(char *out, size_t size) {
    long start = 0;
    long parent = 0;
    (void)smith_slots_proc(getpid(), &start, &parent);
    int written = snprintf(out, size, "%ld %ld %ld\n", (long)getpid(), (long)getppid(), start);
    SMITH_ASSERT(written > 0 && (size_t)written < size);
    return (size_t)written;
}

// Whether the holder a slot file names no longer deserves its slot: its process is gone (the lock outlived it in a
// child), the pid was recycled (another start time), or its parent is gone (it is reparented, an orphan: its parent is
// now pid 1, or not the one it had). A file whose first line cannot be read is never stale.
static bool smith_slots_stale(const char *text) {
    char *end = nullptr;
    long pid = strtol(text, &end, 10);
    long parent = strtol(end, &end, 10);
    long start = strtol(end, &end, 10);
    if (pid <= 0 || *end != '\n') return false;
    long now_start = 0;
    long now_parent = 0;
    if (!smith_slots_proc((pid_t)pid, &now_start, &now_parent)) return true;
    if (start != 0 && now_start != 0 && start != now_start) return true;
    return now_parent == 1 || (parent != 0 && now_parent != 0 && now_parent != parent); // 1: init or launchd took it
}

// Whether a CI job holds a slot: it holds a shared lock on `ci-held`. Looked at most once a second.
static bool smith_slots_ci_held(void) {
    static time_t checked;
    static bool held;
    time_t now = time(nullptr);
    if (smith_slots_dir[0] == '\0' || smith_slots_ci || now == checked) return held;
    checked = now;
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/ci-held", smith_slots_dir);
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return held;
    held = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd);
    return held;
}

// How many jobs a build may run at once, of the `jobs` it asked for: at most the cores SMITH_CORES allows (the share
// of a slot that a holder gave its children), and one while a CI job holds a slot and this one is not CI's. Asked
// before every job starts, so a CI job that appears mid-run lowers the rest of it.
static size_t smith_slots_limit(size_t jobs) {
    size_t cores = (size_t)smith_slots_cores();
    if (getenv("SMITH_CORES") != nullptr && cores < jobs) jobs = cores;
    if (smith_slots_fd >= 0 && smith_slots_ci_held()) jobs = 1;
    return jobs;
}

// Tries each slot once. On success the lock is held in smith_slots_fd and the label is written in the file. Otherwise
// `holders` lists the labels of the slots taken and `*running` counts them.
static bool smith_slots_try(const char *folder, int count, const char *label, char *holders, int *running) {
    holders[0] = '\0';
    *running = 0;
    for (int i = 0; i < count; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/slot-%d", folder, i);
        for (int again = 0; again < 2; again++) { // twice: a stale holder's file is replaced and tried once more
            int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (fd < 0) break;
            if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
                char meta[96];
                size_t length = smith_slots_meta(meta, sizeof meta);
                char text[512];
                snprintf(text, sizeof text, "%.*s%s", (int)length, meta, label);
                if (ftruncate(fd, 0) == 0) (void)!pwrite(fd, text, strlen(text), 0);
                smith_slots_fd = fd;
                return true;
            }
            char line[256];
            ssize_t got = pread(fd, line, sizeof line - 1, 0);
            line[got > 0 ? got : 0] = '\0';
            close(fd);
            if (again == 0 && smith_slots_stale(line)) {
                unlink(path); // the holder keeps a lock on a file nobody opens again: the slot is free
                continue;
            }
            const char *name = strchr(line, '\n');
            size_t used = strlen(holders);
            snprintf(holders + used, smith_slots_text_max - used, "%s%s", *running > 0 ? "; " : "",
                     name != nullptr ? name + 1 : line);
            *running += 1;
            break;
        }
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
        // The last slot is CI's: a local job tries only the first count - 1, so a CI job never waits behind local work.
        // With one slot nothing can be reserved: the job tries it, and the CI job's `ci-waiting` mark makes local jobs
        // take no slot while it waits.
        int usable = !ci && count > 1 ? count - 1 : count;
        if (!behind_ci && smith_slots_try(folder, usable, label, holders, &running)) {
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
    smith_slots_ci = ci;
    if (!smith_slots_wait(folder, count, label, ci)) {
        fprintf(stderr, "smith: no slot after %ld hours; running without one\n", smith_slots_wait_max_ms / 3'600'000);
        return;
    }
    char held[32];
    snprintf(held, sizeof held, "%ld", (long)getpid());
    setenv("SMITH_SLOT_HELD", held, 1);
    snprintf(smith_slots_dir, sizeof smith_slots_dir, "%s", folder);
    // A CI holder says so for as long as it lives; a local one gets a share of the cores for what it runs: the host's
    // cores / slots, and one while a CI job holds a slot.
    if (ci) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/ci-held", folder);
        int mark = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (mark >= 0) flock(mark, LOCK_SH); // kept open until the process ends
    }
    int share = smith_slots_cores() / count;
    if (share < 1 || (!ci && smith_slots_ci_held())) share = 1;
    char cores[32];
    snprintf(cores, sizeof cores, "%d", share);
    setenv("SMITH_CORES", cores, 1);
}

// The verbs that do heavy work arm the slot instead of taking it: the executor takes it when it starts its first real
// step (a compile, a link, a test), so an up-to-date build, or one that only runs build.c's functions, never waits.
static const char *smith_slots_pending;

static void smith_slots_arm(const char *verb) {
    smith_slots_pending = verb;
}

// Called by the executor before it starts a step that is not a function of build.c; takes the armed slot, once.
static void smith_slots_need(void) {
    const char *verb = smith_slots_pending;
    if (verb == nullptr) return;
    smith_slots_pending = nullptr;
    smith_slots_take(verb);
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
