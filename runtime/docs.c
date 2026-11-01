// `smith docs`: checks a project's documents, knowledge/ (an OKF bundle) and .agents/skills/, and prints
// each problem on stderr as path: message, in the order of the rules below, then a summary on stdout. It exits 1 if
// there was a problem. One file, included by smith.c; it allocates nothing itself (its memory is two mapped regions,
// used as arenas). Ported from virtual-cell's docs_check; the rules, the fixtures and the skill smith-docs describe it.
//
//   smith docs [--type <name>]... [--require <type>:<field>[=<value>,...]]...
//              [--depfile <path> --target <path>] [<root>]       root is the project, "." by default
//
// - Every concept, knowledge/<name>.md other than index.md and log.md, has a kebab-case file name and frontmatter
//   holding type, title and description, and its type is one the project allows: `types:` in the frontmatter of
//   knowledge/index.md (or --type, which replaces it; without either convention, architecture, contract and design).
//   `requires:` there (or --require) makes a field a must for a type, optionally from a list of values.
// - knowledge/index.md has okf_version in its frontmatter and lists exactly the concepts on disk, each as a list item
//   "- [Title](name.md): description" whose description is the concept's own, word for word.
// - Every skill, .agents/skills/<name>/SKILL.md, has frontmatter with a name matching its folder and a description.
// - .claude/skills, when there are skills, is a symlink to ../.agents/skills.
// - .claude-plugin/plugin.json, when it lists skills, lists exactly the skills that are not named <plugin>-dev or
//   <plugin>-dev-* (the plugin's name is its "name"): those are for changing the project and never ship.
// - Every relative link in knowledge/ and .agents/skills/ resolves to a file or folder (links inside code
//   are ignored; #anchors are not checked), and a document of knowledge/ never links a skill.
// - No document has a merge-conflict marker.
//
// With --depfile, the check also writes <path> in the form of a compiler's -MF: "<target>: <file>..." naming, once
// each and as root/relative, every file it read or found to exist: the documents, the skills, and what each link
// points at. A file that is not there is not named, since the check then has a problem and builds no stamp. A space is
// written as "\ ", # as "\#" and $ as "$$". A build.c step reruns the check when one of them changes.
#include <dirent.h>
#include <stdint.h>
#include <sys/mman.h>

constexpr size_t smith_docs_memory_size = (size_t)256 << 20; // reserved, and touched as used
constexpr size_t smith_docs_depends_size = (size_t)16 << 20;
constexpr uint32_t smith_docs_fields_max = 64;
constexpr uint32_t smith_docs_entries_max = 4096;  // names in one folder
constexpr uint32_t smith_docs_paths_max = 4096;    // documents under one folder, and folders still to read
constexpr uint32_t smith_docs_listed_max = 4096;   // index items
constexpr uint32_t smith_docs_depends_max = 16384; // distinct files one run may name in the depfile
constexpr uint32_t smith_docs_types_max = 32;
constexpr uint32_t smith_docs_requires_max = 16;
constexpr size_t smith_docs_path_capacity = 1024;
constexpr uint64_t smith_docs_fence_length = 3;
constexpr int smith_docs_status_problems = 1;

static const char *const smith_docs_default_types[] = {"convention", "architecture", "contract", "design"};
static const char *const smith_docs_required[] = {"type", "title", "description"};

typedef struct {
    const char *begin; // nullptr: not there
    uint64_t length;
} SmithDocsText;

typedef struct {
    SmithDocsText key;
    SmithDocsText value;
} SmithDocsField;

// The top-level key: value pairs of a document's frontmatter. A value with no begin is a field that isn't there.
typedef struct {
    SmithDocsField fields[smith_docs_fields_max];
    uint32_t count;
} SmithDocsFields;

typedef struct {
    char *name;
    bool directory;
} SmithDocsEntry;

// An index item: the file it links to and the description after it.
typedef struct {
    SmithDocsText target;
    SmithDocsText description;
} SmithDocsItem;

// A field a type must have, from a list of values when there is one (comma-separated; no begin: any value).
typedef struct {
    SmithDocsText type;
    SmithDocsText field;
    SmithDocsText values;
} SmithDocsRequirement;

// A bump allocator over a mapped region. A scope gives back what was taken after it began.
typedef struct {
    char *base;
    size_t size;
    size_t used;
} SmithDocsArena;

typedef struct {
    SmithDocsArena arena;   // everything the run reads
    SmithDocsArena depends; // the paths for the depfile, which a scope of arena never frees
    const char *root;       // the project, as given
    FILE *errors;           // where the problems go: stderr, or the buffer of smith_core_docs_check
    const char *types[smith_docs_types_max];
    uint32_t type_count;
    SmithDocsRequirement requirements[smith_docs_requires_max];
    uint32_t requirement_count;
    const char **depended;
    uint32_t depended_count;
    uint32_t problems;
    uint32_t concepts;
} SmithDocs;

static void *smith_docs_push(SmithDocsArena *arena, size_t bytes) {
    constexpr size_t align = 16;
    size_t start = (arena->used + align - 1) & ~(align - 1);
    if (bytes > arena->size || start > arena->size - bytes) smith_die(smith_exit_usage, "docs: out of memory", nullptr);
    arena->used = start + bytes;
    return arena->base + start;
}

#define SMITH_DOCS_PUSH(arena, type, count) ((type *)smith_docs_push((arena), sizeof(type) * (count)))

[[gnu::format(printf, 2, 3)]] static void smith_docs_report(SmithDocs *docs, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vfprintf(docs->errors, format, arguments);
    va_end(arguments);
    fputc('\n', docs->errors);
    docs->problems += 1;
}

// ---- Text

static SmithDocsText smith_docs_text(const char *string) {
    return (SmithDocsText){string, strlen(string)};
}

static bool smith_docs_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

static SmithDocsText smith_docs_strip(SmithDocsText text) {
    while (text.length > 0 && smith_docs_is_space(text.begin[0])) {
        text.begin += 1;
        text.length -= 1;
    }
    while (text.length > 0 && smith_docs_is_space(text.begin[text.length - 1])) {
        text.length -= 1;
    }
    return text;
}

static bool smith_docs_same(SmithDocsText a, SmithDocsText b) {
    return a.length == b.length && (a.length == 0 || memcmp(a.begin, b.begin, a.length) == 0);
}

static bool smith_docs_is(SmithDocsText text, const char *string) {
    return text.begin != nullptr && smith_docs_same(text, smith_docs_text(string));
}

static bool smith_docs_starts(SmithDocsText text, const char *prefix) {
    SmithDocsText wanted = smith_docs_text(prefix);
    return text.length >= wanted.length && smith_docs_same((SmithDocsText){text.begin, wanted.length}, wanted);
}

static bool smith_docs_ends(SmithDocsText text, const char *suffix) {
    SmithDocsText wanted = smith_docs_text(suffix);
    return text.length >= wanted.length &&
           smith_docs_same((SmithDocsText){text.begin + text.length - wanted.length, wanted.length}, wanted);
}

// Whether text is not the empty field and is one of the strings.
static bool smith_docs_in(SmithDocsText text, const char *const *strings, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (smith_docs_is(text, strings[i])) return true;
    }
    return false;
}

