// `smith cc --front-end=new` (issue #1221, J2 of #1112): the `.smith` files of the command line go through smith's own
// front end (front/compile.smith: parse, check, emit, write) and become objects; the other arguments go on to clang as
// they were, with each `.smith` file replaced by its object. Experimental and off by default: without the flag a
// `.smith` file still goes through the token lowering. Linked into smith; the units runner (SMITH_CORE) has no front
// end linked, so it has none of this.
#include "front_flag.h"
#include "core.h"
#include "import_cache.h"
#ifdef __APPLE__
#include "sdk_cache.h"
#endif
#include "main.h"
#include "mcp_unit.h"
#include "package.h"
#include "self_digest.h"

#ifndef SMITH_CORE

// front::compile's entry (front/compile.smith, `front_compile_run`), declared here by hand: the generated header
// .smith/include/front/compile.h is made by the compile of a module, which the previous release's build (the lag rule)
// does not do for the sources under .smith/front/.
int32_t front_compile_run(const char *const *sources, const char *const *outputs, size_t count, const char *triple);
void front_compile_before_file(void (*hook)(const char *source));

// front::compile's header entry (`front_header_run`), declared by hand for the same reason.
int32_t front_header_run(const char *source, const char *output, const char *triple);

// front::lsp_server's entry (`front_lsp_run`, `smith lsp --smith`), declared by hand for the same reason.
int32_t front_lsp_run(void);

// front::parser's fix entry (front/parser.smith, `front_parser_fix_run`), declared by hand for the same reason.
int32_t front_parser_fix_run(const char *const *paths, size_t count);

// front::format's entries (front/format.smith), declared by hand for the same reason: `front_format_accepts` is 1 for a
// file in the new syntax, `front_format_run` formats files (in place when `write`, else it only reports).
int32_t front_format_accepts(const char *path);
int32_t front_format_run(const char *const *paths, size_t count, bool write);

// shim/cimport_std.cpp: the table of the sources of `std.<name>` that front::compile's loader reads (#2041).
void smith_std_source_add(const char *name, const char *source, size_t size);

// Hands the front end the source of every `.smith` library embedded in this binary, so `import std.<name>` resolves
// outside smith's repository. Once per process.
static void smith_front_std_sources(void) {
    static bool done = false;
    if (done) return;
    done = true;
    for (size_t i = 0; i < sizeof smith_libraries / sizeof smith_libraries[0]; i++) { // bounded by the table
        if (smith_libraries[i].lang)
            smith_std_source_add(smith_libraries[i].name, (const char *)smith_libraries[i].source,
                                 smith_libraries[i].source_size);
    }
}

constexpr size_t smith_front_sources_max = 64;
constexpr size_t smith_front_name_max = 4096;

char *smith_front_arguments[smith_front_args_max + 64]; // the command line, then the std objects a link adds
static const char *smith_front_sources[smith_front_sources_max];
static const char *smith_front_objects[smith_front_sources_max];
static char smith_front_names[smith_front_sources_max][smith_front_name_max];

// The object for `source`: `output` when `-c -o output` names the only file, else `<stem>.o` in the current folder
// (when only compiling) or in smith's temporary folder (when linking).
static void smith_front_object_name(const char *source, const char *output, bool compile_only, size_t index) {
    char *name = smith_front_names[index];
    if (output != nullptr) {
        int written = snprintf(name, smith_front_name_max, "%s", output);
        SMITH_ASSERT(written > 0 && (size_t)written < smith_front_name_max);
        return;
    }
    const char *slash = strrchr(source, '/');
    const char *stem = slash != nullptr ? slash + 1 : source;
    const char *folder = compile_only ? "." : (getenv("TMPDIR") != nullptr ? getenv("TMPDIR") : "/tmp");
    int written = snprintf(name, smith_front_name_max, "%s/%.*s.%s", folder, (int)(strlen(stem) - strlen(".smith")),
                           stem, compile_only ? "o" : "front.o");
    SMITH_ASSERT(written > 0 && (size_t)written < smith_front_name_max);
}

static bool smith_front_is_source(const char *argument) {
    size_t length = strlen(argument);
    return length > strlen(".smith") && strcmp(argument + length - strlen(".smith"), ".smith") == 0;
}

// Whether `--front-end=new` is among the arguments; any other value of the flag is an error.
bool smith_front_requested(int count, char **args) {
    bool requested = false;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "--front-end=", 12) != 0) continue;
        if (strcmp(args[i] + 12, "new") != 0) smith_die(smith_exit_usage, "--front-end takes only `new`", args[i]);
        requested = true;
    }
    return requested;
}

// The include paths and macros of the command line, one on each line after `used` bytes of `flags`: a header is read
// with the `-I`, `-isystem`, `-iquote`, `-D`, `-U`, `-include` and `-std=` the compile has. What does not fit is left
// out.
static void smith_front_command_flags(char *flags, size_t size, size_t used, int count, char **args) {
    static const char *const separate[] = {"-I", "-isystem", "-iquote", "-D", "-U", "-include"};
    for (int i = 0; i < count; i++) { // bounded by count
        const char *argument = args[i];
        bool take = strncmp(argument, "-std=", 5) == 0;
        for (size_t k = 0; k < sizeof separate / sizeof separate[0] && !take; k++)
            take = strncmp(argument, separate[k], strlen(separate[k])) == 0;
        if (!take) continue;
        bool pair = false;
        for (size_t k = 0; k < sizeof separate / sizeof separate[0]; k++)
            pair = pair || strcmp(argument, separate[k]) == 0;
        if (pair && i + 1 >= count) break;
        int more = snprintf(flags + used, size - used, pair ? "%s\n%s\n" : "%s\n", argument, pair ? args[i + 1] : "");
        if (more < 0 || (size_t)more >= size - used) {
            flags[used] = '\0'; // no room: leave it out
            break;
        }
        used += (size_t)more;
        if (pair) i++;
    }
}

