// Cores as tokens (issue #567): a host-wide pool of core tokens, so that local builds, checks and tests cannot starve
// CI and a run costs what it uses. A token is a file `core-<i>` under a folder of the host that a process holds an
// flock on, so the OS drops it when the process dies (no daemon). `smith build`, `check` and `test` take one token at
// their first heavy step (the base: the run's first worker rides on it); every further worker the run starts (an
// executor job, a test runner worker) takes one more and gives it back when it ends, so a run grows as tokens free up.
// knowledge/slots.md. Included by smith.c.
constexpr int smith_slots_max = 64;
constexpr long smith_slots_poll_ms = 200;
constexpr long smith_slots_report_ms = 30'000;
constexpr long smith_slots_wait_max_ms = 4L * 60 * 60 * 1000; // a backstop: after it the job runs without a token
constexpr size_t smith_slots_text_max = 1024;
constexpr int smith_slots_denied = -1;    // smith_slots_extra: no token is free for another worker
constexpr int smith_slots_unlimited = -2; // smith_slots_extra: there is no pool, the worker needs none

static int smith_slots_fd = -1; // the base token this process holds; closed by the OS when the process ends
static bool smith_slots_ci;     // this process runs for CI (SMITH_PRIORITY=ci)

// What this run measured about the pool (issue #754), written to stderr when it ends, for a CI run or when
// SMITH_SLOTS_LOG is set: how long the base token took, how often and for how long a worker found no token, and how
// many workers the run had over its life.
typedef struct {
    bool on;
    int pool;
    int running; // workers now (the base worker counts from the start)
    int peak;
    long start_ms;      // when the run began to count workers
    long last_ms;       // when `running` last changed
    long worker_ms;     // the integral of `running` over time
    long base_wait_ms;  // from the first ask for the base token until it was held
    long denied;        // attempts that found no token for another worker
    long want_ms;       // the time workers wanted a token and found none
    long want_since_ms; // -1 when no worker waits
} SmithSlotsStats;

static SmithSlotsStats smith_slots_stats = {.want_since_ms = -1};

static long smith_slots_now_ms(void) {
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)now.tv_sec * 1000L + now.tv_nsec / 1'000'000L;
}

static bool smith_slots_is_ci(void) {
    const char *priority = getenv("SMITH_PRIORITY");
    return priority != nullptr && strcmp(priority, "ci") == 0;
}

static void smith_slots_report(void) {
    SmithSlotsStats *stats = &smith_slots_stats;
    long now = smith_slots_now_ms();
    stats->worker_ms += (long)stats->running * (now - stats->last_ms);
    if (stats->want_since_ms >= 0) stats->want_ms += now - stats->want_since_ms;
    long span = now - stats->start_ms > 0 ? now - stats->start_ms : 1;
    long tenths = stats->worker_ms * 10 / span;
    fprintf(stderr,
            "smith: pool: waited %ld ms for the base core; %ld.%ld workers on average, %d at most, of %d cores; "
            "%ld attempts for another worker found none, %ld ms wanting a core in all\n",
            stats->base_wait_ms, tenths / 10, tenths % 10, stats->peak, stats->pool, stats->denied, stats->want_ms);
}

// Starts counting when the run's first worker begins (it rides on the base token, or on its parent's). Only a CI run or
// SMITH_SLOTS_LOG says anything at its end.
static void smith_slots_stats_begin(int pool) {
    SmithSlotsStats *stats = &smith_slots_stats;
    if (stats->on) return;
    const char *log = getenv("SMITH_SLOTS_LOG");
    if (!smith_slots_is_ci() && (log == nullptr || log[0] == '\0')) return;
    stats->on = true;
    stats->pool = pool;
    stats->running = 1;
    stats->peak = 1;
    stats->start_ms = smith_slots_now_ms();
    stats->last_ms = stats->start_ms;
    atexit(smith_slots_report);
}

// A worker started (delta 1) or ended (-1).
static void smith_slots_stats_workers(int delta) {
    SmithSlotsStats *stats = &smith_slots_stats;
    if (!stats->on) return;
    long now = smith_slots_now_ms();
    stats->worker_ms += (long)stats->running * (now - stats->last_ms);
    stats->last_ms = now;
    stats->running += delta;
    if (stats->running > stats->peak) stats->peak = stats->running;
}