// Takes the next line off text without its newline or carriage return.
static bool smith_docs_line(SmithDocsText *text, SmithDocsText *line) {
    if (text->length == 0) return false;
    uint64_t length = 0;
    while (length < text->length && text->begin[length] != '\n') {
        length += 1;
    }
    *line = (SmithDocsText){text->begin, length};
    uint64_t used = length < text->length ? length + 1 : length;
    text->begin += used;
    text->length -= used;
    if (line->length > 0 && line->begin[line->length - 1] == '\r') line->length -= 1;
    return true;
}

// ---- Files

// Remembers a path that was read or found, once, for the depfile. Past the bound the run stops: a depfile that left a
// file out would not rerun the check when that file changes.
static void smith_docs_depend(SmithDocs *docs, const char *path) {
    for (uint32_t i = 0; i < docs->depended_count; i++) {
        if (strcmp(docs->depended[i], path) == 0) return;
    }
    if (docs->depended_count == smith_docs_depends_max)
        smith_die(smith_exit_usage, "docs: too many files to name", nullptr);
    size_t length = strlen(path);
    char *copy = SMITH_DOCS_PUSH(&docs->depends, char, length + 1);
    memcpy(copy, path, length + 1);
    docs->depended[docs->depended_count++] = copy;
}

static void smith_docs_path(const SmithDocs *docs, const char *relative, char *path) {
    int written = snprintf(path, smith_docs_path_capacity, "%s/%s", docs->root, relative);
    if (written < 0 || (size_t)written >= smith_docs_path_capacity)
        smith_die(smith_exit_usage, "docs: path too long", relative);
}

// The file's bytes in the arena, or an empty text with no begin if it can't be read.
static SmithDocsText smith_docs_read(SmithDocs *docs, const char *relative) {
    char path[smith_docs_path_capacity];
    smith_docs_path(docs, relative, path);
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return (SmithDocsText){};
    smith_docs_depend(docs, path);
    struct stat status;
    if (fstat(fileno(file), &status) != 0 || status.st_size < 0) {
        fclose(file);
        return (SmithDocsText){};
    }
    uint64_t size = (uint64_t)status.st_size;
    char *bytes = SMITH_DOCS_PUSH(&docs->arena, char, size + 1);
    uint64_t got = fread(bytes, 1, size, file);
    fclose(file);
    return got == size ? (SmithDocsText){bytes, size} : (SmithDocsText){};
}

// Whether the path exists, and if so, named in the depfile.
static bool smith_docs_stat(SmithDocs *docs, const char *path) {
    struct stat status;
    if (stat(path, &status) != 0) return false;
    smith_docs_depend(docs, path);
    return true;
}

static bool smith_docs_exists(SmithDocs *docs, const char *relative) {
    char path[smith_docs_path_capacity];
    smith_docs_path(docs, relative, path);
    return smith_docs_stat(docs, path);
}

// <target>: then each path, escaped for a depfile, as the compiler writes -MF.
static bool smith_docs_write_depfile(const SmithDocs *docs, const char *depfile, const char *target) {
    FILE *file = fopen(depfile, "w");
    if (file == nullptr) return false;
    fprintf(file, "%s:", target);
    for (uint32_t i = 0; i < docs->depended_count; i++) {
        fputs(" ", file);
        for (const char *c = docs->depended[i]; *c != '\0'; c++) {
            if (*c == ' ' || *c == '#') fputc('\\', file);
            if (*c == '$') fputc('$', file);
            fputc(*c, file);
        }
    }
    fputc('\n', file);
    return fclose(file) == 0;
}

static char *smith_docs_copy(SmithDocs *docs, const char *string, uint64_t length) {
    char *copy = SMITH_DOCS_PUSH(&docs->arena, char, length + 1);
    memcpy(copy, string, length);
    return copy;
}

// Whether the entry is a folder; a link or an unknown type is looked at through stat.
static bool smith_docs_is_folder(const char *folder_path, const struct dirent *found) {
    if (found->d_type != DT_LNK && found->d_type != DT_UNKNOWN) return found->d_type == DT_DIR;
    char entry_path[smith_docs_path_capacity];
    int written = snprintf(entry_path, sizeof entry_path, "%s/%s", folder_path, found->d_name);
    if (written < 0 || (size_t)written >= sizeof entry_path) return false;
    struct stat status;
    return stat(entry_path, &status) == 0 && S_ISDIR(status.st_mode);
}

// The names in a folder (relative to the root), sorted by bytes, with whether each is a folder. Zero if it can't open.
static uint32_t smith_docs_list(SmithDocs *docs, const char *relative, SmithDocsEntry *entries) {
    char path[smith_docs_path_capacity];
    smith_docs_path(docs, relative, path);
    DIR *directory = opendir(path);
    if (directory == nullptr) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < smith_docs_entries_max * 2 && count < smith_docs_entries_max; i++) {
        struct dirent *found = readdir(directory);
        if (found == nullptr) break;
        if (strcmp(found->d_name, ".") == 0 || strcmp(found->d_name, "..") == 0) continue;
        SmithDocsEntry entry = {smith_docs_copy(docs, found->d_name, strlen(found->d_name)),
                                smith_docs_is_folder(path, found)};
        uint32_t at = count; // an insertion sort: the folder is small
        for (; at > 0 && strcmp(entries[at - 1].name, entry.name) > 0; at--) {
            entries[at] = entries[at - 1];
        }
        entries[at] = entry;
        count += 1;
    }
    closedir(directory);
    return count;
}

// Whether the path comes before the other, comparing folder and file names one at a time.
static bool smith_docs_path_less(const char *a, const char *b) {
    for (uint32_t i = 0; i < smith_docs_path_capacity; i++) {
        const char *end_a = strchr(a, '/');
        const char *end_b = strchr(b, '/');
        uint64_t length_a = end_a != nullptr ? (uint64_t)(end_a - a) : strlen(a);
        uint64_t length_b = end_b != nullptr ? (uint64_t)(end_b - b) : strlen(b);
        uint64_t common = length_a < length_b ? length_a : length_b;
        int order = memcmp(a, b, common);
        if (order != 0) return order < 0;
        if (length_a != length_b) return length_a < length_b;
        if (end_a == nullptr || end_b == nullptr) return end_a == nullptr && end_b != nullptr;
        a = end_a + 1;
        b = end_b + 1;
    }
    return false;
}