// The string a line of compile_commands.json holds (`    "-I",` gives `-I`, `"file": "a.smith",` gives `a.smith`):
// the last quoted string of the line, unescaped into `out`. False when the line has none or it does not fit.
static bool smith_front_json_string(const char *line, char *out, size_t size) {
    const char *end = strrchr(line, '"');
    if (end == nullptr || end == line) return false;
    const char *begin = end - 1;
    while (begin > line && !(*begin == '"' && begin[-1] != '\\')) // bounded by the line
        begin--;
    if (*begin != '"') return false;
    size_t used = 0;
    for (const char *c = begin + 1; c < end; c++) { // bounded by the line
        if (*c == '\\' && c + 1 < end) c++;
        if (used + 1 >= size) return false;
        out[used++] = *c;
    }
    out[used] = '\0';
    return true;
}

// Whether the database's `file` names the source `path`: the same text, or one a suffix of the other after a `/`.
static bool smith_front_same_file(const char *file, const char *path) {
    while (path[0] == '.' && path[1] == '/')
        path += 2;
    size_t a = strlen(file);
    size_t b = strlen(path);
    if (a == b) return strcmp(file, path) == 0;
    const char *longer = a > b ? file : path;
    const char *shorter = a > b ? path : file;
    size_t gap = a > b ? a - b : b - a;
    return longer[gap - 1] == '/' && strcmp(longer + gap, shorter) == 0;
}

// The compile command of `source` in the database `database` (a compile_commands.json with an `arguments` array of
// one string on each line, as smith's builds write it), as the flags a header is read with: its `-I`, `-isystem`,
// `-D` and the rest of smith_front_command_flags, appended to the text in `flags`. This is how a file of a project
// whose build gives it `-I<llvm>/include` reads `<llvm-c/Core.h>` under `--front-end=new` as it does under clang
// (#2313). Nothing is added when the database or the file's entry is not there.
static void smith_front_database_flags(char *flags, size_t size, const char *database, const char *source) {
    FILE *file = fopen(database, "r");
    if (file == nullptr) return;
    static char line[PATH_MAX + 64];
    static char text[PATH_MAX];
    static char storage[256][PATH_MAX / 4];
    static char *found[256];
    int count = 0;
    bool matched = false;
    bool inside = false;
    while (fgets(line, sizeof line, file) != nullptr) { // bounded by the database
        if (strstr(line, "\"directory\"") != nullptr) {
            if (matched && count > 0) break;
            matched = false;
            inside = false;
            count = 0;
        } else if (strstr(line, "\"file\"") != nullptr) {
            matched = smith_front_json_string(line, text, sizeof text) && smith_front_same_file(text, source);
        } else if (matched && strstr(line, "\"arguments\"") != nullptr) {
            inside = true;
        } else if (matched && inside && strchr(line, ']') != nullptr && strchr(line, '"') == nullptr) {
            break;
        } else if (matched && inside && count < 256 && smith_front_json_string(line, text, sizeof storage[0])) {
            snprintf(storage[count], sizeof storage[count], "%s", text);
            found[count] = storage[count];
            count++;
        }
    }
    fclose(file);
    if (matched && count > 0) smith_front_command_flags(flags, size, strlen(flags), count, found);
}

// The flags front::cimport reads a C header with (#1762), one on each line, in $SMITH_FRONT_CIMPORT_FLAGS for
// front::compile: the compiler's resource directory and, where there is one, the SDK. The caller's own value stays.
static void smith_front_cimport_flags(const char *self, int count, char **args, const char *source) {
    if (getenv("SMITH_FRONT_CIMPORT_FLAGS") != nullptr) return;
    char folder[PATH_MAX];
    char programs[PATH_MAX];
    char resource[PATH_MAX];
    smith_resource_folder(folder);
    smith_programs_folder(folder, programs);
    smith_resource_ensure(folder, programs, self);
    smith_resource_directory(folder, resource);
#ifdef __APPLE__
    smith_sdk_environment(self);
#endif
    static char flags[1U << 16];
    int written = snprintf(flags, sizeof flags, "-resource-dir\n%s\n", resource);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof flags);
    const char *sdk = getenv("SDKROOT");
    if (sdk != nullptr && *sdk != '\0' && strlen(sdk) < PATH_MAX)
        written += snprintf(flags + written, sizeof flags - (size_t)written, "-isysroot\n%s\n", sdk);
    if (source !=
        nullptr) { // smith's own headers (`<smith/console.h>`) are written beside the project on first use (#2041)
        if (access(".smith/include/smith", F_OK) != 0) smith_write_embedded_headers(self);
        written += snprintf(flags + written, sizeof flags - (size_t)written, "-I\n.smith/include\n");
        SMITH_ASSERT((size_t)written < sizeof flags);
    } else if (access(".smith/include/smith", F_OK) == 0) { // `smith lint --fix` where smith's headers are written
        written += snprintf(flags + written, sizeof flags - (size_t)written, "-I\n.smith/include\n");
        SMITH_ASSERT((size_t)written < sizeof flags);
    }
    smith_front_command_flags(flags, sizeof flags, (size_t)written, count, args);
    if (source != nullptr) { // the build's own flags for the file, where a compile_commands.json has them (#2313)
        static const char *const databases[] = {"compile_commands.json", "build/compile_commands.json",
                                                ".smith/compile_commands.json"};
        for (size_t k = 0; k < sizeof databases / sizeof databases[0]; k++)
            if (access(databases[k], R_OK) == 0) {
                smith_front_database_flags(flags, sizeof flags, databases[k], source);
                break;
            }
    }
    setenv("SMITH_FRONT_CIMPORT_FLAGS", flags, 1);
}