// The cores of the host: SMITH_CORES (also the size of the pool, and what a test sets as a fake host), else the
// machine's. At least 1.
static int smith_slots_cores(void) {
    const char *text = getenv("SMITH_CORES");
    long cores = text != nullptr && text[0] != '\0' ? strtol(text, nullptr, 10) : 0;
    if (cores <= 0) cores = sysconf(_SC_NPROCESSORS_ONLN);
    return cores < 1 ? 1 : cores > INT_MAX ? INT_MAX : (int)cores;
}

// The size of the pool: SMITH_CORES, else the old SMITH_SLOTS read as a number of tokens, else the machine's cores.
// SMITH_SLOTS=0 turns the limit off (0 here).
static int smith_slots_count(void) {
    const char *old = getenv("SMITH_SLOTS");
    const char *cores = getenv("SMITH_CORES");
    bool given_cores = cores != nullptr && cores[0] != '\0';
    long value = -1;
    if (old != nullptr && old[0] != '\0') {
        char *end = nullptr;
        errno = 0;
        value = strtol(old, &end, 10);
        if (*end != '\0' || errno != 0 || value < 0) smith_die(smith_exit_usage, "SMITH_SLOTS needs a number", old);
        if (value == 0) return 0;
    }
    if (!given_cores && value > 0) return value > smith_slots_max ? smith_slots_max : (int)value;
    int count = smith_slots_cores();
    return count > smith_slots_max ? smith_slots_max : count;
}

// The folder of the tokens: SMITH_SLOTS_DIR, else one per user in /tmp (a launchd service and a shell have different
// $TMPDIR, and the tokens are the host's).
static void smith_slots_folder(char *out, size_t size) {
    const char *given = getenv("SMITH_SLOTS_DIR");
    int written = given != nullptr && given[0] != '\0' ? snprintf(out, size, "%s", given)
                                                       : snprintf(out, size, "/tmp/smith-slots-%u", (unsigned)getuid());
    SMITH_ASSERT(written > 0 && (size_t)written < size);
    if (mkdir(out, 0700) != 0 && errno != EEXIST) smith_die(smith_exit_missing, "cannot make the slots folder", out);
}

// Whether the process that holds the pool position we inherited is still running. A child of a smith that holds a
// token has SMITH_SLOT_HELD=<pid of the holder>; its first worker rides on that token. A stale value is ignored.
static bool smith_slots_inherited(void) {
    const char *text = getenv("SMITH_SLOT_HELD");
    if (text == nullptr || text[0] == '\0') return false;
    char *end = nullptr;
    long pid = strtol(text, &end, 10);
    if (*end != '\0' || pid <= 0) return false;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

// Whether a lock is held on the file `name` of the folder: an exclusive one fails against a shared one.
static bool smith_slots_marked(const char *folder, const char *name) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool marked = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd); // this drops a lock we got, too
    return marked;
}

// Whether a CI job is waiting for a token: it holds a shared lock on `ci-waiting`. Not atomic: a job that asks just
// before the CI job starts to wait may still take a free token, and the CI job gets the next one.
static bool smith_slots_ci_waiting(const char *folder) {
    return smith_slots_marked(folder, "ci-waiting");
}

// Whether a CI job holds a token: it holds a shared lock on `ci-held` while it lives.
static bool smith_slots_ci_held(const char *folder) {
    return smith_slots_marked(folder, "ci-held");
}

#ifndef __APPLE__
// Linux: /proc/<pid>/stat, "pid (comm) state ppid ... starttime ...". The name in parentheses may hold anything, so the
// fields are read after the last `)`: the state is field 3, the parent 4 and the start time (clock ticks since
// boot) 22. Where /proc cannot be read, only whether the process exists is known.
static bool smith_slots_proc_stat(pid_t pid, long *start, long *parent) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    char text[1024] = {};
    ssize_t count = fd >= 0 ? read(fd, text, sizeof text - 1) : -1;
    if (fd >= 0) close(fd);
    if (count <= 0) return kill(pid, 0) == 0 || errno == EPERM;
    const char *at = strrchr(text, ')');
    if (at == nullptr) return true;
    char *end = nullptr;
    long fields[20] = {};
    at += 1;
    while (*at == ' ')
        at += 1;
    at += 1; // the state letter
    for (int field = 1; field < 20; field++) {
        fields[field] = strtol(at, &end, 10);
        if (end == at) return true; // cut short: what was read stands
        at = end;
    }
    *parent = fields[1];
    *start = fields[19];
    return true;
}
#endif

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
    return smith_slots_proc_stat(pid, start, parent);
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