// Appends every .md file under the folder, at any depth, sorted, to paths. The folders still to read are a stack.
static void smith_docs_collect(SmithDocs *docs, const char *folder, const char **paths, uint32_t *count) {
    const char **pending = SMITH_DOCS_PUSH(&docs->arena, const char *, smith_docs_paths_max);
    SmithDocsEntry *entries = SMITH_DOCS_PUSH(&docs->arena, SmithDocsEntry, smith_docs_entries_max);
    uint32_t first = *count;
    uint32_t pending_count = 0;
    pending[pending_count++] = folder;
    for (uint32_t round = 0; round < smith_docs_paths_max && pending_count > 0; round++) {
        const char *current = pending[--pending_count];
        uint32_t names = smith_docs_list(docs, current, entries);
        for (uint32_t i = 0; i < names; i++) {
            char path[smith_docs_path_capacity];
            snprintf(path, sizeof(path), "%s/%s", current, entries[i].name);
            char *kept = smith_docs_copy(docs, path, strlen(path));
            if (entries[i].directory && pending_count < smith_docs_paths_max) {
                pending[pending_count++] = kept;
            } else if (!entries[i].directory && smith_docs_ends(smith_docs_text(kept), ".md") &&
                       *count < smith_docs_paths_max) {
                paths[(*count)++] = kept;
            }
        }
    }
    for (uint32_t i = first + 1; i < *count; i++) { // an insertion sort
        const char *path = paths[i];
        uint32_t at = i;
        for (; at > first && smith_docs_path_less(path, paths[at - 1]); at--) {
            paths[at] = paths[at - 1];
        }
        paths[at] = path;
    }
}

// ---- Frontmatter

static void smith_docs_set(SmithDocsFields *fields, SmithDocsText key, SmithDocsText value) {
    for (uint32_t i = 0; i < fields->count; i++) {
        if (smith_docs_same(fields->fields[i].key, key)) {
            fields->fields[i].value = value;
            return;
        }
    }
    if (fields->count < smith_docs_fields_max) fields->fields[fields->count++] = (SmithDocsField){key, value};
}

static SmithDocsText smith_docs_get(const SmithDocsFields *fields, const char *key) {
    for (uint32_t i = 0; i < fields->count; i++) {
        if (smith_docs_is(fields->fields[i].key, key)) return fields->fields[i].value;
    }
    return (SmithDocsText){};
}

// The value of a "key: value" line: stripped, and without one pair of matching quotes around it.
static SmithDocsText smith_docs_value(SmithDocsText value) {
    value = smith_docs_strip(value);
    bool quoted = value.length >= 2 && value.begin[0] == value.begin[value.length - 1] &&
                  (value.begin[0] == '"' || value.begin[0] == '\'');
    return quoted ? (SmithDocsText){value.begin + 1, value.length - 2} : value;
}

// The top-level key: value pairs of a leading --- block; false if there is no block or it is never closed.
static bool smith_docs_frontmatter(SmithDocsText text, SmithDocsFields *fields) {
    *fields = (SmithDocsFields){};
    SmithDocsText line = {};
    if (!smith_docs_line(&text, &line) || !smith_docs_is(smith_docs_strip(line), "---")) return false;
    while (smith_docs_line(&text, &line)) {
        if (smith_docs_is(smith_docs_strip(line), "---")) return true;
        if (line.length > 0 && smith_docs_is_space(line.begin[0])) continue;
        uint64_t colon = 0;
        while (colon < line.length && line.begin[colon] != ':') {
            colon += 1;
        }
        if (colon == line.length) continue;
        SmithDocsText key = smith_docs_strip((SmithDocsText){line.begin, colon});
        SmithDocsText value = {line.begin + colon + 1, line.length - colon - 1};
        smith_docs_set(fields, key, smith_docs_value(value));
    }
    *fields = (SmithDocsFields){}; // never closed: no frontmatter, so no fields
    return false;
}

// ---- Code and links

// Whether a fenced block opens at the line starting at position i and closes on a later line; its end if so.
static bool smith_docs_fence_end(SmithDocsText text, uint64_t i, uint64_t *end) {
    SmithDocsText here = {text.begin + i, text.length - i};
    if (!smith_docs_starts(here, "```") && !smith_docs_starts(here, "~~~")) return false;
    uint64_t at = i;
    for (uint64_t round = 0; round < text.length; round++) {
        while (at < text.length && text.begin[at] != '\n') {
            at += 1;
        }
        if (at >= text.length) return false;
        at += 1; // the next line starts here
        SmithDocsText line = {text.begin + at, text.length - at};
        if (line.length >= smith_docs_fence_length && memcmp(line.begin, here.begin, smith_docs_fence_length) == 0) {
            *end = at + smith_docs_fence_length;
            return true;
        }
    }
    return false;
}

// The text without fenced code blocks and inline code spans.
static SmithDocsText smith_docs_body(SmithDocs *docs, SmithDocsText text) {
    char *out = SMITH_DOCS_PUSH(&docs->arena, char, text.length + 1);
    uint64_t length = 0;
    for (uint64_t i = 0; i < text.length;) {
        uint64_t end = 0;
        if ((i == 0 || text.begin[i - 1] == '\n') && smith_docs_fence_end(text, i, &end)) {
            i = end;
            continue;
        }
        out[length++] = text.begin[i++];
    }
    uint64_t kept = 0;
    for (uint64_t i = 0; i < length;) {
        uint64_t j = i + 1;
        while (out[i] == '`' && j < length && out[j] != '`' && out[j] != '\n') {
            j += 1;
        }
        if (out[i] == '`' && j < length && out[j] == '`') {
            i = j + 1;
            continue;
        }
        out[kept++] = out[i++];
    }
    return (SmithDocsText){out, kept};
}

static bool smith_docs_is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Whether the target starts with a URL scheme: a letter, then letters, digits, + . - and a colon.
static bool smith_docs_has_scheme(SmithDocsText target) {
    if (target.length == 0 || !smith_docs_is_letter(target.begin[0])) return false;
    for (uint64_t i = 1; i < target.length; i++) {
        char c = target.begin[i];
        if (c == ':') return true;
        bool word = smith_docs_is_letter(c) || (c >= '0' && c <= '9');
        if (!word && c != '+' && c != '.' && c != '-') return false;
    }
    return false;
}

// The link [text](target) or [text](target "title") starting at text[0]: its target and its length. False if none.
static bool smith_docs_link_at(SmithDocsText text, SmithDocsText *target, uint64_t *length) {
    uint64_t i = 1;
    while (i < text.length && text.begin[i] != ']') {
        i += 1;
    }
    if (i + 1 >= text.length || text.begin[i + 1] != '(') return false;
    uint64_t start = i + 2;
    uint64_t j = start;
    while (j < text.length && text.begin[j] != ')' && !smith_docs_is_space(text.begin[j])) {
        j += 1;
    }
    if (j == start || j >= text.length) return false;
    *target = (SmithDocsText){text.begin + start, j - start};
    if (text.begin[j] != ')') {
        while (j < text.length && smith_docs_is_space(text.begin[j])) {
            j += 1;
        }
        if (j >= text.length || text.begin[j] != '"') return false;
        j += 1;
        while (j < text.length && text.begin[j] != '"') {
            j += 1;
        }
        j += 1; // the closing quote
        if (j >= text.length || text.begin[j] != ')') return false;
    }
    *length = j + 1;
    return true;
}

// The path with "." and ".." resolved by name alone, without a leading "/"; false if it climbs out of the project.
static bool smith_docs_normalize(const char *path, char *out, size_t capacity) {
    size_t length = 0;
    out[0] = '\0';
    for (const char *c = path; *c != '\0';) {
        size_t part = strcspn(c, "/");
        if (part == 0 || (part == 1 && c[0] == '.')) {
            // an empty or "." component: nothing
        } else if (part == 2 && c[0] == '.' && c[1] == '.') {
            char *slash = strrchr(out, '/');
            if (length == 0) return false;
            length = slash != nullptr ? (size_t)(slash - out) : 0;
            out[length] = '\0';
        } else {
            if (length + part + 2 > capacity) return false;
            if (length > 0) out[length++] = '/';
            memcpy(out + length, c, part);
            length += part;
            out[length] = '\0';
        }
        c += part;
        if (*c == '/') c += 1;
    }
    return true;
}