// The command line `front_compile_run` was called with, for `smith_front_flags_of_file`.
static const char *smith_front_self;
static int smith_front_count;
static char **smith_front_args;
static bool smith_front_flags_given; // $SMITH_FRONT_CIMPORT_FLAGS came from the caller: every file uses it

// Before each file of a command line with several: the flags of that file's own compile command (#2888), so that it
// reads its C headers as it does compiled alone.
static void smith_front_flags_of_file(const char *source) {
    if (smith_front_flags_given) return;
    unsetenv("SMITH_FRONT_CIMPORT_FLAGS");
    smith_front_cimport_flags(smith_front_self, smith_front_count, smith_front_args, source);
}

// `-g` (or `-g<level>`, not `-g0`) on the command line sets $SMITH_FRONT_DEBUG to 1 for front::compile, which then
// emits debug info (#1979): the line table, the source names and the variables `smith dbg`, `dap` and `symbolize`
// read. Without `-g` it is unset, so an object has none.
static void smith_front_debug_flag(int count, char **args) {
    bool debug = false;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "-g", 2) != 0 || strcmp(args[i], "-g0") == 0) continue;
        debug = strcmp(args[i], "-gdwarf-aranges") != 0;
    }
    if (debug) {
        setenv("SMITH_FRONT_DEBUG", "1", 1);
    } else {
        unsetenv("SMITH_FRONT_DEBUG");
    }
}

// Whether `--smith-header` is among the arguments: the new front end writes a header instead of an object (#1621).
static bool smith_front_header_requested(int count, char **args) {
    for (int i = 0; i < count; i++) { // bounded by count
        if (strcmp(args[i], "--smith-header") == 0) return true;
    }
    return false;
}

// `--front-end=new --smith-header src.smith -o out.h [--target=T]`: exactly one source and one `-o`. Returns 0 (nothing
// is left for clang) or -1 when the header was not written.
static int smith_front_header(int count, char **args, size_t sources, const char *output, const char *triple) {
    int outputs = 0;
    for (int i = 0; i < count; i++) // bounded by count
        outputs += strcmp(args[i], "-o") == 0 ? 1 : 0;
    if (sources != 1 || outputs != 1 || output == nullptr)
        smith_die(smith_exit_usage, "usage: smith cc --front-end=new --smith-header SOURCE.smith -o OUTPUT.h", nullptr);
    return front_header_run(smith_front_sources[0], output, triple) == 0 ? 0 : -1;
}

// Whether an argument names a file clang would compile or link (a source, an object, a library): after the front end
// took the `.smith` files, an argument list without one has nothing left for clang to do.
static bool smith_front_is_input(const char *argument) {
    static const char *const suffixes[] = {".c", ".cc", ".cpp", ".cxx", ".m",  ".mm",
                                           ".o", ".a",  ".s",   ".S",   ".bc", ".ll"};
    size_t length = strlen(argument);
    if (argument[0] == '-') return false;
    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) { // bounded by the table
        size_t n = strlen(suffixes[i]);
        if (length > n && strcmp(argument + length - n, suffixes[i]) == 0) return true;
    }
    return false;
}

// The depfile a build's compile asked for (`-MD -MF file`): `output: source`, so the step reruns when the source
// changes. The imports of a module are not listed: a change to one is picked up by the front end's own caches.
static void smith_front_depfile(const char *depfile, const char *output, const char *source) {
    FILE *file = fopen(depfile, "w");
    if (file == nullptr) smith_die(smith_exit_missing, "cannot write the depfile", depfile);
    fprintf(file, "%s: %s\n", output != nullptr ? output : "a.out", source);
    fclose(file);
}

// The std a program links (#2853). `smith cc --front-end=new` links what a program needs, as a C compile links libc:
// std.contract (the emitter's checks call it with no import), every std module the program imports, directly or
// through other imports, and the C libraries of smith's those modules include (`<smith/trace.h>`). The std embedded
// in smith is in today's syntax, which the new front end reads only fixed: the first compile that needs a module
// writes the embedded libraries to `<cache>/front-std/<smith's digest>/`, fixes the module there with `smith lint
// --fix` and compiles it to `obj/<target>-<g|o>/<name>.o`; later compiles find both. A lock file keeps two compiles
// from doing it at once. Where the current folder has no `lib/std` (a user's project), the program's own compile
// reads the fixed texts too; in smith's repository `lib/std` on disk wins, as it always did.
constexpr size_t smith_front_std_max = 48;
static_assert(smith_front_std_max >= sizeof smith_libraries / sizeof smith_libraries[0]);
static const SmithLibrary *smith_front_std[smith_front_std_max];
static size_t smith_front_std_count;
static char smith_front_std_folder[PATH_MAX];                       // the cache's folder, once made
static char smith_front_std_objects[smith_front_std_max][PATH_MAX]; // the objects a link adds
static size_t smith_front_std_object_count;

