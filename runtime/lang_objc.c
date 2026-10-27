// Objective-C from a .smith file (issue #318, skill smith-lang-objc, knowledge/objc.md). Included by runtime/lang.c,
// after runtime/lang_import.c.
//
// `import <AppKit/AppKit.h>;` is read in Objective-C mode by a separate compile (shim/objc_index.cpp, started by the
// host), never by this file's own: the header cannot be read as C, and reading the file as Objective-C would change
// what its C means. That compile leaves two build outputs under .smith/objc/: types.h, C for everything but the methods
// (every name `objc_<name>`), and an index of the methods. This file does the rest, in plain C, with no Objective-C in
// the output:
//   - `objc::NSRect`, `objc::NSBackingStoreBuffered`, `objc::MTLCreateSystemDefaultDevice` are `objc_` and the name;
//   - `objc::NSWindow::setTitle` is `objc_NSWindow_setTitle`, a typed static inline function written in front of the
//     file's first declaration, for every method of every class the text names (and of its superclasses and protocols):
//     a call of objc_msgSend cast to the method's own type, so a wrong argument is an ordinary C error;
//   - a linker option for each framework and libobjc is written into the object, so a build needs no flag.
// A host without a compiler (the header pass) has no `objc_facts` and reads such an import as a C header.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

constexpr size_t smith_lang_objc_text_max = 8U << 20;
constexpr size_t smith_lang_objc_names_max = 256;
constexpr size_t smith_lang_objc_owners_max = 256;
constexpr size_t smith_lang_objc_set_size = 16384; // a power of two

static char smith_lang_objc_text[smith_lang_objc_text_max];
static uint64_t smith_lang_objc_instance_set[smith_lang_objc_set_size];
static uint64_t smith_lang_objc_emitted_set[smith_lang_objc_set_size];

typedef struct {
    char *text;
    size_t used;
    bool full;
} SmithLangObjcOut;

static void smith_lang_objc_put(SmithLangObjcOut *out, const char *text, size_t length) {
    if (out->used + length + 1 > smith_lang_objc_text_max) {
        out->full = true;
        return;
    }
    memcpy(out->text + out->used, text, length);
    out->used += length;
    out->text[out->used] = 0;
}

static void smith_lang_objc_puts(SmithLangObjcOut *out, const char *text) {
    smith_lang_objc_put(out, text, strlen(text));
}

// ---- The index of a header: where it is kept, and the compile that makes it

constexpr size_t smith_lang_objc_path_max = 1024;
constexpr size_t smith_lang_objc_args_max = 256;
constexpr size_t smith_lang_objc_env_max = 4096;

extern char **environ;

// The value of the first line of the host's facts that begins with `key` (`sysroot=`), or null.
static const char *smith_lang_objc_fact(const char *facts, const char *key, char *out, size_t capacity) {
    size_t key_length = strlen(key);
    for (const char *line = facts; *line != 0;) { // by line
        const char *end = strchr(line, '\n');
        size_t length = end != nullptr ? (size_t)(end - line) : strlen(line);
        if (length >= key_length && memcmp(line, key, key_length) == 0 && length - key_length < capacity) {
            memcpy(out, line + key_length, length - key_length);
            out[length - key_length] = 0;
            return out;
        }
        if (end == nullptr) break;
        line = end + 1;
    }
    return nullptr;
}