static bool smith_docs_in_folder(const char *path, const char *folder) {
    size_t length = strlen(folder);
    return strncmp(path, folder, length) == 0 && (path[length] == '\0' || path[length] == '/');
}

// Whether the link from the document leads into a skill folder: knowledge says what, skills say how, and only skills
// link knowledge.
static bool smith_docs_links_skill(const char *joined) {
    char normal[smith_docs_path_capacity];
    return smith_docs_normalize(joined, normal, sizeof normal) &&
           (smith_docs_in_folder(normal, ".agents/skills") || smith_docs_in_folder(normal, ".claude/skills"));
}

// The path a link in the document points at: relative to the document's folder, or as written for an absolute link.
static void smith_docs_join(const char *relative, SmithDocsText target, uint64_t path_length, char *joined) {
    const char *slash = strrchr(relative, '/');
    uint64_t folder = slash != nullptr ? (uint64_t)(slash - relative) : 0;
    if (target.begin[0] == '/' || folder == 0) {
        snprintf(joined, smith_docs_path_capacity, "%.*s", (int)path_length, target.begin);
    } else {
        snprintf(joined, smith_docs_path_capacity, "%.*s/%.*s", (int)folder, relative, (int)path_length, target.begin);
    }
}

// Checks one link of the document: that it resolves, and that knowledge does not link a skill.
static void smith_docs_link(SmithDocs *docs, const char *relative, SmithDocsText target) {
    if (smith_docs_has_scheme(target) || target.begin[0] == '#') return;
    uint64_t path_length = 0;
    while (path_length < target.length && target.begin[path_length] != '#') {
        path_length += 1;
    }
    if (path_length == 0) return;
    char joined[smith_docs_path_capacity];
    smith_docs_join(relative, target, path_length, joined);
    bool found = target.begin[0] == '/' ? smith_docs_stat(docs, joined) : smith_docs_exists(docs, joined);
    if (!found) {
        smith_docs_report(docs, "%s: broken link %.*s", relative, (int)path_length, target.begin);
    } else if (strncmp(relative, "knowledge/", 10) == 0 && target.begin[0] != '/' && smith_docs_links_skill(joined)) {
        smith_docs_report(docs, "%s: links the skill %.*s; knowledge never links skills", relative, (int)path_length,
                          target.begin);
    }
}

// Checks each relative link of the document, outside code.
static void smith_docs_links(SmithDocs *docs, const char *relative, SmithDocsText text) {
    size_t mark = docs->arena.used;
    SmithDocsText body = smith_docs_body(docs, text);
    for (uint64_t i = 0; i < body.length; i++) {
        SmithDocsText target = {};
        uint64_t length = 0;
        if (body.begin[i] != '[' ||
            !smith_docs_link_at((SmithDocsText){body.begin + i, body.length - i}, &target, &length)) {
            continue;
        }
        i += length - 1;
        smith_docs_link(docs, relative, target);
    }
    docs->arena.used = mark;
}

// Reports each merge-conflict marker line of the document.
static void smith_docs_conflicts(SmithDocs *docs, const char *relative, SmithDocsText text) {
    SmithDocsText line = {};
    for (uint32_t number = 1; smith_docs_line(&text, &line); number++) {
        bool marker = smith_docs_starts(line, "<<<<<<< ") || smith_docs_starts(line, "||||||| ") ||
                      smith_docs_starts(line, ">>>>>>> ") || smith_docs_is(line, "=======");
        if (marker) smith_docs_report(docs, "%s:%u: merge-conflict marker", relative, number);
    }
}

// ---- Concepts

// The concepts, knowledge/<name>.md other than index.md and log.md, sorted by name.
static uint32_t smith_docs_concepts(SmithDocs *docs, const char **names) {
    SmithDocsEntry *entries = SMITH_DOCS_PUSH(&docs->arena, SmithDocsEntry, smith_docs_entries_max);
    uint32_t total = smith_docs_list(docs, "knowledge", entries);
    uint32_t count = 0;
    for (uint32_t i = 0; i < total; i++) {
        SmithDocsText name = smith_docs_text(entries[i].name);
        if (!smith_docs_ends(name, ".md") || smith_docs_is(name, "index.md") || smith_docs_is(name, "log.md")) continue;
        names[count++] = entries[i].name;
    }
    return count;
}

// Whether the file name is lowercase letters and digits in words joined by single hyphens, then .md.
static bool smith_docs_kebab(SmithDocsText name) {
    if (!smith_docs_ends(name, ".md")) return false;
    name.length -= 3;
    bool word = false;
    for (uint64_t i = 0; i < name.length; i++) {
        char c = name.begin[i];
        bool alphanumeric = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alphanumeric) {
            word = true;
        } else if (c == '-' && word && i + 1 < name.length) {
            word = false;
        } else {
            return false;
        }
    }
    return word;
}

// The type names, as "a, b, c", for a message.
static void smith_docs_type_list(const SmithDocs *docs, char *out, size_t capacity) {
    const char *const *types = docs->type_count > 0 ? docs->types : smith_docs_default_types;
    uint32_t count = docs->type_count > 0
                         ? docs->type_count
                         : (uint32_t)(sizeof smith_docs_default_types / sizeof smith_docs_default_types[0]);
    size_t used = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < count && used + 1 < capacity; i++) {
        int written = snprintf(out + used, capacity - used, "%s%s", i > 0 ? ", " : "", types[i]);
        if (written < 0 || (size_t)written >= capacity - used) break;
        used += (size_t)written;
    }
}

static bool smith_docs_type_allowed(const SmithDocs *docs, SmithDocsText type) {
    if (docs->type_count == 0) {
        return smith_docs_in(type, smith_docs_default_types,
                             (uint32_t)(sizeof smith_docs_default_types / sizeof smith_docs_default_types[0]));
    }
    return smith_docs_in(type, docs->types, docs->type_count);
}

// Whether the value is in the comma-separated list.
static bool smith_docs_listed(SmithDocsText value, SmithDocsText list) {
    if (value.length == 0) return false;
    SmithDocsText rest = list;
    for (uint64_t round = 0; round <= list.length; round++) {
        uint64_t end = 0;
        while (end < rest.length && rest.begin[end] != ',') {
            end += 1;
        }
        if (smith_docs_same(smith_docs_strip((SmithDocsText){rest.begin, end}), value)) return true;
        if (end >= rest.length) return false;
        rest.begin += end + 1;
        rest.length -= end + 1;
    }
    return false;
}