static void smith_front_std_want(const char *name, size_t length) {
    for (size_t i = 0; i < sizeof smith_libraries / sizeof smith_libraries[0]; i++) { // bounded by the table
        const SmithLibrary *library = &smith_libraries[i];
        if (strlen(library->name) != length || strncmp(library->name, name, length) != 0) continue;
        for (size_t k = 0; k < smith_front_std_count; k++) // bounded by the wanted
            if (smith_front_std[k] == library) return;
        SMITH_ASSERT(smith_front_std_count < smith_front_std_max);
        smith_front_std[smith_front_std_count++] = library;
        return;
    }
}

static size_t smith_front_word(const char *text, size_t size) {
    size_t n = 0;
    while (n < size && (isalnum((unsigned char)text[n]) || text[n] == '_'))
        n++;
    return n;
}

static bool smith_front_starts(const char *text, size_t size, const char *prefix) {
    size_t n = strlen(prefix);
    return size >= n && memcmp(text, prefix, n) == 0;
}

// The library one line names: `import std.x`, `import std::x`, `from std.x import ...` (`pub` before any), or a header
// of smith's, `<smith/x.h>`, in `import <...>`, `import c <...>` or `#include <...>`.
static void smith_front_std_line(const char *line, size_t size) {
    while (size > 0 && (*line == ' ' || *line == '\t')) {
        line++;
        size--;
    }
    if (smith_front_starts(line, size, "pub ")) {
        line += 4;
        size -= 4;
    }
    bool import = smith_front_starts(line, size, "import ") || smith_front_starts(line, size, "from ");
    if (!import && !smith_front_starts(line, size, "#include")) return;
    for (size_t i = 0; i + 7 < size; i++) { // bounded by the line
        if (memcmp(line + i, "<smith/", 7) != 0) continue;
        smith_front_std_want(line + i + 7, smith_front_word(line + i + 7, size - i - 7));
        return;
    }
    if (!import) return;
    size_t at = (size_t)(memchr(line, ' ', size) == nullptr ? size : (const char *)memchr(line, ' ', size) - line);
    while (at < size && line[at] == ' ')
        at++;
    const char *rest = line + at;
    size_t left = size - at;
    if (smith_front_starts(rest, left, "std.")) smith_front_std_want(rest + 4, smith_front_word(rest + 4, left - 4));
    if (smith_front_starts(rest, left, "std::")) smith_front_std_want(rest + 5, smith_front_word(rest + 5, left - 5));
}

static void smith_front_std_scan(const char *text, size_t size) {
    size_t at = 0;
    for (size_t guard = 0; guard <= size && at < size; guard++) { // each pass reads one line
        const char *end = memchr(text + at, '\n', size - at);
        size_t length = end == nullptr ? size - at : (size_t)(end - (text + at));
        smith_front_std_line(text + at, length);
        at += length + 1;
    }
}

// Adds what the wanted libraries need, until nothing new turns up: each is read once, and the table is finite.
static void smith_front_std_close(void) {
    for (size_t i = 0; i < smith_front_std_count; i++) // bounded by smith_front_std_max
        smith_front_std_scan((const char *)smith_front_std[i]->source, smith_front_std[i]->source_size);
}

// The wanted libraries of a source file, read from disk; a file that cannot be read adds none.
static void smith_front_std_of_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    struct stat info;
    if (fstat(fd, &info) == 0 && info.st_size > 0) {
        void *text = mmap(nullptr, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (text != MAP_FAILED) {
            smith_front_std_scan(text, (size_t)info.st_size);
            munmap(text, (size_t)info.st_size);
        }
    }
    close(fd);
}

static bool smith_front_write_file(const char *path, const void *bytes, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    bool good = write(fd, bytes, size) == (ssize_t)size;
    return close(fd) == 0 && good;
}

// Runs `argv` (smith itself) in `folder` and waits: whether it exited 0. $SMITH_FRONT_CIMPORT_FLAGS is the caller's,
// with paths of the caller's folder, so the child goes without it and works out its own. With `log`, the child's
// standard error goes to that file (in `folder`) instead of the caller's.
static bool smith_front_run_in(const char *folder, char *const *argv, const char *log) {
    const char *flags = getenv("SMITH_FRONT_CIMPORT_FLAGS");
    static char saved[1U << 16];
    bool had = flags != nullptr && strlen(flags) < sizeof saved;
    if (had) snprintf(saved, sizeof saved, "%s", flags);
    unsetenv("SMITH_FRONT_CIMPORT_FLAGS");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, folder);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0); // `N fixes applied`
    if (log != nullptr)
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    pid_t pid = 0;
    int started = posix_spawn(&pid, argv[0], &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (had) setenv("SMITH_FRONT_CIMPORT_FLAGS", saved, 1);
    int status = 0;
    if (started != 0 || waitpid(pid, &status, 0) != pid) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

#ifdef SMITH_IMPORT_IDENTITY_LINKED
extern const char smith_import_identity[]; // runtime/import_identity.c
#endif

// What names this smith's std: the import identity `smith build` compiled in (every file under front/ and lib/std/,
// the runtime, build.c, the LLVM pack, the flags: what a std object depends on), else the digest of the binary, which
// hashes all of it and is slower.
static bool smith_front_std_identity(const char *self, char digest[smith_sha256_hex_size]) {
#ifdef SMITH_IMPORT_IDENTITY_LINKED
    if (strlen(smith_import_identity) >= 16) {
        int written = snprintf(digest, smith_sha256_hex_size, "%.16s", smith_import_identity);
        return written == 16;
    }
#endif
    return self != nullptr && smith_self_digest(self, digest);
}

// The cache's folder for this smith (`<cache>/front-std/<identity>`), made once. False when there is none.
static bool smith_front_std_root(const char *self) {
    if (smith_front_std_folder[0] != '\0') return true;
    char digest[smith_sha256_hex_size];
    char base[PATH_MAX];
    SmithPackageFault fault;
    if (!smith_front_std_identity(self, digest) || !smith_cache_base(base, &fault)) return false;
    char folder[PATH_MAX];
    int written = snprintf(folder, sizeof folder, "%s/front-std/%.16s/lib/std", base, digest);
    if (written <= 0 || (size_t)written >= sizeof folder || !smith_package_make_folders(folder)) return false;
    written = snprintf(smith_front_std_folder, sizeof smith_front_std_folder, "%s/front-std/%.16s", base, digest);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof smith_front_std_folder);
    return true;
}

