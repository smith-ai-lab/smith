// The effects of the C library: what each function of libc and libSystem does to the world (issue #207, skill
// smith-lang-effects). A function a program declares with no body and no attribute is `unknown` (every effect); a
// system header's function is looked up here by name, and one the table does not know is `unknown` too.
//
// Effects are one bit each, one per kind of handler: alloc, files, net, console, clock, process, env, entropy,
// globals (writing a global or a function-local static), unknown (code smith cannot see into), fails (a failure point),
// code (making memory executable), tasks (starting a task; its handler is the scheduler). A function in the "pure" list
// has none. Failing is an effect, `fails` (issue #325): a call that stops the task
// (`abort`,
// `__builtin_trap`, smith's `smith_failure` family, so a checked index, a slice bound and a contract) is a failure
// point, and so is every caller.
#pragma once

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <string>

namespace smith_effects {

enum Effect : uint32_t {
    None = 0,
    Alloc = 1u << 0,
    Files = 1u << 1,
    Net = 1u << 2,
    Console = 1u << 3,
    Clock = 1u << 4,
    Process = 1u << 5,
    Env = 1u << 6,
    Entropy = 1u << 7,
    Globals = 1u << 8,
    Unknown = 1u << 9,
    Fails = 1u << 10,
    Code = 1u << 11,
    Tasks = 1u << 12,
    Everything = (1u << 13) - 1,
};

struct Name {
    const char *text;
    uint32_t bit;
};

inline constexpr Name names[] = {{"alloc", Alloc},     {"files", Files},     {"net", Net},     {"console", Console},
                                 {"clock", Clock},     {"process", Process}, {"env", Env},     {"entropy", Entropy},
                                 {"globals", Globals}, {"unknown", Unknown}, {"fails", Fails}, {"code", Code},
                                 {"tasks", Tasks}};

// The names of the bits of a set, in the order of `names`, joined by ", ": "alloc, files". "none" for the empty set.
inline std::string describe(uint32_t set) {
    std::string text;
    for (const Name &name : names) {
        if ((set & name.bit) == 0) continue;
        if (!text.empty()) text += ", ";
        text += name.text;
    }
    return text.empty() ? "nothing" : text;
}

struct Entry {
    const char *name;
    uint32_t effects;
};

// Pure: no effect at all (they read their arguments and write only through their pointers, which the caller owns).
inline constexpr const char *pure_functions[] = {
    // <string.h>
    "memcpy",
    "memmove",
    "memset",
    "memcmp",
    "memchr",
    "strlen",
    "strnlen",
    "strcmp",
    "strncmp",
    "strcpy",
    "strncpy",
    "strcat",
    "strncat",
    "strchr",
    "strrchr",
    "strstr",
    "strspn",
    "strcspn",
    "strpbrk",
    "strtok_r",
    "strcoll",
    "strlcpy",
    "strlcat",
    "memccpy",
    "bzero",
    "bcopy",
    "memcmp_explicit",
    // <math.h>
    "sqrt",
    "sqrtf",
    "cbrt",
    "cbrtf",
    "pow",
    "powf",
    "exp",
    "expf",
    "exp2",
    "exp2f",
    "expm1",
    "log",
    "logf",
    "log2",
    "log2f",
    "log10",
    "log10f",
    "log1p",
    "sin",
    "sinf",
    "cos",
    "cosf",
    "tan",
    "tanf",
    "asin",
    "asinf",
    "acos",
    "acosf",
    "atan",
    "atanf",
    "atan2",
    "atan2f",
    "sinh",
    "cosh",
    "tanh",
    "asinh",
    "acosh",
    "atanh",
    "fabs",
    "fabsf",
    "floor",
    "floorf",
    "ceil",
    "ceilf",
    "round",
    "roundf",
    "trunc",
    "truncf",
    "rint",
    "rintf",
    "nearbyint",
    "fmod",
    "fmodf",
    "fmin",
    "fminf",
    "fmax",
    "fmaxf",
    "fma",
    "fmaf",
    "hypot",
    "hypotf",
    "copysign",
    "copysignf",
    "ldexp",
    "frexp",
    "modf",
    "lround",
    "llround",
    "lrint",
    "llrint",
    "erf",
    "erfc",
    "tgamma",
    "lgamma",
    "remainder",
    "isnan",
    "isinf",
    "isfinite",
    "signbit",
    // <ctype.h>
    "isalnum",
    "isalpha",
    "isdigit",
    "isxdigit",
    "islower",
    "isupper",
    "isspace",
    "isprint",
    "ispunct",
    "iscntrl",
    "isgraph",
    "isblank",
    "tolower",
    "toupper",
    // <stdlib.h>, <inttypes.h>, <stdio.h>: arithmetic and text into a buffer
    "abs",
    "labs",
    "llabs",
    "div",
    "ldiv",
    "lldiv",
    "qsort",
    "bsearch",
    "strtol",
    "strtoll",
    "strtoul",
    "strtoull",
    "strtod",
    "strtof",
    "strtold",
    "atoi",
    "atol",
    "atoll",
    "atof",
    "snprintf",
    "sprintf",
    "vsnprintf",
    "vsprintf",
    "sscanf",
    "strtoimax",
    "strtoumax",
    // <setjmp.h>, <stdarg.h>, builtins and the crash path: a crash is no effect
    "__builtin_unreachable",
    "__stack_chk_fail",
    // smith's own: a failed check or contract stops the program
};

inline constexpr Entry effect_functions[] = {
    // memory
    {"malloc", Alloc},
    {"calloc", Alloc},
    {"realloc", Alloc},
    {"reallocf", Alloc},
    {"free", Alloc},
    {"aligned_alloc", Alloc},
    {"posix_memalign", Alloc},
    {"valloc", Alloc},
    {"strdup", Alloc},
    {"strndup", Alloc},
    {"asprintf", Alloc},
    {"vasprintf", Alloc},
    {"mmap", Alloc},
    {"munmap", Alloc},
    {"mprotect", Alloc},
    // console
    {"printf", Console | Globals},
    {"vprintf", Console | Globals},
    {"puts", Console | Globals},
    {"putchar", Console | Globals},
    {"putc", Console | Files | Globals},
    {"fputs", Console | Files | Globals},
    {"fputc", Console | Files | Globals},
    {"fprintf", Console | Files | Globals},
    {"vfprintf", Console | Files | Globals},
    {"fwrite", Console | Files | Globals},
    {"fflush", Console | Files | Globals},
    {"getchar", Console | Globals},
    {"gets", Console | Globals},
    {"scanf", Console | Globals},
    {"fgets", Console | Files | Globals},
    {"fgetc", Console | Files | Globals},
    {"getc", Console | Files | Globals},
    {"fread", Console | Files | Globals},
    {"perror", Console | Globals},
    {"isatty", Console},
    // files
    {"fopen", Files | Globals},
    {"freopen", Files | Globals},
    {"fclose", Files | Globals},
    {"fseek", Files | Globals},
    {"ftell", Files | Globals},
    {"rewind", Files | Globals},
    {"fileno", Files},
    {"remove", Files},
    {"rename", Files},
    {"tmpfile", Files | Globals},
    {"open", Files},
    {"openat", Files},
    {"creat", Files},
    {"close", Files | Net},
    {"read", Files | Net},
    {"write", Files | Net},
    {"pread", Files},
    {"pwrite", Files},
    {"lseek", Files},
    {"fsync", Files},
    {"ftruncate", Files},
    {"truncate", Files},
    {"unlink", Files},
    {"mkdir", Files},
    {"rmdir", Files},
    {"stat", Files},
    {"lstat", Files},
    {"fstat", Files},
    {"access", Files},
    {"chmod", Files},
    {"chdir", Files},
    {"getcwd", Files | Alloc}, // allocates when its buffer is null
    {"opendir", Files},
    {"readdir", Files},
    {"closedir", Files},
    {"realpath", Files | Alloc}, // allocates when its buffer is null
    {"dup", Files | Net},
    {"dup2", Files | Net},
    {"pipe", Files},
    {"fcntl", Files | Net},
    {"ioctl", Files | Net},
    {"flock", Files},
    {"readv", Files | Net},
    {"writev", Files | Net},
    // net
    {"socket", Net},
    {"socketpair", Net},
    {"bind", Net},
    {"listen", Net},
    {"accept", Net},
    {"connect", Net},
    {"send", Net},
    {"sendto", Net},
    {"sendmsg", Net},
    {"recv", Net},
    {"recvfrom", Net},
    {"recvmsg", Net},
    {"shutdown", Net},
    {"getsockopt", Net},
    {"setsockopt", Net},
    {"getaddrinfo", Net},
    {"freeaddrinfo", Net},
    {"poll", Files | Net},
    {"select", Files | Net},
    {"kqueue", Files | Net},
    {"kevent", Files | Net},
    // clock
    {"time", Clock},
    {"clock", Clock},
    {"clock_gettime", Clock},
    {"gettimeofday", Clock},
    {"mach_absolute_time", Clock},
    {"nanosleep", Clock},
    {"usleep", Clock},
    {"sleep", Clock},
    {"localtime", Clock | Globals},
    {"localtime_r", Clock},
    {"gmtime", Clock | Globals},
    {"mktime", Clock | Globals},
    {"strftime", Clock | Globals},
    {"difftime", None},
    // process
    {"fork", Process},
    {"vfork", Process},
    {"execv", Process},
    {"execve", Process},
    {"execvp", Process},
    {"execl", Process},
    {"execlp", Process},
    {"system", Process},
    {"popen", Process | Files | Globals},
    {"pclose", Process | Globals},
    {"posix_spawn", Process},
    {"posix_spawnp", Process},
    {"waitpid", Process},
    {"wait", Process},
    {"kill", Process},
    {"exit", Process},
    {"_exit", Process},
    {"atexit", Process | Globals},
    {"signal", Process | Globals},
    {"sigaction", Process | Globals},
    {"raise", Process},
    {"getpid", Process},
    {"getppid", Process},
    {"pthread_create", Process},
    {"pthread_join", Process},
    {"pthread_mutex_lock", Process},
    {"pthread_mutex_unlock", Process},
    {"pthread_cond_wait", Process},
    {"pthread_cond_signal", Process},
    {"setjmp", Process},
    {"longjmp", Process},
    {"sched_yield", Process},
    // env
    {"getenv", Env | Globals},
    {"setenv", Env | Globals},
    {"unsetenv", Env | Globals},
    {"putenv", Env | Globals},
    {"gethostname", Env},
    {"getuid", Env},
    {"getlogin", Env | Globals},
    {"cuserid", Env | Globals},
    {"ctermid", Globals},
    {"tmpnam", Globals},
    {"tempnam", Alloc | Globals},
    {"uname", Env},
    {"sysconf", Env},
    {"getpagesize", Env},
    {"setlocale", Env | Globals},
    // entropy
    {"arc4random", Entropy},
    {"arc4random_buf", Entropy},
    {"arc4random_uniform", Entropy},
    {"getentropy", Entropy},
    {"getrandom", Entropy},
    {"random", Globals},
    {"srandom", Globals},
    {"rand", Globals},
    {"srand", Globals},
    {"rand_r", None},
    {"drand48", Globals},
    {"srand48", Globals},
    {"errno", Globals},
    // fails: the crash path (issue #325)
    {"abort", Fails},
    {"__assert_rtn", Fails},
    {"__assert_fail", Fails},
    {"__builtin_trap", Fails},
    {"__builtin_abort", Fails},
    {"smith_failure_proved", None},
    {"smith_failure", Fails},
    {"smith_failure_detail", Fails},
    {"smith_failure_format", Fails},
    {"smith_contract_failed", Fails},
    {"smith_check_failed", Fails},
    {"smith_test_failed", Fails},
    {"__error", Globals},
    // state kept across calls (issue #724): libc's own memory, which an allocator swapped for an arena would leave
    // dangling
    {"strtok", Globals},
    {"strerror", Globals},
    {"strsignal", Globals},
    {"asctime", Clock | Globals},
    {"ctime", Clock | Globals},
    {"localeconv", Env | Globals},
    {"basename", Globals},
    {"dirname", Globals},
    {"gai_strerror", Globals},
    {"ttyname", Globals},
    {"getpwnam", Files | Globals},
    {"getpwuid", Files | Globals},
    {"dlopen", Files | Globals},
    {"dlclose", Files | Globals},
    {"dlerror", Globals},
    {"fdopen", Files | Globals},
    {"fdopendir", Files},
    {"setvbuf", Files | Globals},
    {"setbuf", Files | Globals},
    {"ungetc", Files | Globals},
    {"getline", Files | Globals | Alloc},
    {"getdelim", Files | Globals | Alloc},
    {"fscanf", Console | Files | Globals},
    {"gethostbyname", Net | Globals},
    {"__errno_location", Globals},
    // glibc's <ctype.h> macros (isalpha, tolower, ...) read the current locale's tables through these
    {"__ctype_b_loc", Globals},
    {"__ctype_tolower_loc", Globals},
    {"__ctype_toupper_loc", Globals},
};

// What a pointer a function of the C library returns means (issue #724, #720): who frees it, or how long it lives. The
// idiom is written as #720's attributes are recorded in a summary: `owned:<f>` (the caller frees it with `f`),
// `borrowed:<n>` (it points into argument `n`, 1-based) or `internal` (the function's own memory, never freed by the
// caller). A function that returns a pointer and is not here is an error in a module. Where the idiom depends on the
// arguments (`realloc`, `fgets`) it is the documented common case. `owned:<f>:<n>[,<n>...]` calls `f` with the pointer
// and then the listed arguments of the allocating call (`mmap`: `owned:munmap:2`, the length). A function that
// allocates (or returns static memory) when one argument is a null pointer constant (`getcwd(nullptr, 0)`) has a second
// idiom for it: `null_arg` (1-based) and `null_idiom`; a call picks the form by its argument.
struct Ownership {
    const char *name;
    const char *idiom;
    int null_arg = 0;
    const char *null_idiom = nullptr;
};

inline constexpr Ownership ownership_functions[] = {
    // owned: the caller frees it
    {"malloc", "owned:free"},
    {"calloc", "owned:free"},
    {"realloc", "owned:free"},
    {"reallocf", "owned:free"},
    {"aligned_alloc", "owned:free"},
    {"valloc", "owned:free"},
    {"strdup", "owned:free"},
    {"strndup", "owned:free"},
    {"fopen", "owned:fclose"},
    {"fdopen", "owned:fclose"},
    {"tmpfile", "owned:fclose"},
    {"popen", "owned:pclose"},
    {"opendir", "owned:closedir"},
    {"fdopendir", "owned:closedir"},
    {"dlopen", "owned:dlclose"},
    // libdispatch (macOS): an object the caller releases
    {"dispatch_queue_create", "owned:dispatch_release"},
    {"dispatch_data_create", "owned:dispatch_release"},
    {"dispatch_semaphore_create", "owned:dispatch_release"},
    {"mmap", "owned:munmap:2"},
    {"tempnam", "owned:free"},
    // borrowed: it points into an argument
    {"strchr", "borrowed:1"},
    {"strrchr", "borrowed:1"},
    {"strstr", "borrowed:1"},
    {"strpbrk", "borrowed:1"},
    {"memchr", "borrowed:1"},
    {"strtok_r", "borrowed:1"},
    {"memcpy", "borrowed:1"},
    {"memmove", "borrowed:1"},
    {"memset", "borrowed:1"},
    {"memccpy", "borrowed:1"},
    {"strcpy", "borrowed:1"},
    {"strncpy", "borrowed:1"},
    {"strcat", "borrowed:1"},
    {"strncat", "borrowed:1"},
    {"stpcpy", "borrowed:1"},
    {"fgets", "borrowed:1"},
    {"gets", "borrowed:1"}, // removed from C11, still declared by macOS
    {"getcwd", "borrowed:1", 1, "owned:free"},
    {"readdir", "borrowed:1"},
    {"bsearch", "borrowed:2"},
    {"realpath", "borrowed:2", 2, "owned:free"},
    {"tmpnam", "borrowed:1", 1, "internal"},
    {"ctermid", "borrowed:1", 1, "internal"},
    {"cuserid", "borrowed:1", 1, "internal"},
    {"ctime_r", "borrowed:2"},
    {"asctime_r", "borrowed:2"},
    {"localtime_r", "borrowed:2"},
    {"gmtime_r", "borrowed:2"},
    {"freopen", "borrowed:3"},
    {"dlsym", "borrowed:1"},
    // internal: the function's own memory
    {"strerror", "internal"},
    {"strerror_r", "internal"},
    {"strsignal", "internal"},
    {"getenv", "internal"},
    {"setlocale", "internal"},
    {"localeconv", "internal"},
    {"localtime", "internal"},
    {"gmtime", "internal"},
    {"asctime", "internal"},
    {"ctime", "internal"},
    {"getpwnam", "internal"},
    {"getpwuid", "internal"},
    {"getlogin", "internal"},
    {"gethostbyname", "internal"},
    {"basename", "internal"},
    {"dirname", "internal"},
    {"strtok", "internal"},
    {"gai_strerror", "internal"},
    {"dlerror", "internal"},
    {"ttyname", "internal"},
    {"__error", "internal"},
    {"__errno_location", "internal"},
    {"__ctype_b_loc", "internal"},
    {"__ctype_tolower_loc", "internal"},
    {"__ctype_toupper_loc", "internal"},
};

// The idiom of the pointer a function of the C library returns, by name; false when the table gives it none.
inline bool idiom(llvm::StringRef name, llvm::StringRef &out) {
    static const llvm::StringMap<llvm::StringRef> table = [] {
        llvm::StringMap<llvm::StringRef> map;
        for (const Ownership &entry : ownership_functions)
            map[entry.name] = entry.idiom;
        return map;
    }();
    auto found = table.find(name);
    if (found == table.end()) return false;
    out = found->second;
    return true;
}

// The second idiom of a function whose result depends on a null argument: which argument (1-based) and the idiom when
// it is a null pointer constant; false when the function has one form.
inline bool null_idiom(llvm::StringRef name, int &arg, llvm::StringRef &out) {
    for (const Ownership &entry : ownership_functions)
        if (entry.null_idiom != nullptr && name == entry.name) {
            arg = entry.null_arg;
            out = entry.null_idiom;
            return true;
        }
    return false;
}

// The effects of a function of the C library by name; false when the table does not know it.
inline bool lookup(llvm::StringRef name, uint32_t &set) {
    static const llvm::StringMap<uint32_t> table = [] {
        llvm::StringMap<uint32_t> map;
        for (const char *function : pure_functions)
            map[function] = None;
        for (const Entry &entry : effect_functions)
            map[entry.name] = entry.effects;
        return map;
    }();
    auto found = table.find(name);
    if (found == table.end()) return false;
    set = found->second;
    return true;
}

} // namespace smith_effects