// The comma-separated values, as "a, b, c", for a message.
static void smith_docs_value_list(SmithDocsText list, char *out, size_t capacity) {
    size_t used = 0;
    out[0] = '\0';
    SmithDocsText rest = list;
    for (uint64_t round = 0; round <= list.length; round++) {
        uint64_t end = 0;
        while (end < rest.length && rest.begin[end] != ',') {
            end += 1;
        }
        SmithDocsText value = smith_docs_strip((SmithDocsText){rest.begin, end});
        int written =
            snprintf(out + used, capacity - used, "%s%.*s", used > 0 ? ", " : "", (int)value.length, value.begin);
        if (written < 0 || (size_t)written >= capacity - used) return;
        used += (size_t)written;
        if (end >= rest.length) return;
        rest.begin += end + 1;
        rest.length -= end + 1;
    }
}

// Reports a type's required fields that the concept lacks, or holds with a value the list does not have.
static void smith_docs_requirements(SmithDocs *docs, const char *relative, const SmithDocsFields *fields,
                                    SmithDocsText type) {
    for (uint32_t i = 0; i < docs->requirement_count; i++) {
        const SmithDocsRequirement *need = &docs->requirements[i];
        if (!smith_docs_same(need->type, type)) continue;
        char field[128];
        snprintf(field, sizeof field, "%.*s", (int)need->field.length, need->field.begin);
        SmithDocsText value = smith_docs_get(fields, field);
        bool good = need->values.begin != nullptr ? smith_docs_listed(value, need->values) : value.length > 0;
        if (good) continue;
        if (need->values.begin == nullptr) {
            smith_docs_report(docs, "%s: type '%.*s' needs %s", relative, (int)type.length, type.begin, field);
            continue;
        }
        char values[256];
        smith_docs_value_list(need->values, values, sizeof values);
        smith_docs_report(docs, "%s: type '%.*s' needs %s: %s", relative, (int)type.length, type.begin, field, values);
    }
}

static void smith_docs_concept(SmithDocs *docs, const char *name) {
    char relative[smith_docs_path_capacity];
    snprintf(relative, sizeof(relative), "knowledge/%s", name);
    if (!smith_docs_kebab(smith_docs_text(name))) smith_docs_report(docs, "%s: file name is not kebab-case", relative);
    SmithDocsFields fields;
    if (!smith_docs_frontmatter(smith_docs_read(docs, relative), &fields)) {
        smith_docs_report(docs, "%s: no frontmatter", relative);
        return;
    }
    for (uint32_t i = 0; i < (sizeof smith_docs_required / sizeof smith_docs_required[0]); i++) {
        if (smith_docs_get(&fields, smith_docs_required[i]).length == 0) {
            smith_docs_report(docs, "%s: frontmatter lacks %s", relative, smith_docs_required[i]);
        }
    }
    SmithDocsText type = smith_docs_get(&fields, "type");
    if (type.length > 0 && !smith_docs_type_allowed(docs, type)) {
        char list[512];
        smith_docs_type_list(docs, list, sizeof list);
        smith_docs_report(docs, "%s: type '%.*s' is not one of %s", relative, (int)type.length, type.begin, list);
    }
    if (type.length > 0) smith_docs_requirements(docs, relative, &fields, type);
}

// ---- The index

// Reads an item "- [Title](target): description" off an index line; false for any other line.
static bool smith_docs_item(SmithDocsText line, SmithDocsItem *item) {
    uint64_t i = 0;
    while (i < line.length && smith_docs_is_space(line.begin[i])) {
        i += 1;
    }
    if (i + 3 > line.length || (line.begin[i] != '-' && line.begin[i] != '*') || line.begin[i + 1] != ' ' ||
        line.begin[i + 2] != '[') {
        return false;
    }
    i += 3;
    uint64_t title = i;
    while (i < line.length && line.begin[i] != ']') {
        i += 1;
    }
    if (i == title || i + 1 >= line.length || line.begin[i + 1] != '(') return false;
    uint64_t target = i + 2;
    uint64_t end = target;
    while (end < line.length && line.begin[end] != ')') {
        end += 1;
    }
    SmithDocsText rest = {line.begin + end, line.length - end};
    if (end == target || !smith_docs_starts(rest, "): ")) return false;
    SmithDocsText description = smith_docs_strip((SmithDocsText){rest.begin + 3, rest.length - 3});
    if (description.length == 0) return false;
    *item = (SmithDocsItem){.target = {line.begin + target, end - target}, .description = description};
    return true;
}

// Index items by file; a file listed again replaces the earlier item (smith_docs_index_order reports it).
static uint32_t smith_docs_items(SmithDocs *docs, SmithDocsText index, SmithDocsItem *items) {
    SmithDocsText body = smith_docs_body(docs, index);
    uint32_t count = 0;
    SmithDocsText line = {};
    while (smith_docs_line(&body, &line)) {
        SmithDocsItem item;
        if (!smith_docs_item(line, &item)) continue;
        uint32_t at = count;
        for (uint32_t i = 0; i < count; i++) {
            if (smith_docs_same(items[i].target, item.target)) at = i;
        }
        if (at == count && count < smith_docs_listed_max) {
            count += 1;
        }
        items[at] = item;
    }
    return count;
}

// The position of the item that links to the file, or count if none does.
static uint32_t smith_docs_find(const SmithDocsItem *items, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; i++) {
        if (smith_docs_is(items[i].target, name)) return i;
    }
    return count;
}

// Whether a sorts before b, by bytes.
static bool smith_docs_less(SmithDocsText a, SmithDocsText b) {
    uint64_t common = a.length < b.length ? a.length : b.length;
    int order = memcmp(a.begin, b.begin, common);
    return order != 0 ? order < 0 : a.length < b.length;
}

// Whether the line opens or closes a fenced code block.
static bool smith_docs_is_fence(SmithDocsText line) {
    return smith_docs_starts(line, "```") || smith_docs_starts(line, "~~~");
}

// Reports, with the line, an item listed twice and an item that sorts before the one above it in the same section (a
// run of item lines): the entries are one to a line, sorted by file name, so a bad merge shows at once.
static void smith_docs_index_order(SmithDocs *docs, SmithDocsText index) {
    SmithDocsItem *seen = SMITH_DOCS_PUSH(&docs->arena, SmithDocsItem, smith_docs_listed_max);
    uint32_t count = 0;
    SmithDocsText previous = {};
    bool fenced = false;
    SmithDocsText line = {};
    for (uint32_t number = 1; smith_docs_line(&index, &line); number++) {
        if (smith_docs_is_fence(line)) fenced = !fenced;
        SmithDocsItem item;
        if (fenced || !smith_docs_item(line, &item)) {
            previous = (SmithDocsText){};
            continue;
        }
        bool twice = false;
        for (uint32_t i = 0; i < count; i++) {
            twice = twice || smith_docs_same(seen[i].target, item.target);
        }
        if (twice) {
            smith_docs_report(docs, "knowledge/index.md:%u: %.*s is listed twice; remove one", number,
                              (int)item.target.length, item.target.begin);
        } else if (previous.begin != nullptr && smith_docs_less(item.target, previous)) {
            smith_docs_report(docs,
                              "knowledge/index.md:%u: entry %.*s is out of order; sort the entries of each section by "
                              "file name (after %.*s)",
                              number, (int)item.target.length, item.target.begin, (int)previous.length, previous.begin);
        }
        if (!twice && count < smith_docs_listed_max) seen[count++] = item;
        previous = item.target;
    }
}