static void smith_front_std_path(char *out, const char *format, const char *name) {
    char tail[PATH_MAX];
    int written = snprintf(tail, sizeof tail, format, name);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof tail);
    written = snprintf(out, PATH_MAX, "%s/%s", smith_front_std_folder, tail);
    if (written <= 0 || written >= PATH_MAX) smith_die(smith_exit_usage, "the cache path is too long", tail);
}

// smith's headers (`.smith/include/smith/`, `.smith/lib/`) in the cache's folder, written there as a project's are.
static void smith_front_std_headers(const char *self) {
    char path[PATH_MAX];
    smith_front_std_path(path, "%s", ".smith/include/smith/trace.h");
    if (access(path, F_OK) == 0) return;
    int here = open(".", O_RDONLY | O_DIRECTORY);
    if (here < 0 || chdir(smith_front_std_folder) != 0) smith_die(smith_exit_missing, "cannot enter", path);
    smith_write_embedded_headers(self);
    if (fchdir(here) != 0) smith_die(smith_exit_missing, "cannot go back to the folder of the compile", nullptr);
    close(here);
}

// Says once, on standard error, that this compile prepares std, so that the wait of a cold cache is not silent.
static void smith_front_std_notice(void) {
    static bool said = false;
    if (said) return;
    said = true;
    fprintf(stderr, "smith: preparing its std library for the new front end, once for this smith (%s)\n",
            smith_front_std_folder);
}

// Fixes, in one `smith lint --fix`, each wanted module of today's syntax that is not fixed yet, then marks it. The
// fixer's warnings (the `++` of today's std) go to `fix.log` in the cache, not to the user. This step goes at the
// switch-over (#1631): std is then in smith's own syntax, and only the object cache below stays.
static void smith_front_std_fix(const char *self) {
    static char paths[smith_front_std_max][PATH_MAX];
    char *argv[smith_front_std_max + 4] = {(char *)self, "lint", "--fix"};
    size_t used = 3;
    char marker[PATH_MAX];
    for (size_t i = 0; i < smith_front_std_count; i++) { // bounded by smith_front_std_max
        const SmithLibrary *library = smith_front_std[i];
        smith_front_std_path(marker, "lib/std/%s.fixed", library->name);
        if (!library->lang || access(marker, F_OK) == 0) continue;
        smith_front_std_path(paths[used], "lib/std/%s.smith", library->name);
        if (!smith_front_write_file(paths[used], library->source, library->source_size))
            smith_die(smith_exit_missing, "cannot write", paths[used]);
        argv[used] = paths[used];
        used++;
    }
    if (used == 3) return;
    argv[used] = nullptr;
    smith_front_std_notice();
    char log[PATH_MAX];
    smith_front_std_path(log, "%s", "fix.log");
    if (!smith_front_run_in(smith_front_std_folder, argv, log))
        smith_die(smith_exit_usage, "cannot fix smith's std for the new front end; the fixer's output is in", log);
    for (size_t i = 3; i < used; i++) { // bounded by used
        int written =
            snprintf(marker, sizeof marker, "%.*s.fixed", (int)(strlen(paths[i]) - strlen(".smith")), paths[i]);
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof marker);
        if (!smith_front_write_file(marker, "", 0)) smith_die(smith_exit_missing, "cannot write", marker);
    }
}