// Who holds the tokens: each distinct label with its count, for the message of a job that waits.
typedef struct {
    char text[smith_slots_text_max];
    int held;
} SmithSlotsHolders;

static void smith_slots_note(SmithSlotsHolders *holders, const char *line) {
    const char *name = strchr(line, '\n');
    name = name != nullptr ? name + 1 : line;
    size_t length = strlen(name);
    holders->held += 1;
    if (strstr(holders->text, name) != nullptr && length > 0) return; // listed; the count says how many
    size_t used = strlen(holders->text);
    snprintf(holders->text + used, smith_slots_text_max - used, "%s%s", used > 0 ? "; " : "", name);
}

// Tries each token once; returns the descriptor of the one it locked (the label written in it), or -1 and then
// `holders` says who holds the others. A token whose holder is dead or orphaned is replaced and tried again.
static int smith_slots_grab(const char *folder, int count, const char *label, SmithSlotsHolders *holders) {
    holders->text[0] = '\0';
    holders->held = 0;
    for (int i = 0; i < count; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/core-%d", folder, i);
        for (int again = 0; again < 2; again++) { // twice: a stale holder's file is replaced and tried once more
            int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
            if (fd < 0) break;
            if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
                char meta[96];
                size_t length = smith_slots_meta(meta, sizeof meta);
                char text[512];
                snprintf(text, sizeof text, "%.*s%s", (int)length, meta, label);
                if (ftruncate(fd, 0) == 0) (void)!pwrite(fd, text, strlen(text), 0);
                return fd;
            }
            char line[256];
            ssize_t got = pread(fd, line, sizeof line - 1, 0);
            line[got > 0 ? got : 0] = '\0';
            close(fd);
            if (again == 0 && smith_slots_stale(line)) {
                unlink(path); // the holder keeps a lock on a file nobody opens again: the token is free
                continue;
            }
            smith_slots_note(holders, line);
            break;
        }
    }
    return -1;
}

// How many tokens are free right now (each is locked and let go at once).
static int smith_slots_free(const char *folder, int count) {
    int free_tokens = 0;
    for (int i = 0; i < count; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/core-%d", folder, i);
        int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0) continue;
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) free_tokens += 1;
        close(fd);
    }
    return free_tokens;
}

static void smith_slots_sleep(long milliseconds) {
    struct timespec pause = {.tv_sec = milliseconds / 1000, .tv_nsec = (milliseconds % 1000) * 1'000'000L};
    nanosleep(&pause, nullptr);
}

// The label of this process in a token file: "pid smith verb in folder".
static void smith_slots_label(char *out, size_t size, const char *verb) {
    char where[PATH_MAX];
    if (getcwd(where, sizeof where) == nullptr) where[0] = '\0';
    snprintf(out, size, "%ld smith %s in %s", (long)getpid(), verb, where);
}

// Waits for the base token, telling why on stderr at the start and every 30 s. A CI job (SMITH_PRIORITY=ci) marks
// itself as waiting, and until it has a token the other jobs take none.
static int smith_slots_wait(const char *folder, int count, const char *label, bool ci) {
    SmithSlotsHolders holders = {};
    int mark = -1;
    if (ci) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/ci-waiting", folder);
        mark = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (mark >= 0) flock(mark, LOCK_SH);
    }
    int got = -1;
    bool behind_ci = false;
    for (long waited = 0; waited < smith_slots_wait_max_ms; waited += smith_slots_poll_ms) { // bounded by the backstop
        behind_ci = !ci && smith_slots_ci_waiting(folder);
        if (!behind_ci) got = smith_slots_grab(folder, count, label, &holders);
        if (got >= 0) break;
        if (waited % smith_slots_report_ms == 0)
            fprintf(stderr, "smith: waiting for a core (%s%d of %d held: %s)\n",
                    behind_ci ? "a CI job is waiting; " : "", holders.held, count, holders.text);
        smith_slots_sleep(smith_slots_poll_ms);
    }
    if (mark >= 0) close(mark);
    return got;
}