// Reports each item that is no concept on disk, in the order of the targets.
static void smith_docs_index_ghosts(SmithDocs *docs, const SmithDocsItem *items, uint32_t count, const char **names,
                                    uint32_t name_count) {
    uint32_t *order = SMITH_DOCS_PUSH(&docs->arena, uint32_t, smith_docs_listed_max);
    for (uint32_t i = 0; i < count; i++) { // the items' indexes, sorted by target
        uint32_t at = i;
        for (; at > 0 && smith_docs_less(items[i].target, items[order[at - 1]].target); at--) {
            order[at] = order[at - 1];
        }
        order[at] = i;
    }
    for (uint32_t i = 0; i < count; i++) {
        const SmithDocsItem *item = &items[order[i]];
        bool concept = false;
        for (uint32_t j = 0; j < name_count; j++) {
            concept = concept || smith_docs_is(item->target, names[j]);
        }
        if (!concept) {
            smith_docs_report(docs, "knowledge/index.md: lists %.*s, which is not a concept on disk",
                              (int)item->target.length, item->target.begin);
        }
    }
}

// The index must list exactly the concepts: report a file not listed, an item that is no concept (sorted), and an item
// whose description is not the concept's own.
static void smith_docs_index(SmithDocs *docs, const char **names, uint32_t name_count) {
    SmithDocsText index = smith_docs_read(docs, "knowledge/index.md");
    if (index.begin == nullptr) {
        if (name_count > 0) smith_docs_report(docs, "knowledge/index.md: missing");
        return;
    }
    SmithDocsFields own_fields;
    (void)smith_docs_frontmatter(index, &own_fields);
    if (smith_docs_get(&own_fields, "okf_version").length == 0) {
        smith_docs_report(docs, "knowledge/index.md: frontmatter lacks okf_version");
    }
    smith_docs_index_order(docs, index);
    SmithDocsItem *items = SMITH_DOCS_PUSH(&docs->arena, SmithDocsItem, smith_docs_listed_max);
    uint32_t count = smith_docs_items(docs, index, items);
    for (uint32_t i = 0; i < name_count; i++) {
        if (smith_docs_find(items, count, names[i]) == count) {
            smith_docs_report(docs, "knowledge/index.md: does not list %s", names[i]);
        }
    }
    smith_docs_index_ghosts(docs, items, count, names, name_count);
    for (uint32_t i = 0; i < name_count; i++) {
        uint32_t at = smith_docs_find(items, count, names[i]);
        if (at == count) continue;
        char relative[smith_docs_path_capacity];
        snprintf(relative, sizeof(relative), "knowledge/%s", names[i]);
        SmithDocsFields fields;
        (void)smith_docs_frontmatter(smith_docs_read(docs, relative), &fields);
        SmithDocsText own = smith_docs_get(&fields, "description");
        if (own.begin == nullptr || !smith_docs_same(items[at].description, own)) {
            smith_docs_report(docs, "knowledge/index.md: description of %s differs from its own", names[i]);
        }
    }
}

// ---- Skills

constexpr uint32_t smith_docs_skill_lines_max = 60;

static uint32_t smith_docs_line_count(SmithDocsText text) {
    uint32_t lines = 0;
    for (size_t i = 0; i < text.length; i++)
        lines += text.begin[i] == '\n' ? 1 : 0;
    return lines;
}

static void smith_docs_skill(SmithDocs *docs, const char *folder) {
    char relative[smith_docs_path_capacity];
    snprintf(relative, sizeof(relative), ".agents/skills/%s/SKILL.md", folder);
    if (!smith_docs_exists(docs, relative)) {
        smith_docs_report(docs, "%s: missing", relative);
        return;
    }
    SmithDocsFields fields;
    if (!smith_docs_frontmatter(smith_docs_read(docs, relative), &fields)) {
        smith_docs_report(docs, "%s: no frontmatter", relative);
        return;
    }
    SmithDocsText name = smith_docs_get(&fields, "name");
    if (name.begin == nullptr) {
        smith_docs_report(docs, "%s: name None does not match its folder", relative);
    } else if (!smith_docs_is(name, folder)) {
        smith_docs_report(docs, "%s: name '%.*s' does not match its folder", relative, (int)name.length, name.begin);
    }
    if (smith_docs_get(&fields, "description").length == 0) {
        smith_docs_report(docs, "%s: frontmatter lacks description", relative);
    }
    uint32_t lines = smith_docs_line_count(smith_docs_read(docs, relative));
    if (lines > smith_docs_skill_lines_max)
        smith_docs_report(docs, "%s: %u lines; a skill stays under %u: split it or shorten it", relative, lines,
                          smith_docs_skill_lines_max);
}

static void smith_docs_skills(SmithDocs *docs) {
    SmithDocsEntry *entries = SMITH_DOCS_PUSH(&docs->arena, SmithDocsEntry, smith_docs_entries_max);
    uint32_t count = smith_docs_list(docs, ".agents/skills", entries);
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].directory) smith_docs_skill(docs, entries[i].name);
    }
}

// ---- The plugin

constexpr uint32_t smith_docs_plugin_skills_max = 256;

// The offset just after `"key"` and its colon, in the document, or -1: the first place a value for key could start.
static int64_t smith_docs_json_value(SmithDocsText json, const char *key) {
    char quoted[128];
    int written = snprintf(quoted, sizeof quoted, "\"%s\"", key);
    if (written < 0 || (size_t)written >= sizeof quoted) return -1;
    uint64_t wanted = strlen(quoted);
    for (uint64_t at = 0; at + wanted <= json.length; at++) {
        if (memcmp(json.begin + at, quoted, wanted) != 0) continue;
        uint64_t next = at + wanted;
        while (next < json.length && smith_docs_is_space(json.begin[next]))
            next++;
        if (next >= json.length || json.begin[next] != ':') continue;
        return (int64_t)next + 1;
    }
    return -1;
}

// The string that starts at or after `from` (spaces skipped) and the offset after it, or no begin.
static SmithDocsText smith_docs_json_string(SmithDocsText json, uint64_t *from) {
    uint64_t at = *from;
    while (at < json.length && smith_docs_is_space(json.begin[at]))
        at++;
    if (at >= json.length || json.begin[at] != '"') return (SmithDocsText){};
    uint64_t end = at + 1;
    while (end < json.length && json.begin[end] != '"')
        end++;
    if (end >= json.length) return (SmithDocsText){};
    *from = end + 1;
    return (SmithDocsText){json.begin + at + 1, end - at - 1};
}

// Whether the skill is one for changing the project: <plugin>-dev or <plugin>-dev-<anything>.
static bool smith_docs_dev_skill(SmithDocsText plugin, SmithDocsText skill) {
    char dev[256];
    int written = snprintf(dev, sizeof dev, "%.*s-dev", (int)plugin.length, plugin.begin);
    if (written < 0 || (size_t)written >= sizeof dev - 1) return false;
    if (smith_docs_is(skill, dev)) return true;
    dev[written] = '-';
    dev[written + 1] = '\0';
    return smith_docs_starts(skill, dev);
}