// The front end reads each wanted module's fixed text in place of the embedded one (kept mapped for the process).
static void smith_front_std_register(void) {
    char path[PATH_MAX];
    for (size_t i = 0; i < smith_front_std_count; i++) { // bounded by smith_front_std_max
        if (!smith_front_std[i]->lang) continue;
        smith_front_std_path(path, "lib/std/%s.smith", smith_front_std[i]->name);
        int fd = open(path, O_RDONLY);
        struct stat info;
        if (fd < 0 || fstat(fd, &info) != 0 || info.st_size <= 0) smith_die(smith_exit_missing, "cannot read", path);
        void *text = mmap(nullptr, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (text == MAP_FAILED) smith_die(smith_exit_missing, "cannot read", path);
        smith_std_source_add(smith_front_std[i]->name, text, (size_t)info.st_size);
    }
}

// Whether the command line has the library already: its source (`lib/std/contract.smith`, `lib/handlers.c`), or
// the object a build made of it (`smith_handlers.o`, `-lsmith_handlers` on macOS). Then its object is the program's.
static bool smith_front_std_given(const char *name, int count, char **args) {
    char forms[4][PATH_MAX];
    const char *patterns[4] = {"std/%s.smith", "lib/%s.c", "smith_%s.o", "-lsmith_%s"};
    for (size_t k = 0; k < 4; k++) { // bounded by the forms
        int written = snprintf(forms[k], sizeof forms[k], patterns[k], name);
        SMITH_ASSERT(written > 0 && (size_t)written < sizeof forms[k]);
    }
    for (int i = 0; i < count; i++) { // bounded by count
        for (size_t k = 0; k < 3; k++)
            if (smith_ends_with(args[i], forms[k])) return true;
        if (strcmp(args[i], forms[3]) == 0) return true;
    }
    return false;
}

// One object of the cache: a module through the new front end, a C library through clang, written beside and renamed
// so that a killed compile leaves none.
static void smith_front_std_object(const char *self, const SmithLibrary *library, const char *object,
                                   const char *triple, bool debug) {
    char source[PATH_MAX];
    char partial[PATH_MAX];
    char target[PATH_MAX];
    // Relative to the cache's folder, where the child runs: `lib/std/math.smith` under the root `lib` names the module
    // `std::math`, and so its symbols (`std::math::wrap_angle`, not a bare `wrap_angle` that std.math_trig has too).
    int written =
        snprintf(source, sizeof source, library->lang ? "lib/std/%s.smith" : ".smith/lib/%s.c", library->name);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof source);
    written = snprintf(partial, sizeof partial, "%s.tmp-%d", object, (int)getpid());
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof partial);
    written = snprintf(target, sizeof target, "--target=%s", triple != nullptr ? triple : "");
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof target);
    char *argv[16] = {(char *)self, "cc"};
    size_t used = 2;
    if (library->lang) {
        argv[used++] = "--front-end=new";
    } else {
        argv[used++] = "-std=gnu23";
        argv[used++] = "-I";
        argv[used++] = ".smith/include";
    }
    argv[used++] = "-c";
    argv[used++] = source;
    argv[used++] = "-o";
    argv[used++] = partial;
    if (debug) argv[used++] = "-g";
    if (triple != nullptr) argv[used++] = target;
    argv[used] = nullptr;
    smith_front_std_notice();
    char log[PATH_MAX]; // the compile's warnings (the `++` the fixer leaves in today's std), not the user's
    smith_front_std_path(log, "%s", "compile.log");
    bool made = smith_front_run_in(smith_front_std_folder, argv, log) && rename(partial, object) == 0;
    char list[PATH_MAX];
    written = snprintf(list, sizeof list, "%s.std", partial);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof list);
    unlink(list); // the child's list of what its object needs: the link reads the closure itself
    if (!made) {
        unlink(partial);
        smith_die(smith_exit_usage, "cannot compile smith's std library for the link; its errors are in", log);
    }
}

// The objects of the wanted libraries for this target and debug setting, in `obj/<target>-<g|o>/`, compiled when
// missing; the command line's own copy of a library is left to it.
static void smith_front_std_compile(const char *self, const char *triple, bool debug, int count, char **args) {
    char kind[PATH_MAX];
    int written = snprintf(kind, sizeof kind, "obj/%s-%s", triple != nullptr ? triple : "host", debug ? "g" : "o");
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof kind);
    char folder[PATH_MAX];
    smith_front_std_path(folder, "%s", kind);
    if (!smith_package_make_folders(folder)) smith_die(smith_exit_missing, "cannot make", folder);
    smith_front_std_object_count = 0;
    for (size_t i = 0; i < smith_front_std_count; i++) { // bounded by smith_front_std_max
        const SmithLibrary *library = smith_front_std[i];
        if (smith_front_std_given(library->name, count, args)) continue;
        char *object = smith_front_std_objects[smith_front_std_object_count++];
        written = snprintf(object, PATH_MAX, "%s/%s.o", folder, library->name);
        SMITH_ASSERT(written > 0 && written < PATH_MAX);
        if (access(object, F_OK) != 0) smith_front_std_object(self, library, object, triple, debug);
    }
}

// Makes the wanted libraries ready: fixed in the cache, read by this compile where the folder has no `lib/std` of its
// own, and compiled to objects when the command links. Under the cache's lock; false when there is no cache folder.
static bool smith_front_std_prepare(const char *self, bool linking, const char *triple, bool debug, int count,
                                    char **args) {
    bool own = access("lib/std", F_OK) == 0; // smith's repository, or a project with its own library
    if (smith_front_std_count == 0 || (own && !linking) || !smith_front_std_root(self)) return false;
    char lock[PATH_MAX];
    smith_front_std_path(lock, "%s", "lock");
    int fd = open(lock, O_RDWR | O_CREAT, 0644);
    if (fd < 0 || flock(fd, LOCK_EX) != 0) smith_die(smith_exit_missing, "cannot lock", lock);
    smith_front_std_headers(self);
    smith_front_std_fix(self);
    if (!own) smith_front_std_register();
    if (linking) smith_front_std_compile(self, triple, debug, count, args);
    close(fd); // the lock goes with it
    return true;
}

// What the command's `.smith` files import, with what those need; when it links, also what the `.std` lists beside its
// objects name, and std.contract, which every program's checks call.
static void smith_front_std_gather(int count, char **args, size_t sources, bool linking) {
    smith_front_std_count = 0;
    for (size_t i = 0; i < sources; i++) // bounded by the sources
        smith_front_std_of_file(smith_front_sources[i]);
    if (linking) {
        char list[PATH_MAX];
        for (int i = 0; i < count; i++) { // bounded by count
            if (!smith_ends_with(args[i], ".o") || (i > 0 && strcmp(args[i - 1], "-o") == 0)) continue;
            int written = snprintf(list, sizeof list, "%s.std", args[i]);
            if (written > 0 && (size_t)written < sizeof list) smith_front_std_of_file(list);
        }
        smith_front_std_want("contract", strlen("contract"));
    }
    smith_front_std_close();
}