// Takes the base token for this process unless it rides on its parent's (a smith that runs smith) or the limit is
// off. `verb` says who holds it. The children inherit SMITH_SLOT_HELD and take theirs only for their further workers.
static void smith_slots_take(const char *verb) {
    int count = smith_slots_count();
    if (count == 0 || smith_slots_inherited()) return;
    char folder[PATH_MAX];
    smith_slots_folder(folder, sizeof folder);
    char label[256];
    smith_slots_label(label, sizeof label, verb);
    const char *priority = getenv("SMITH_PRIORITY");
    bool ci = priority != nullptr && strcmp(priority, "ci") == 0;
    smith_slots_ci = ci;
    long asked = smith_slots_now_ms();
    int fd = smith_slots_wait(folder, count, label, ci);
    smith_slots_stats.base_wait_ms = smith_slots_now_ms() - asked;
    smith_slots_stats_begin(count);
    if (fd < 0) {
        fprintf(stderr, "smith: no core after %ld hours; running without one\n", smith_slots_wait_max_ms / 3'600'000);
        return;
    }
    smith_slots_fd = fd;
    char held[32];
    snprintf(held, sizeof held, "%ld", (long)getpid());
    setenv("SMITH_SLOT_HELD", held, 1);
    if (ci) { // CI says so for as long as it lives: the others leave it cores
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/ci-held", folder);
        int mark = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (mark >= 0) flock(mark, LOCK_SH); // kept open until the process ends
    }
}

// A token for one more worker of a run that holds its base (or rides on its parent's): a descriptor to close when the
// worker ends, smith_slots_denied when none is free (try again later), smith_slots_unlimited when there is no pool.
// A waiting CI job is served first (a local run takes none meanwhile), and while a CI job holds a token a local run
// takes one only if a quarter of the pool (at least one) stays free for CI.
static int smith_slots_extra_try(const char *folder, int count, bool ci) {
    if (!ci && smith_slots_ci_waiting(folder)) return smith_slots_denied;
    char label[256];
    smith_slots_label(label, sizeof label, "worker");
    SmithSlotsHolders holders = {};
    int fd = smith_slots_grab(folder, count, label, &holders);
    if (fd < 0 || ci || !smith_slots_ci_held(folder)) return fd < 0 ? smith_slots_denied : fd;
    int reserve = count / 4 > 0 ? count / 4 : 1;
    if (smith_slots_free(folder, count) >= reserve) return fd;
    close(fd);
    return smith_slots_denied;
}

static int smith_slots_extra(void) {
    int count = smith_slots_count();
    if (count == 0 || (smith_slots_fd < 0 && !smith_slots_inherited())) return smith_slots_unlimited;
    char folder[PATH_MAX];
    smith_slots_folder(folder, sizeof folder);
    bool ci = smith_slots_is_ci();
    smith_slots_stats_begin(count);
    int fd = smith_slots_extra_try(folder, count, ci);
    SmithSlotsStats *stats = &smith_slots_stats;
    if (fd >= 0) {
        if (stats->on && stats->want_since_ms >= 0) { // the wait of the workers ends with this one
            stats->want_ms += smith_slots_now_ms() - stats->want_since_ms;
            stats->want_since_ms = -1;
        }
        smith_slots_stats_workers(1);
        return fd;
    }
    if (stats->on) {
        stats->denied += 1;
        if (stats->want_since_ms < 0) stats->want_since_ms = smith_slots_now_ms();
    }
    return smith_slots_denied;
}

// Gives a token back (a descriptor smith_slots_extra returned).
static void smith_slots_give(int token) {
    if (token < 0) return;
    close(token);
    smith_slots_stats_workers(-1);
}

// The verbs that do heavy work arm the pool instead of taking a token: the executor takes it when it starts its first
// real step (a compile, a link, a test), so an up-to-date build, or one that only runs build.c's functions, never
// waits.
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