// The folder's name of a listed skill, "./.agents/skills/<name>/" -> <name>.
static SmithDocsText smith_docs_listed_skill(SmithDocsText entry) {
    while (entry.length > 0 && entry.begin[entry.length - 1] == '/')
        entry.length -= 1;
    uint64_t start = entry.length;
    while (start > 0 && entry.begin[start - 1] != '/')
        start--;
    return (SmithDocsText){entry.begin + start, entry.length - start};
}

static bool smith_docs_names_skill(const SmithDocsText *listed, uint32_t count, const char *folder) {
    for (uint32_t i = 0; i < count; i++) {
        if (smith_docs_is(listed[i], folder)) return true;
    }
    return false;
}

// A plugin lists its skills in .claude-plugin/plugin.json, and exactly the ones that are not for changing the project
// (named <plugin>-dev or <plugin>-dev-*), which stay in the repository only.
static void smith_docs_plugin(SmithDocs *docs) {
    constexpr char relative[] = ".claude-plugin/plugin.json";
    SmithDocsText json = smith_docs_read(docs, relative);
    if (json.begin == nullptr) return;
    int64_t name_at = smith_docs_json_value(json, "name");
    int64_t skills_at = smith_docs_json_value(json, "skills");
    if (name_at < 0 || skills_at < 0) return;
    uint64_t from = (uint64_t)name_at;
    SmithDocsText plugin = smith_docs_json_string(json, &from);
    if (plugin.begin == nullptr) return;
    from = (uint64_t)skills_at;
    while (from < json.length && smith_docs_is_space(json.begin[from]))
        from++;
    if (from >= json.length || json.begin[from] != '[') return;
    from += 1;
    SmithDocsText *listed = SMITH_DOCS_PUSH(&docs->arena, SmithDocsText, smith_docs_plugin_skills_max);
    uint32_t count = 0;
    for (; count < smith_docs_plugin_skills_max; count++) {
        SmithDocsText entry = smith_docs_json_string(json, &from);
        if (entry.begin == nullptr) break;
        listed[count] = smith_docs_listed_skill(entry);
        while (from < json.length && (smith_docs_is_space(json.begin[from]) || json.begin[from] == ','))
            from++;
    }
    SmithDocsEntry *entries = SMITH_DOCS_PUSH(&docs->arena, SmithDocsEntry, smith_docs_entries_max);
    uint32_t total = smith_docs_list(docs, ".agents/skills", entries);
    for (uint32_t i = 0; i < count; i++) {
        bool on_disk = false;
        for (uint32_t j = 0; j < total; j++) {
            on_disk = on_disk || (entries[j].directory && smith_docs_is(listed[i], entries[j].name));
        }
        if (smith_docs_dev_skill(plugin, listed[i])) {
            smith_docs_report(docs, "%s: lists the dev skill %.*s, which must not ship", relative,
                              (int)listed[i].length, listed[i].begin);
        } else if (!on_disk) {
            smith_docs_report(docs, "%s: lists %.*s, which is not a skill on disk", relative, (int)listed[i].length,
                              listed[i].begin);
        }
    }
    for (uint32_t j = 0; j < total; j++) {
        if (!entries[j].directory || smith_docs_dev_skill(plugin, smith_docs_text(entries[j].name))) continue;
        if (!smith_docs_names_skill(listed, count, entries[j].name)) {
            smith_docs_report(docs, "%s: does not list the skill %s", relative, entries[j].name);
        }
    }
}

// Claude Code finds a project's skills only in .claude/skills, so with skills in .agents/skills it is a link to them.
static void smith_docs_claude_link(SmithDocs *docs) {
    constexpr char wanted[] = "../.agents/skills";
    if (!smith_docs_exists(docs, ".agents/skills")) return;
    char path[smith_docs_path_capacity];
    smith_docs_path(docs, ".claude/skills", path);
    char target[smith_docs_path_capacity];
    ssize_t length = readlink(path, target, sizeof target - 1);
    if (length < 0) {
        smith_docs_report(docs, ".claude/skills: %s; it must be a symlink to %s",
                          errno == ENOENT ? "missing" : "is not a symlink", wanted);
        return;
    }
    target[length] = '\0';
    smith_docs_depend(docs, path);
    if (strcmp(target, wanted) != 0) smith_docs_report(docs, ".claude/skills: links to %s, not %s", target, wanted);
}

// ---- Documents

// Every .md under knowledge/ and under .agents/skills/, each folder's sorted. A project's AGENTS.md is its own file and
// is not checked.
static uint32_t smith_docs_documents(SmithDocs *docs, const char **paths) {
    uint32_t count = 0;
    smith_docs_collect(docs, "knowledge", paths, &count);
    smith_docs_collect(docs, ".agents/skills", paths, &count);
    return count;
}

static void smith_docs_run(SmithDocs *docs) {
    const char **names = SMITH_DOCS_PUSH(&docs->arena, const char *, smith_docs_entries_max);
    docs->concepts = smith_docs_concepts(docs, names);
    for (uint32_t i = 0; i < docs->concepts; i++) {
        smith_docs_concept(docs, names[i]);
    }
    smith_docs_index(docs, names, docs->concepts);
    smith_docs_skills(docs);
    smith_docs_plugin(docs);
    smith_docs_claude_link(docs);
    const char **documents = SMITH_DOCS_PUSH(&docs->arena, const char *, smith_docs_paths_max * 2 + 1);
    uint32_t count = smith_docs_documents(docs, documents);
    for (uint32_t i = 0; i < count; i++) {
        smith_docs_links(docs, documents[i], smith_docs_read(docs, documents[i]));
    }
    for (uint32_t i = 0; i < count; i++) {
        smith_docs_conflicts(docs, documents[i], smith_docs_read(docs, documents[i]));
    }
}

// ---- The command

[[noreturn]] static void smith_docs_usage(void) {
    smith_die(smith_exit_usage, "usage:",
              "smith docs [--type <name>]... [--require <type>:<field>[=<value>,...]]... "
              "[--depfile <path> --target <path>] [<root>]");
}

// "type:field" or "type:field=a,b,c".
static void smith_docs_add_requirement(SmithDocs *docs, const char *spec) {
    const char *colon = strchr(spec, ':');
    if (colon == nullptr || colon == spec || colon[1] == '\0' || docs->requirement_count == smith_docs_requires_max) {
        smith_die(smith_exit_usage, "docs: --require takes <type>:<field>[=<value>,...], not", spec);
    }
    const char *equals = strchr(colon, '=');
    const char *field_end = equals != nullptr ? equals : spec + strlen(spec);
    if (field_end == colon + 1 || (equals != nullptr && equals[1] == '\0')) {
        smith_die(smith_exit_usage, "docs: --require takes <type>:<field>[=<value>,...], not", spec);
    }
    docs->requirements[docs->requirement_count++] =
        (SmithDocsRequirement){.type = {spec, (uint64_t)(colon - spec)},
                               .field = {colon + 1, (uint64_t)(field_end - colon - 1)},
                               .values = equals != nullptr ? smith_docs_text(equals + 1) : (SmithDocsText){}};
}

typedef struct {
    const char *depfile;
    const char *target;
} SmithDocsOutput;