// Beside each object a compile wrote (`-c`), `<object>.std`: the std modules its source needs, as import lines, so the
// link of the objects (`smith build`) links them too. None, and an old list removed, when it needs none.
static void smith_front_std_lists(size_t sources) {
    static char text[smith_front_std_max * 64];
    size_t used = 0;
    for (size_t i = 0; i < smith_front_std_count; i++) { // bounded by smith_front_std_max
        int more = snprintf(text + used, sizeof text - used, "import std.%s\n", smith_front_std[i]->name);
        SMITH_ASSERT(more > 0 && (size_t)more < sizeof text - used);
        used += (size_t)more;
    }
    char path[PATH_MAX];
    for (size_t i = 0; i < sources; i++) { // bounded by the sources
        int written = snprintf(path, sizeof path, "%s.std", smith_front_objects[i]);
        if (written <= 0 || (size_t)written >= sizeof path) continue;
        if (used == 0)
            unlink(path);
        else if (!smith_front_write_file(path, text, used))
            smith_die(smith_exit_missing, "cannot write", path);
    }
}

// The std objects after the command's own arguments, with libm on Linux (std.math calls it; macOS has it in libSystem).
static int smith_front_std_link(int used) {
    for (size_t i = 0; i < smith_front_std_object_count; i++) // bounded by smith_front_std_max
        smith_front_arguments[used++] = smith_front_std_objects[i];
#ifndef __APPLE__
    if (smith_front_std_object_count > 0) smith_front_arguments[used++] = "-lm";
    smith_front_arguments[used++] = "-Wl,--gc-sections"; // each function is a section of its own (front::emit_object)
#else
    smith_front_arguments[used++] = "-Wl,-dead_strip";
#endif
    return used;
}

// Compiles the `.smith` files among `args` (argv[first ..]) with the front end and rewrites the arguments for clang in
// smith_front_arguments (argv[0 .. first) first): returns their number, or -1 when a file did not compile (its
// diagnostics are printed) and 0 when nothing is left for clang to do.
int smith_front_rewrite(char **argv, int first, int count, char **args, const char *self) {
    SMITH_ASSERT((size_t)count < smith_front_args_max && first >= 1 && first <= 8);
    for (int i = 0; i < first; i++) // bounded by 8
        smith_front_arguments[i] = argv[i];
    bool compile_only = false;
    bool stops_early = false; // `-S` or `-E`: clang links nothing, so no std is added
    const char *output = nullptr;
    const char *triple = nullptr;
    size_t sources = 0;
    const char *depfile = nullptr; // `-MD -MF file` of a build's compile: smith writes the file itself (#2042)
    int source_at = 0;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strcmp(args[i], "-MF") == 0 && i + 1 < count) depfile = args[i + 1];
        if (strcmp(args[i], "-c") == 0) compile_only = true;
        if (strcmp(args[i], "-S") == 0 || strcmp(args[i], "-E") == 0) stops_early = true;
        if (strcmp(args[i], "-o") == 0 && i + 1 < count) output = args[i + 1];
        if (strncmp(args[i], "--target=", 9) == 0) triple = args[i] + 9;
        if (smith_front_is_source(args[i]) && i > 0 && strcmp(args[i - 1], "-o") != 0) {
            if (sources == smith_front_sources_max) smith_die(smith_exit_usage, "too many .smith files", nullptr);
            source_at = i;
            smith_front_sources[sources++] = args[i];
        }
    }
    if (sources > 0) smith_front_std_sources();
    if (smith_front_header_requested(count, args)) return smith_front_header(count, args, sources, output, triple);
    bool single = compile_only && sources == 1;
    for (size_t i = 0; i < sources; i++) { // bounded by the arguments
        smith_front_object_name(smith_front_sources[i], single ? output : nullptr, compile_only, i);
        smith_front_objects[i] = smith_front_names[i];
    }
    smith_front_flags_given = getenv("SMITH_FRONT_CIMPORT_FLAGS") != nullptr;
    if (sources > 0) smith_front_cimport_flags(self, count, args, smith_front_sources[0]);
    smith_front_self = self;
    smith_front_count = count;
    smith_front_args = args;
    front_compile_before_file(sources > 1 ? smith_front_flags_of_file : nullptr);
    smith_front_debug_flag(count, args);
    bool links = !compile_only && !stops_early;
    smith_front_std_gather(count, args, sources, links);
    (void)smith_front_std_prepare(self, links, triple, getenv("SMITH_FRONT_DEBUG") != nullptr, count, args);
    int compiled = sources > 0 ? front_compile_run(smith_front_sources, smith_front_objects, sources, triple) : 0;
    smith_import_cache_report();
    if (compiled != 0) return -1;
    if (compile_only) smith_front_std_lists(sources);
    int used = first;
    size_t seen = 0;
    for (int i = 0; i < count; i++) { // bounded by count
        if (strncmp(args[i], "--front-end=", 12) == 0 || strcmp(args[i], "-MD") == 0) continue;
        if (strcmp(args[i], "-MF") == 0 && i + 1 < count) {
            i++;
            continue;
        }
        if (smith_front_is_source(args[i]) && (i == 0 || strcmp(args[i - 1], "-o") != 0)) {
            if (!compile_only) smith_front_arguments[used++] = smith_front_names[seen];
            seen++;
            continue;
        }
        smith_front_arguments[used++] = args[i];
    }
    if (links) used = smith_front_std_link(used);
    bool only_options = true;            // nothing but options left: clang has no input to compile
    for (int i = first; i < used; i++) { // bounded by used
        if (strcmp(smith_front_arguments[i], "-o") == 0 && i + 1 < used)
            i++; // the value of -o is the output, not an input
        else if (smith_front_is_input(smith_front_arguments[i]))
            only_options = false;
    }
    if (compile_only && only_options && sources == 1 && depfile != nullptr)
        smith_front_depfile(depfile, output, args[source_at]);
    return compile_only && only_options ? 0 : used;
}
#endif // SMITH_CORE