static bool smith_lang_objc_is_folder(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool smith_lang_objc_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

// Whether the SDK (`sysroot=`) or a -F folder (`framework=`) has the framework a header's first name says.
static bool smith_lang_objc_has_framework(const char *facts, const char *name, size_t length) {
    char path[smith_lang_objc_path_max];
    for (const char *line = facts; *line != 0;) { // by line
        const char *end = strchr(line, '\n');
        size_t size = end != nullptr ? (size_t)(end - line) : strlen(line);
        bool sdk = size > 8 && memcmp(line, "sysroot=", 8) == 0;
        if (sdk || (size > 10 && memcmp(line, "framework=", 10) == 0)) {
            int written = snprintf(path, sizeof path, "%.*s%s/%.*s.framework/Headers", (int)(size - (sdk ? 8 : 10)),
                                   line + (sdk ? 8 : 10), sdk ? "/System/Library/Frameworks" : "", (int)length, name);
            if (written > 0 && (size_t)written < sizeof path && smith_lang_objc_is_folder(path)) return true;
        }
        if (end == nullptr) break;
        line = end + 1;
    }
    return false;
}

static uint64_t smith_lang_objc_mix(uint64_t hash, const char *text, size_t length) {
    for (size_t i = 0; i < length; i++)
        hash = (hash ^ (unsigned char)text[i]) * 1099511628211ULL;
    return (hash ^ 0xff) * 1099511628211ULL; // a separator
}

typedef struct {
    char self[smith_lang_objc_path_max];
    char flags[16384];
    char *flag[smith_lang_objc_args_max];
    size_t flag_count;
} SmithLangObjcCompile;

// The path of the running binary (this code is linked into smith), for a process the driver did not start.
static bool smith_lang_objc_own_path(char *out) {
    char raw[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof raw;
    if (_NSGetExecutablePath(raw, &size) != 0) return false;
#else
    ssize_t length = readlink("/proc/self/exe", raw, sizeof raw - 1);
    if (length <= 0) return false;
    raw[length] = 0;
#endif
    char real[PATH_MAX];
    if (realpath(raw, real) == nullptr || strlen(real) >= smith_lang_objc_path_max) return false;
    snprintf(out, smith_lang_objc_path_max, "%s", real);
    return true;
}

// The flags the driver passed (SMITH_LANG_FLAGS, a line each; the standard is left out) and this binary.
static bool smith_lang_objc_compile_flags(SmithLangObjcCompile *compile) {
    const char *self = getenv("SMITH_LANG_SELF");
    const char *flags = getenv("SMITH_LANG_FLAGS");
    if (self != nullptr && strlen(self) < sizeof compile->self) {
        snprintf(compile->self, sizeof compile->self, "%s", self);
    } else if (!smith_lang_objc_own_path(compile->self)) { // clang-tidy and clangd: no driver set the variable
        return false;
    }
    snprintf(compile->flags, sizeof compile->flags, "%s", flags != nullptr ? flags : "");
    compile->flag_count = 0;
    for (char *line = compile->flags; *line != 0;) { // by line
        char *end = strchr(line, '\n');
        if (end != nullptr) *end = 0;
        if (*line != 0 && strncmp(line, "-std=", 5) != 0 && compile->flag_count < smith_lang_objc_args_max - 8)
            compile->flag[compile->flag_count++] = line;
        if (end == nullptr) break;
        line = end + 1;
    }
    return true;
}

// Runs this binary as `cc -x <language> -fsyntax-only -w <flags> <source>`, quietly, with SMITH_OBJC_INDEX and
// SMITH_OBJC_FRAMEWORK set when `folder` is (the plugin of shim/objc_index.cpp writes the index then). True if it
// passed.
static bool smith_lang_objc_run(const SmithLangObjcCompile *compile, const char *language, const char *source,
                                const char *folder, const char *framework) {
    static char *args[smith_lang_objc_args_max + 8];
    static char *env[smith_lang_objc_env_max];
    static char index_env[smith_lang_objc_path_max + 24];
    static char framework_env[256];
    size_t count = 0;
    args[count++] = (char *)compile->self;
    args[count++] = (char *)"cc";
    args[count++] = (char *)"-x";
    args[count++] = (char *)language;
    args[count++] = (char *)"-fsyntax-only";
    args[count++] = (char *)"-w";
    for (size_t i = 0; i < compile->flag_count; i++)
        args[count++] = compile->flag[i];
    args[count++] = (char *)source;
    args[count] = nullptr;
    size_t used = 0;
    for (char **entry = environ; entry != nullptr && *entry != nullptr && used < smith_lang_objc_env_max - 3; entry++) {
        if (strncmp(*entry, "SMITH_OBJC_", 11) != 0) env[used++] = *entry;
    }
    if (folder != nullptr) {
        snprintf(index_env, sizeof index_env, "SMITH_OBJC_INDEX=%s", folder);
        snprintf(framework_env, sizeof framework_env, "SMITH_OBJC_FRAMEWORK=%s", framework);
        env[used++] = index_env;
        env[used++] = framework_env;
    }
    env[used] = nullptr;
    posix_spawn_file_actions_t quiet;
    posix_spawn_file_actions_init(&quiet);
    posix_spawn_file_actions_addopen(&quiet, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&quiet, 2, "/dev/null", O_WRONLY, 0);
    pid_t child = 0;
    int started = posix_spawn(&child, compile->self, &quiet, nullptr, args, env);
    posix_spawn_file_actions_destroy(&quiet);
    int status = 0;
    return started == 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Writes `#import <header>` (or `#include`) to a file of the folder, for `run`.
static bool smith_lang_objc_write_source(const char *path, const char *directive, const char *header) {
    FILE *file = fopen(path, "w");
    if (file == nullptr) return false;
    fprintf(file, "#%s <%s>\n", directive, header);
    return fclose(file) == 0;
}

// A header that C can read stays a C header, whatever Objective-C is behind it (Network.framework has classes, and a C
// API that `import <Network/Network.h>;` is used for). The answer is kept in the folder: `c.yes` or `c.no`.
static bool smith_lang_objc_reads_as_c(const SmithLangObjcCompile *compile, const char *header, const char *folder,
                                       bool editor) {
    char path[smith_lang_objc_path_max];
    snprintf(path, sizeof path, "%s/c.yes", folder);
    if (smith_lang_objc_exists(path)) return true;
    char no[smith_lang_objc_path_max];
    snprintf(no, sizeof no, "%s/c.no", folder);
    if (smith_lang_objc_exists(no) || editor) return false;
    char source[smith_lang_objc_path_max];
    snprintf(source, sizeof source, "%s/import.%d.c", folder, (int)getpid());
    if (!smith_lang_objc_write_source(source, "include", header)) return false;
    bool reads = smith_lang_objc_run(compile, "c", source, nullptr, nullptr);
    unlink(source);
    int marker = open(reads ? path : no, O_WRONLY | O_CREAT, 0644);
    if (marker >= 0) close(marker);
    return reads;
}

// The folder for a header: .smith/objc/<key> of the working folder, made if it is missing. The key is the header, the
// SDK, the target, the flags and this binary, so a new SDK or a new smith makes the index again.
static bool smith_lang_objc_folder(const SmithLangObjcCompile *compile, const char *facts, const char *header,
                                   char *folder, size_t capacity) {
    char sysroot[smith_lang_objc_path_max] = "";
    char triple[128] = "";
    smith_lang_objc_fact(facts, "sysroot=", sysroot, sizeof sysroot);
    smith_lang_objc_fact(facts, "triple=", triple, sizeof triple);
    uint64_t key = smith_lang_objc_mix(1469598103934665603ULL, header, strlen(header));
    key = smith_lang_objc_mix(key, sysroot, strlen(sysroot));
    key = smith_lang_objc_mix(key, triple, strlen(triple));
    for (size_t i = 0; i < compile->flag_count; i++)
        key = smith_lang_objc_mix(key, compile->flag[i], strlen(compile->flag[i]));
    struct stat binary;
    if (stat(compile->self, &binary) == 0) {
        char identity[64];
        int length =
            snprintf(identity, sizeof identity, "%lld %lld", (long long)binary.st_size, (long long)binary.st_mtime);
        key = smith_lang_objc_mix(key, identity, length > 0 ? (size_t)length : 0);
    }
    char here[smith_lang_objc_path_max];
    if (getcwd(here, sizeof here) == nullptr) return false;
    int written = snprintf(folder, capacity, "%s/.smith/objc/%016llx", here, (unsigned long long)key);
    if (written <= 0 || (size_t)written >= capacity) return false;
    char step[smith_lang_objc_path_max];
    static const char *const parts[] = {"/.smith", "/.smith/objc"};
    for (size_t i = 0; i < 2; i++) {
        snprintf(step, sizeof step, "%s%s", here, parts[i]);
        if (mkdir(step, 0755) != 0 && errno != EEXIST) return false;
    }
    return (mkdir(folder, 0755) == 0 || errno == EEXIST);
}

// The index of a header: made if the folder has none (not in an editor), mapped read-only for the whole compile. False
// when the header is no Objective-C one: it is no framework's, has no class of the framework's own, or C reads it.
static bool smith_lang_objc_index(const char *facts, const char *header, size_t length, char *types, size_t capacity,
                                  const char **index, uint32_t *index_length) {
    const char *slash = memchr(header, '/', length);
    if (slash == nullptr || slash == header || !smith_lang_objc_has_framework(facts, header, (size_t)(slash - header)))
        return false;
    static SmithLangObjcCompile shared; // its flags are big, and a compile reads one header at a time
    SmithLangObjcCompile *compile = &shared;
    char editor[4];
    bool in_editor = smith_lang_objc_fact(facts, "editor=", editor, sizeof editor) != nullptr;
    char folder[smith_lang_objc_path_max];
    if (!smith_lang_objc_compile_flags(compile) ||
        !smith_lang_objc_folder(compile, facts, header, folder, sizeof folder))
        return false;
    char file[smith_lang_objc_path_max];
    snprintf(file, sizeof file, "%s/methods.idx", folder);
    if (!smith_lang_objc_exists(file)) {
        if (in_editor) return false;
        snprintf(file, sizeof file, "%s/import.%d.m", folder, (int)getpid());
        char framework[256];
        snprintf(framework, sizeof framework, "%.*s", (int)(slash - header), header);
        bool written = smith_lang_objc_write_source(file, "import", header);
        if (written) smith_lang_objc_run(compile, "objective-c", file, folder, framework);
        unlink(file);
        snprintf(file, sizeof file, "%s/methods.idx", folder);
    }
    int descriptor = open(file, O_RDONLY);
    struct stat info;
    if (descriptor < 0 || fstat(descriptor, &info) != 0 || info.st_size < 8) {
        if (descriptor >= 0) close(descriptor);
        return false;
    }
    void *mapped = mmap(nullptr, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, descriptor, 0);
    close(descriptor);
    if (mapped == MAP_FAILED || memcmp(mapped, "objc\t1\n", 7) != 0) return false;
    int made = snprintf(types, capacity, "%s/types.h", folder);
    if (made <= 0 || (size_t)made >= capacity || !smith_lang_objc_exists(types)) return false;
    if (smith_lang_objc_reads_as_c(compile, header, folder, in_editor)) return false;
    *index = mapped;
    *index_length = (uint32_t)info.st_size;
    return true;
}

// ---- The import

// A framework header is an Objective-C one when the compile of it found classes C cannot read. Returns whether this
// import was taken: then the header is not included as C, and `objc::` names what it declares.
static bool smith_lang_objc_import(SmithLang *lang, const SmithHost *host, const char *header, size_t length,
                                   uint32_t location) {
    if (host->objc_facts == nullptr || lang->objc.count >= smith_lang_objc_max) return false;
    SmithLangObjcIndex *entry = &lang->objc.indexes[lang->objc.count];
    if (!smith_lang_objc_index(host->objc_facts(host->context), header, length, entry->types, sizeof entry->types,
                               &entry->index, &entry->length))
        return false;
    lang->objc.count += 1;
    const char *slash = memchr(header, '/', length);
    size_t framework = slash != nullptr ? (size_t)(slash - header) : length;
    bool known = false;
    for (uint32_t i = 0; i < lang->objc.frameworks_count; i++)
        known = known || (strlen(lang->objc.frameworks[i]) == framework &&
                          memcmp(lang->objc.frameworks[i], header, framework) == 0);
    if (!known && framework < sizeof lang->objc.frameworks[0] && lang->objc.frameworks_count < smith_lang_objc_max) {
        memcpy(lang->objc.frameworks[lang->objc.frameworks_count], header, framework);
        lang->objc.frameworks[lang->objc.frameworks_count++][framework] = 0;
    }
    if (!lang->objc.bound) {
        lang->objc.bound = true;
        if (smith_lang_add_binding(lang, host, "objc", 4, location, nullptr, false))
            smith_lang_binding_find(lang, "objc", 4)->is_objc = true;
    }
    if (host->watch_mode != nullptr) host->watch_mode(host->context, SmithWatch_All);
    return true;
}

// ---- Names: objc::X and objc::Class::method

// `objc::X` is `objc_X`; `objc::Class::method` is `objc_Class_method`. `taken` is the name after the first `::`.
static void smith_lang_objc_name(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                 const SmithToken *taken, SmithToken *result) {
    size_t total = 0;
    static const char prefix[] = "objc_";
    if (sizeof prefix - 1 + taken->length + 1 > sizeof lang->tmp) {
        lang->broken = true;
        return;
    }
    memcpy(lang->tmp, prefix, sizeof prefix - 1);
    memcpy(lang->tmp + sizeof prefix - 1, taken->text, taken->length);
    total = sizeof prefix - 1 + taken->length;
    const SmithToken *colons = smith_lang_peek(lang, host, 0, false);
    const SmithToken *method = smith_lang_peek(lang, host, 1, false);
    if (smith_lang_punct(colons, "::") && method->kind == SmithTokenKind_Identifier && !method->foreign &&
        total + 1 + method->length + 1 <= sizeof lang->tmp) {
        smith_lang_take(lang, host);
        SmithToken member = smith_lang_take(lang, host);
        lang->tmp[total++] = '_';
        memcpy(lang->tmp + total, member.text, member.length);
        total += member.length;
    }
    lang->tmp[total] = 0;
    *result = smith_lang_made(name, lang->tmp, total, false);
    result->location = taken->location;
}

// ---- The index

typedef struct {
    const char *start;
    const char *end;
} SmithLangObjcSpan;

typedef struct {
    char kind; // 'c' or 'p'
    char name[96];
} SmithLangObjcOwner;

typedef struct {
    SmithLangObjcOwner owners[smith_lang_objc_owners_max];
    size_t count;
} SmithLangObjcChain;

static bool smith_lang_objc_has_owner(const SmithLangObjcChain *chain, char kind, const char *name, size_t length) {
    for (size_t i = 0; i < chain->count; i++) {
        if (chain->owners[i].kind == kind && strlen(chain->owners[i].name) == length &&
            memcmp(chain->owners[i].name, name, length) == 0)
            return true;
    }
    return false;
}

static void smith_lang_objc_add_owner(SmithLangObjcChain *chain, char kind, const char *name, size_t length) {
    if (chain->count >= smith_lang_objc_owners_max || length >= sizeof chain->owners[0].name ||
        smith_lang_objc_has_owner(chain, kind, name, length))
        return;
    chain->owners[chain->count].kind = kind;
    memcpy(chain->owners[chain->count].name, name, length);
    chain->owners[chain->count++].name[length] = 0;
}

// The blocks of the indexes that begin with a `c` or `p` line for this name; a block ends where the next begins.
static size_t smith_lang_objc_blocks(const SmithLang *lang, char kind, const char *name, size_t length,
                                     SmithLangObjcSpan *spans, size_t capacity) {
    char key[128];
    if (length + 5 > sizeof key) return 0;
    key[0] = '\n';
    key[1] = kind;
    key[2] = '\t';
    memcpy(key + 3, name, length);
    key[3 + length] = '\t';
    size_t found = 0;
    for (uint32_t i = 0; i < lang->objc.count && found < capacity; i++) {
        const char *text = lang->objc.indexes[i].index;
        const char *end = text + lang->objc.indexes[i].length;
        const char *at = memmem(text, (size_t)(end - text), key, length + 4);
        if (at == nullptr) continue;
        const char *line = at + 1;
        const char *stop = line;
        for (size_t guard = 0; stop < end && guard < (size_t)(end - text); guard++) { // by line
            const char *newline = memchr(stop, '\n', (size_t)(end - stop));
            if (newline == nullptr) {
                stop = end;
                break;
            }
            stop = newline + 1;
            if (stop < end && (*stop == 'c' || *stop == 'p')) break;
        }
        spans[found++] = (SmithLangObjcSpan){line, stop};
    }
    return found;
}

// The next tab-separated field of a line: its start and length, advancing `at`. False at the end of the line.
static bool smith_lang_objc_field(const char **at, const char *end, const char **field, size_t *length) {
    if (*at >= end || **at == '\n') return false;
    const char *start = *at;
    const char *stop = start;
    while (stop < end && *stop != '\t' && *stop != '\n')
        stop += 1;
    *field = start;
    *length = (size_t)(stop - start);
    *at = (stop < end && *stop == '\t') ? stop + 1 : stop;
    return true;
}

// Adds the comma-separated names to the chain as owners of this kind.
static void smith_lang_objc_add_list(SmithLangObjcChain *chain, char kind, const char *list, size_t length) {
    size_t start = 0;
    for (size_t i = 0; i <= length; i++) { // by character
        if (i < length && list[i] != ',') continue;
        if (i > start) smith_lang_objc_add_owner(chain, kind, list + start, i - start);
        start = i + 1;
    }
}

// The chain of a class or protocol: itself, then (breadth first) the protocols and superclass of each.
static void smith_lang_objc_chain(const SmithLang *lang, char kind, const char *name, size_t length,
                                  SmithLangObjcChain *chain) {
    chain->count = 0;
    smith_lang_objc_add_owner(chain, kind, name, length);
    for (size_t at = 0; at < chain->count; at++) { // the chain grows as it is read
        SmithLangObjcSpan spans[8];
        char owner_kind = chain->owners[at].kind;
        size_t owner_length = strlen(chain->owners[at].name);
        size_t count = smith_lang_objc_blocks(lang, owner_kind, chain->owners[at].name, owner_length, spans, 8);
        for (size_t s = 0; s < count; s++) {
            const char *line = spans[s].start;
            for (size_t guard = 0; line < spans[s].end && guard < 100000; guard++) { // by line
                const char *newline = memchr(line, '\n', (size_t)(spans[s].end - line));
                const char *end = newline != nullptr ? newline : spans[s].end;
                if (*line == 'm') break; // the methods follow the head and the `a` lines
                const char *cursor = line + 2;
                const char *field = nullptr;
                size_t field_length = 0;
                smith_lang_objc_field(&cursor, end, &field, &field_length); // the name
                if (*line == 'c') {
                    if (smith_lang_objc_field(&cursor, end, &field, &field_length) && field_length > 0 &&
                        field[0] != '-')
                        smith_lang_objc_add_owner(chain, 'c', field, field_length);
                    if (smith_lang_objc_field(&cursor, end, &field, &field_length))
                        smith_lang_objc_add_list(chain, 'p', field, field_length);
                } else if (*line == 'p' || *line == 'a') {
                    if (smith_lang_objc_field(&cursor, end, &field, &field_length))
                        smith_lang_objc_add_list(chain, 'p', field, field_length);
                }
                if (newline == nullptr) break;
                line = newline + 1;
            }
        }
    }
}

// ---- Sets of names (a hash of each, open addressing in a table of the file's own)

static uint64_t smith_lang_objc_hash(const char *text, size_t length) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < length; i++)
        hash = (hash ^ (unsigned char)text[i]) * 1099511628211ULL;
    return hash | 1;
}

// Adds the name; returns whether it was there already.
static bool smith_lang_objc_set_add(uint64_t *set, const char *text, size_t length) {
    uint64_t hash = smith_lang_objc_hash(text, length);
    for (size_t probe = 0; probe < smith_lang_objc_set_size; probe++) { // bounded by the table
        size_t slot = (size_t)((hash + probe) & (smith_lang_objc_set_size - 1));
        if (set[slot] == hash) return true;
        if (set[slot] == 0) {
            set[slot] = hash;
            return false;
        }
    }
    return true; // full: no more wrappers
}

static bool smith_lang_objc_set_has(const uint64_t *set, const char *text, size_t length) {
    uint64_t hash = smith_lang_objc_hash(text, length);
    for (size_t probe = 0; probe < smith_lang_objc_set_size; probe++) { // bounded by the table
        size_t slot = (size_t)((hash + probe) & (smith_lang_objc_set_size - 1));
        if (set[slot] == hash) return true;
        if (set[slot] == 0) return false;
    }
    return false;
}

// ---- The wrappers

typedef struct {
    const char *kind; // "+" or "-"
    const char *name;
    size_t name_length;
    const char *selector;
    size_t selector_length;
    const char *flags;
    size_t flags_length;
    const char *types[32]; // the result, then the parameters
    size_t type_lengths[32];
    size_t type_count;
} SmithLangObjcMethod;

// A line `m owner kind name selector flags result params...`.
static bool smith_lang_objc_method(const char *line, const char *end, SmithLangObjcMethod *method) {
    const char *at = line + 2;
    const char *field = nullptr;
    size_t length = 0;
    *method = (SmithLangObjcMethod){};
    if (!smith_lang_objc_field(&at, end, &field, &length)) return false; // the owner
    if (!smith_lang_objc_field(&at, end, &field, &length)) return false;
    method->kind = field;
    if (!smith_lang_objc_field(&at, end, &method->name, &method->name_length)) return false;
    if (!smith_lang_objc_field(&at, end, &method->selector, &method->selector_length)) return false;
    if (!smith_lang_objc_field(&at, end, &method->flags, &method->flags_length)) return false;
    while (method->type_count < 32 && smith_lang_objc_field(&at, end, &method->types[method->type_count],
                                                            &method->type_lengths[method->type_count]))
        method->type_count += 1;
    return method->type_count > 0;
}

static void smith_lang_objc_type(SmithLangObjcOut *out, const char *type, size_t length, const char *self,
                                 size_t self_length) {
    if (length == 5 && memcmp(type, "@self", 5) == 0) {
        smith_lang_objc_puts(out, "objc_");
        smith_lang_objc_put(out, self, self_length);
        smith_lang_objc_puts(out, " *");
        return;
    }
    smith_lang_objc_put(out, type, length);
}

// One wrapper. `class_method`: the receiver is the class, found by name once.
static void smith_lang_objc_wrapper(SmithLangObjcOut *out, const char *self, size_t self_length,
                                    const SmithLangObjcMethod *method, bool class_method, bool renamed) {
    const char *send = "smith_objc_send";
    if (memchr(method->flags, 's', method->flags_length) != nullptr) send = "smith_objc_send_stret";
    if (memchr(method->flags, 'f', method->flags_length) != nullptr) send = "smith_objc_send_fpret";
    bool is_void = method->type_lengths[0] == 4 && memcmp(method->types[0], "void", 4) == 0;
    smith_lang_objc_puts(out, "static inline ");
    smith_lang_objc_type(out, method->types[0], method->type_lengths[0], self, self_length);
    smith_lang_objc_puts(out, " objc_");
    smith_lang_objc_put(out, self, self_length);
    smith_lang_objc_puts(out, renamed ? "_class_" : "_");
    smith_lang_objc_put(out, method->name, method->name_length);
    smith_lang_objc_puts(out, "(");
    if (class_method) {
        if (method->type_count == 1) smith_lang_objc_puts(out, "void");
    } else {
        smith_lang_objc_puts(out, "objc_");
        smith_lang_objc_put(out, self, self_length);
        smith_lang_objc_puts(out, " *self");
    }
    char number[16];
    for (size_t i = 1; i < method->type_count; i++) {
        smith_lang_objc_puts(out, class_method && i == 1 ? "" : ", ");
        smith_lang_objc_type(out, method->types[i], method->type_lengths[i], self, self_length);
        snprintf(number, sizeof number, " a%zu", i);
        smith_lang_objc_puts(out, number);
    }
    // No `static` cache of the selector: a write to one is a `globals` effect, and a call of Objective-C is `c` and
    // nothing more (sel_registerName looks the name up, in a table, each time). A class is the framework's own symbol,
    // as Objective-C's compiler names it: the reference also keeps the framework linked, where an automatic link of a
    // framework nothing refers to is dropped, and the class would not be found by name.
    smith_lang_objc_puts(out, ") { smith_objc_sel selector = smith_objc_selector(\"");
    smith_lang_objc_put(out, method->selector, method->selector_length);
    smith_lang_objc_puts(out, "\");");
    if (class_method) {
        smith_lang_objc_puts(out, " smith_objc_id receiver = (smith_objc_id)&smith_objc_class_symbol_");
        smith_lang_objc_put(out, self, self_length);
        smith_lang_objc_puts(out, ";");
    }
    smith_lang_objc_puts(out, is_void ? " ((" : " return ((");
    smith_lang_objc_type(out, method->types[0], method->type_lengths[0], self, self_length);
    smith_lang_objc_puts(out, class_method ? " (*)(smith_objc_id, smith_objc_sel" : " (*)(");
    if (!class_method) {
        smith_lang_objc_puts(out, "objc_");
        smith_lang_objc_put(out, self, self_length);
        smith_lang_objc_puts(out, " *, smith_objc_sel");
    }
    for (size_t i = 1; i < method->type_count; i++) {
        smith_lang_objc_puts(out, ", ");
        smith_lang_objc_type(out, method->types[i], method->type_lengths[i], self, self_length);
    }
    smith_lang_objc_puts(out, "))");
    smith_lang_objc_puts(out, send);
    smith_lang_objc_puts(out, class_method ? ")(receiver, selector" : ")(self, selector");
    for (size_t i = 1; i < method->type_count; i++) {
        snprintf(number, sizeof number, ", a%zu", i);
        smith_lang_objc_puts(out, number);
    }
    smith_lang_objc_puts(out, "); }\n");
}

// Calls `emit` shape: pass 1 collects the instance names of the chain, pass 2 writes the wrappers.
static void smith_lang_objc_class(const SmithLang *lang, SmithLangObjcOut *out, char kind, const char *name,
                                  size_t length) {
    static SmithLangObjcChain chain;
    smith_lang_objc_chain(lang, kind, name, length, &chain);
    memset(smith_lang_objc_instance_set, 0, sizeof smith_lang_objc_instance_set);
    memset(smith_lang_objc_emitted_set, 0, sizeof smith_lang_objc_emitted_set);
    if (kind == 'c') {
        smith_lang_objc_puts(out, "extern char smith_objc_class_symbol_");
        smith_lang_objc_put(out, name, length);
        smith_lang_objc_puts(out, " __asm__(\"_OBJC_CLASS_$_");
        smith_lang_objc_put(out, name, length);
        smith_lang_objc_puts(out, "\");\n");
    }
    for (int pass = 0; pass < 2; pass++) {
        for (size_t o = 0; o < chain.count; o++) {
            SmithLangObjcSpan spans[8];
            size_t count = smith_lang_objc_blocks(lang, chain.owners[o].kind, chain.owners[o].name,
                                                  strlen(chain.owners[o].name), spans, 8);
            for (size_t s = 0; s < count; s++) {
                const char *line = spans[s].start;
                for (size_t guard = 0; line < spans[s].end && guard < 1000000; guard++) { // by line
                    const char *newline = memchr(line, '\n', (size_t)(spans[s].end - line));
                    const char *end = newline != nullptr ? newline : spans[s].end;
                    SmithLangObjcMethod method;
                    if (*line == 'm' && smith_lang_objc_method(line, end, &method)) {
                        bool instance = method.kind[0] == '-';
                        if (pass == 0 && instance) {
                            smith_lang_objc_set_add(smith_lang_objc_instance_set, method.name, method.name_length);
                        } else if (pass == 1 && (instance || kind == 'c')) {
                            bool renamed = !instance && smith_lang_objc_set_has(smith_lang_objc_instance_set,
                                                                                method.name, method.name_length);
                            char key[200];
                            int key_length = snprintf(key, sizeof key, "%c%.*s",
                                                      renamed    ? 'r'
                                                      : instance ? 'i'
                                                                 : 'k',
                                                      (int)method.name_length, method.name);
                            if (key_length > 0 && (size_t)key_length < sizeof key &&
                                !smith_lang_objc_set_add(smith_lang_objc_emitted_set, key, (size_t)key_length))
                                smith_lang_objc_wrapper(out, name, length, &method, !instance, renamed);
                        }
                    }
                    if (newline == nullptr) break;
                    line = newline + 1;
                }
            }
        }
    }
}

static bool smith_lang_objc_word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// The names after `objc::` in the file's text: the classes and protocols whose methods are written out.
static size_t smith_lang_objc_mentions(const SmithHost *host, char names[][96], size_t capacity) {
    size_t count = 0;
    const char *text = host->main_text;
    size_t length = host->main_text_length;
    static const char marker[] = "objc::";
    if (text == nullptr) return 0;
    for (const char *at = memmem(text, length, marker, sizeof marker - 1); at != nullptr && count < capacity;
         at = memmem(at + 1, (size_t)(text + length - at - 1), marker, sizeof marker - 1)) { // one pass per use
        if (at > text && (smith_lang_objc_word(at[-1]))) continue;
        const char *start = at + sizeof marker - 1;
        size_t size = 0;
        while (start + size < text + length && (smith_lang_objc_word(start[size])))
            size += 1;
        if (size == 0 || size >= 96) continue;
        bool known = false;
        for (size_t i = 0; i < count; i++)
            known = known || (strlen(names[i]) == size && memcmp(names[i], start, size) == 0);
        if (known) continue;
        memcpy(names[count], start, size);
        names[count++][size] = 0;
    }
    return count;
}

// At the last import: the types of each index, then the wrappers and the linker options.
static void smith_lang_objc_define(SmithLang *lang, const SmithHost *host) {
    if (lang->objc.count == 0 || lang->objc.defined || host->include_text == nullptr) return;
    lang->objc.defined = true;
    for (uint32_t i = 0; i < lang->objc.count; i++)
        host->include(host->context, lang->objc.indexes[i].types, false);
    SmithLangObjcOut out = {smith_lang_objc_text, 0, false};
    smith_lang_objc_text[0] = 0;
    for (uint32_t i = 0; i < lang->objc.frameworks_count; i++) {
        smith_lang_objc_puts(&out, "__asm__(\".linker_option \\\"-framework\\\", \\\"");
        smith_lang_objc_puts(&out, lang->objc.frameworks[i]);
        smith_lang_objc_puts(&out, "\\\"\");\n");
    }
    smith_lang_objc_puts(&out, "__asm__(\".linker_option \\\"-lobjc\\\"\");\n");
    static char names[smith_lang_objc_names_max][96];
    size_t count = smith_lang_objc_mentions(host, names, smith_lang_objc_names_max);
    for (size_t i = 0; i < count; i++) {
        SmithLangObjcSpan spans[1];
        size_t length = strlen(names[i]);
        if (smith_lang_objc_blocks(lang, 'c', names[i], length, spans, 1) > 0)
            smith_lang_objc_class(lang, &out, 'c', names[i], length);
        else if (smith_lang_objc_blocks(lang, 'p', names[i], length, spans, 1) > 0)
            smith_lang_objc_class(lang, &out, 'p', names[i], length);
    }
    if (out.full) lang->broken = true;
    host->include_text(host->context, out.text, (uint32_t)out.used);
}

// ---- `objc::` in a module's generated header (issue #601)
// ----------------------------------------------------------------------------------------
// The header pass (`smith cc --smith-header`) reads a module with no compiler, so it has no index to ask. A module's
// export may name a class or protocol of an imported framework (`objc::MTLDevice *metal;`), and the header must be C:
// `objc::X` is written `objc_X` (the name the index gives it), and each X used through a pointer is declared opaque in
// the header, `typedef struct objc_X objc_X;`, the same typedef the importer's `types.h` has (C repeats a typedef).
// A record or an enum used by value needs the index: the importer must import the framework itself.

constexpr size_t smith_lang_objc_header_names_max = 64;

static bool smith_lang_objc_header_word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Whether `name` is already in the list.
static bool smith_lang_objc_header_has(char names[][96], size_t count, const char *name, size_t length) {
    for (size_t i = 0; i < count; i++)
        if (strlen(names[i]) == length && memcmp(names[i], name, length) == 0) return true;
    return false;
}

// Rewrites each `objc::X` of the header body to `objc_X` and collects the X that a pointer uses. Returns the count.
static size_t smith_lang_objc_header_rename(char *text, size_t *used, char names[][96], size_t capacity) {
    size_t count = 0;
    size_t out = 0;
    size_t length = *used;
    for (size_t i = 0; i < length; i++) {                                        // bounded by the body
        bool boundary = out == 0 || !smith_lang_objc_header_word(text[out - 1]); // the output holds what came before
        if (!boundary || i + 6 >= length || memcmp(text + i, "objc::", 6) != 0 ||
            !smith_lang_objc_header_word(text[i + 6])) {
            text[out++] = text[i];
            continue;
        }
        size_t end = i + 6;
        while (end < length && smith_lang_objc_header_word(text[end])) // the class name
            end += 1;
        size_t after = end;
        while (after < length && text[after] == ' ')
            after += 1;
        size_t name_length = end - (i + 6);
        bool pointer = after < length && text[after] == '*';
        if (pointer && name_length < sizeof names[0] && count < capacity &&
            !smith_lang_objc_header_has(names, count, text + i + 6, name_length)) {
            memcpy(names[count], text + i + 6, name_length);
            names[count++][name_length] = 0;
        }
        memcpy(text + out, "objc_", 5);
        out += 5;
        i += 5; // the name follows on the next steps
    }
    *used = out;
    return count;
}

// The hook of the header writer: the body of a module's header, made C. Declarations go on the first declaration's
// line, so no line moves.
static void smith_lang_objc_header(SmithLang *lang, char *body, size_t capacity) {
    if (memmem(body, lang->header_used, "objc::", 6) == nullptr) return;
    static char names[smith_lang_objc_header_names_max][96];
    size_t count = smith_lang_objc_header_rename(body, &lang->header_used, names, smith_lang_objc_header_names_max);
    static char declarations[smith_lang_objc_header_names_max * 128];
    size_t used = 0;
    for (size_t i = 0; i < count; i++)
        used += (size_t)snprintf(declarations + used, sizeof declarations - used, "typedef struct objc_%s objc_%s; ",
                                 names[i], names[i]);
    size_t blank = 0;
    while (blank < lang->header_used && body[blank] == '\n')
        blank += 1;
    if (used == 0 || lang->header_used + used >= capacity) return;
    memmove(body + blank + used, body + blank, lang->header_used - blank);
    memcpy(body + blank, declarations, used);
    lang->header_used += used;
}