// Reads the arguments into docs and the depfile's names; the first without a dash is the root.
static SmithDocsOutput smith_docs_arguments(SmithDocs *docs, int count, char **args) {
    SmithDocsOutput output = {};
    bool root_given = false;
    for (int i = 0; i < count; i++) {
        const char *flag = args[i];
        bool valued = strcmp(flag, "--depfile") == 0 || strcmp(flag, "--target") == 0 || strcmp(flag, "--type") == 0 ||
                      strcmp(flag, "--require") == 0;
        if (valued && i + 1 < count) {
            const char *value = args[++i];
            if (strcmp(flag, "--depfile") == 0) output.depfile = value;
            if (strcmp(flag, "--target") == 0) output.target = value;
            if (strcmp(flag, "--require") == 0) smith_docs_add_requirement(docs, value);
            if (strcmp(flag, "--type") == 0) {
                if (docs->type_count == smith_docs_types_max) smith_docs_usage();
                docs->types[docs->type_count++] = value;
            }
        } else if (!valued && flag[0] != '-' && !root_given) {
            docs->root = flag;
            root_given = true;
        } else {
            smith_docs_usage();
        }
    }
    if ((output.depfile == nullptr) != (output.target == nullptr)) smith_docs_usage();
    return output;
}

static SmithDocsArena smith_docs_map(size_t size) {
    void *memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) smith_die(smith_exit_usage, "docs: cannot reserve memory:", strerror(errno));
    return (SmithDocsArena){.base = memory, .size = size};
}

// The next item of a list in a frontmatter value, split at `separator`, stripped and copied (C string) into the arena;
// nullptr at the end. *rest is what is left.
static const char *smith_docs_next_item(SmithDocs *docs, SmithDocsText *rest, char separator) {
    for (uint32_t guard = 0; rest->length > 0 && guard < smith_docs_types_max * 4; guard++) { // bounded by the items
        uint64_t at = 0;
        while (at < rest->length && rest->begin[at] != separator) {
            at += 1;
        }
        SmithDocsText item = smith_docs_strip((SmithDocsText){rest->begin, at});
        uint64_t used = at < rest->length ? at + 1 : at;
        rest->begin += used;
        rest->length -= used;
        if (item.length == 0) continue;
        char *copy = SMITH_DOCS_PUSH(&docs->arena, char, item.length + 1);
        memcpy(copy, item.begin, item.length);
        copy[item.length] = '\0';
        return copy;
    }
    return nullptr;
}

// The concept types and required fields a project allows are declared in knowledge/index.md, in its frontmatter:
//   types: convention, architecture, contract, design
//   requires: design:status=draft,accepted; contract:owner
// `types` lists the allowed types (without it: convention, architecture, contract and design), and `requires` the
// fields a type must have, each optionally from a list of values, the requirements separated by ";". The command line's
// --type and --require replace what the index declares.
static void smith_docs_declared(SmithDocs *docs) {
    if (!smith_docs_exists(docs, "knowledge/index.md")) return;
    SmithDocsFields fields;
    if (!smith_docs_frontmatter(smith_docs_read(docs, "knowledge/index.md"), &fields)) return;
    if (docs->type_count == 0) {
        SmithDocsText types = smith_docs_get(&fields, "types");
        for (const char *type = smith_docs_next_item(docs, &types, ','); type != nullptr;
             type = smith_docs_next_item(docs, &types, ',')) {
            if (docs->type_count == smith_docs_types_max) smith_docs_usage();
            docs->types[docs->type_count++] = type;
        }
    }
    if (docs->requirement_count > 0) return;
    SmithDocsText requires = smith_docs_get(&fields, "requires");
    for (const char *spec = smith_docs_next_item(docs, &requires, ';'); spec != nullptr;
         spec = smith_docs_next_item(docs, &requires, ';')) {
        smith_docs_add_requirement(docs, spec);
    }
}

// The project of the current folder when no root is named: the nearest folder at or above it that has a knowledge/ or
// an .agents/skills/ folder, looking no higher than the git root (a folder that has .git); else the current folder. So
// a build.c in a subfolder (virtual-cell/learner) checks the docs of its repository. A relative path: "." for the
// current folder, else "..",
// "../..".
static const char *smith_docs_find_root(void) {
    static char root[3 * 64 + 4];
    char prefix[3 * 64 + 4] = "";
    for (size_t level = 0; level < 64; level++) { // bounded by the depth
        char file[sizeof prefix + 16];
        char skills[sizeof prefix + 16];
        struct stat info;
        snprintf(file, sizeof file, "%sknowledge", prefix);
        snprintf(skills, sizeof skills, "%s.agents/skills", prefix);
        if ((stat(file, &info) == 0 && S_ISDIR(info.st_mode)) || (stat(skills, &info) == 0 && S_ISDIR(info.st_mode))) {
            snprintf(root, sizeof root, "%s", level == 0 ? "." : prefix);
            if (level > 0) root[strlen(root) - 1] = '\0'; // "../.." and not "../../"
            return root;
        }
        snprintf(file, sizeof file, "%s.git", prefix);
        if (lstat(file, &info) == 0) break; // the git root: nothing above it belongs to the project
        strcat(prefix, "../");
    }
    return ".";
}

static int smith_docs_check(int count, char **args) {
    SmithDocs docs = {.root = nullptr, .errors = stderr};
    SmithDocsOutput output = smith_docs_arguments(&docs, count, args);
    if (docs.root == nullptr) docs.root = smith_docs_find_root();
    docs.arena = smith_docs_map(smith_docs_memory_size);
    docs.depends = smith_docs_map(smith_docs_depends_size);
    docs.depended = SMITH_DOCS_PUSH(&docs.depends, const char *, smith_docs_depends_max);
    smith_docs_declared(&docs);
    smith_docs_run(&docs);
    printf("smith docs: %u concepts, %u problems\n", docs.concepts, docs.problems);
    if (output.depfile != nullptr && !smith_docs_write_depfile(&docs, output.depfile, output.target)) {
        smith_die(smith_exit_usage, "docs: cannot write", output.depfile);
    }
    return docs.problems > 0 ? smith_docs_status_problems : 0;
}

// `smith docs`: the check on the project they name (the current folder by default), with the types and required fields
// knowledge/index.md declares. Its arguments are the check's options.
static int smith_docs_main(int count, char **args) {
    return smith_docs_check(smith_only_options(count, args) ? 0 : count, args);
}

#ifdef SMITH_CORE
// include/smith_core_docs.h: the check of the project at `folder` in process, for the units runner.
bool smith_core_docs_check(const char *folder, char *out, size_t out_size) {
    FILE *text = fmemopen(out, out_size, "w");
    if (text == nullptr) return false;
    SmithDocs docs = {.root = folder, .errors = text};
    docs.arena = smith_docs_map(smith_docs_memory_size);
    docs.depends = smith_docs_map(smith_docs_depends_size);
    docs.depended = SMITH_DOCS_PUSH(&docs.depends, const char *, smith_docs_depends_max);
    smith_docs_declared(&docs);
    smith_docs_run(&docs);
    fprintf(text, "smith docs: %u concepts, %u problems\n", docs.concepts, docs.problems);
    bool fits = ferror(text) == 0;
    fclose(text);
    munmap(docs.arena.base, docs.arena.size);
    munmap(docs.depends.base, docs.depends.size);
    return fits;
}
#endif