// Whether `smith lint --fix FILE...` is the front end's: `--fix` first and no `--`. With a `--` it is clang-tidy's
// `--fix` on a `.smith` file with the flags of its compile (smith-check), as before.
bool smith_lint_fix_requested(int count, char **args) {
    if (count < 1 || strcmp(args[0], "--fix") != 0) return false;
    for (int i = 1; i < count; i++) { // bounded by count
        if (strcmp(args[i], "--") == 0) return false;
    }
    return true;
}

// `smith lint --fix FILE...` (#1442): each `.smith` file is parsed by smith's own front end, the fixes its parse errors
// carry are applied, and `path: N fixes applied` is printed; a file with nothing to fix reports 0. The units runner has
// no front end, so it refuses.
// Gives the fixer a folder for what it keeps between processes (#2889), unless the caller set one (the count gives its
// own per smith, #2586): the mended text of each import and the masks of its fields (`front::fix::mended_import`,
// `fix_zero`), keyed there by the hash of the path and the text. The fixer depends on the code of the smith that runs
// it, so the folder is named by `smith_self_digest`: another smith reads none of these entries. A failure only leaves
// the fixer cold; a cache entry never changes the fixed file.
static void smith_lint_fix_import_cache(const char *self) {
    if (getenv("SMITH_FIX_IMPORT_CACHE") != nullptr) return;
    char digest[smith_sha256_hex_size];
    char base[PATH_MAX];
    char folder[PATH_MAX];
    SmithPackageFault fault;
    if (self == nullptr || !smith_self_digest(self, digest) || !smith_cache_base(base, &fault)) return;
    int written = snprintf(folder, sizeof folder, "%s/fix-imports/%.16s", base, digest);
    if (written <= 0 || (size_t)written >= sizeof folder || !smith_package_make_folders(folder)) return;
    setenv("SMITH_FIX_IMPORT_CACHE", folder, 1);
}

int smith_lint_fix(int count, char **args, const char *self) {
#ifdef SMITH_CORE
    (void)count;
    (void)args;
    (void)self;
    smith_die(smith_exit_usage, "lint --fix needs the full smith (smith's own front end is not in this one)", nullptr);
#else
    if (count < 1) smith_die(smith_exit_usage, "usage: smith lint --fix FILE.smith...", nullptr);
    for (int i = 0; i < count; i++) {
        if (!smith_ends_with(args[i], ".smith"))
            smith_die(smith_exit_usage, "lint --fix reads .smith files only", args[i]);
    }
    smith_front_cimport_flags(self, 0, args,
                              nullptr); // the fixer reads the headers a file imports as `smith cc` does (#2209)
    smith_lint_fix_import_cache(self);
    return front_parser_fix_run((const char *const *)args, (size_t)count) == 0 ? 0 : 1;
#endif
}

#ifndef SMITH_CORE
#include <fnmatch.h>
// `smith fmt` on `.smith` files (#1890): the files of `args` (argv[2 ..]) that are in the new syntax go to smith's own
// formatter (front::format) and are taken out of the arguments, which clang-format then gets without them: `.c` files
// and `.smith` files in today's syntax stay clang-format's until the switch-over (#1631). Writes in place unless
// `--dry-run` or `-n` is among the arguments. Returns the formatter's status (0 when it had no file), and the number
// of arguments left in *count; *only is set when it took files and no file argument is left for clang-format.
// Whether `.clang-format-ignore` (in the current folder, as clang-format reads it: one glob a line, `#` comments, paths
// relative to it) names `file`: such a file is left as it is.
static bool smith_fmt_ignored(const char *file) {
    FILE *list = fopen(".clang-format-ignore", "r");
    if (list == nullptr) return false;
    while (file[0] == '.' && file[1] == '/')
        file += 2; // bounded by the name
    char line[PATH_MAX];
    bool ignored = false;
    while (!ignored && fgets(line, sizeof line, list) != nullptr) { // bounded by the file
        line[strcspn(line, "\n")] = '\0';
        ignored = line[0] != '\0' && line[0] != '#' && fnmatch(line, file, 0) == 0;
    }
    fclose(list);
    return ignored;
}

int smith_fmt_front(int *count, char **args, bool *only) {
    static const char *taken[smith_front_args_max];
    size_t found = 0;
    bool write = true;
    int kept = 0;
    bool found_ignored = false;
    for (int i = 0; i < *count; i++) { // bounded by count
        if (strcmp(args[i], "--dry-run") == 0 || strcmp(args[i], "-n") == 0) write = false;
        if (i >= 2 && smith_front_is_source(args[i]) && access(args[i], R_OK) == 0 &&
            front_format_accepts(args[i]) == 1 && found < smith_front_args_max) {
            if (!smith_fmt_ignored(args[i]))
                taken[found++] = args[i];
            else
                found_ignored = true;
            continue;
        }
        args[kept++] = args[i];
    }
    *count = kept;
    bool others = false;
    for (int i = 2; i < kept; i++)
        others = others || args[i][0] != '-';
    *only = (found > 0 || found_ignored) && !others;
    return found == 0 ? 0 : front_format_run(taken, found, write);
}
#endif
