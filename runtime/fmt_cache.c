// The cache of `smith fmt` with no arguments (#3081, #3177, knowledge/caches.md): a file that a run found formatted is
// kept as an answer in the one store (runtime/executor_cache.c, `steps/a/`), and the next run leaves that file out. The
// key is the SHA-256 of what the answer is a function of: the formatter's identity (the fmt identity `smith build`
// hashes: the LLVM pack, front/, patches/, shim/format.cpp, smith.c, the fmt runtime and smith's default style; the
// binary's build-id when smith has none), the environment that picks another clang-format, the file's path from the
// project's root and its content, and the `.clang-format`, `_clang-format` and `.clang-format-ignore` files from its
// folder up to /, each by its place in that climb and its content, never its absolute path (a key holds what the step
// reads, not where it is). An answer is written only after the batch that held the file exited 0, so a failure is never
// kept. Pruning and the byte cap are the store's. Linked into smith before runtime/verbs.c, whose `smith fmt` asks it
// about each file.
#include "fmt_cache.h"
#include "core.h"
#include "executor_cache.h"
#include "package.h"
#include "platform_linux.h"

#include <sys/stat.h>

constexpr size_t smith_fc_depth_max = 256; // folders from a file up to /

typedef struct {
    bool enabled;
    char root[PATH_MAX]; // the folder smith fmt runs in, the project's root
    SmithSha256 base;    // the identity and the environment
} SmithFc;

static SmithFc smith_fc;

static void smith_fc_text_add(SmithSha256 *sha, const char *text) {
    smith_sha256_update(sha, (const uint8_t *)text, strlen(text) + 1);
}

// The fmt identity `smith build` hashed (runtime/lint_identity.c), or nullptr when this smith has none.
#ifdef SMITH_LINT_IDENTITY_LINKED
extern const char smith_fmt_identity[];
#endif
static const char *smith_fc_identity(void) {
#ifdef SMITH_LINT_IDENTITY_LINKED
    if (smith_fmt_identity[0] != '\0') return smith_fmt_identity;
#endif
    return nullptr;
}

// The style files and the ignore lists from the folder of the file up to /: clang-format reads the nearest style
// (which may inherit the next), and each `.clang-format-ignore` on the way can take the file out. Each present file
// adds its level (0 is the file's own folder) and its digest, one that cannot be read adds "-".
static bool smith_fc_add_configs(SmithSha256 *sha, const char *file) {
    static const char *const names[] = {".clang-format", "_clang-format", ".clang-format-ignore"};
    char folder[PATH_MAX];
    int written = snprintf(folder, sizeof folder, "%s/%s", smith_fc.root, file);
    if (written <= 0 || (size_t)written >= sizeof folder) return false;
    for (size_t depth = 0; depth < smith_fc_depth_max; depth++) { // bounded by the folders
        char *slash = strrchr(folder, '/');
        if (slash == nullptr) break;
        *slash = '\0';
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            char config[PATH_MAX];
            char hex[smith_sha256_hex_size];
            char label[64];
            struct stat info;
            written = snprintf(config, sizeof config, "%s/%s", folder, names[i]);
            if (written <= 0 || (size_t)written >= sizeof config || stat(config, &info) != 0) continue;
            snprintf(label, sizeof label, "%zu/%s", depth, names[i]);
            smith_fc_text_add(sha, label);
            smith_fc_text_add(sha, S_ISREG(info.st_mode) && smith_sha256_file(config, hex) ? hex : "-");
        }
        if (folder[0] == '\0') break;
    }
    return true;
}

// The key of the file as it is now; false when it cannot be made (the file cannot be read, a path too long).
static bool smith_fc_key(const char *file, char key[smith_sha256_hex_size]) {
    char hex[smith_sha256_hex_size];
    if (!smith_sha256_file(file, hex)) return false;
    SmithSha256 sha = smith_fc.base;
    smith_fc_text_add(&sha, file);
    smith_fc_text_add(&sha, hex);
    if (!smith_fc_add_configs(&sha, file)) return false;
    smith_sha256_final(&sha, key);
    return true;
}

// True when a run found the file, as it is now, formatted. The store touches an answer it serves.
bool smith_fc_formatted(const char *file) {
    char key[smith_sha256_hex_size];
    char answer[8];
    size_t size = 0;
    return smith_fc.enabled && smith_fc_key(file, key) && smith_stepcache_answer_get(key, answer, sizeof answer, &size);
}

// Keeps that the file, as it is now (after `-i` rewrote it), is formatted.
void smith_fc_keep(const char *file) {
    char key[smith_sha256_hex_size];
    if (smith_fc.enabled && smith_fc_key(file, key)) smith_stepcache_answer_put(key, "ok", 2);
}

// Opens the cache for a run of `smith fmt`; false (and nothing cached) when it is off (SMITH_FMT_CACHE=0) or cannot be
// used.
bool smith_fc_open(void) {
    smith_fc = (SmithFc){};
    const char *setting = getenv("SMITH_FMT_CACHE");
    if (setting != nullptr && strcmp(setting, "0") == 0) return false;
    if (getcwd(smith_fc.root, sizeof smith_fc.root) == nullptr) return false;
    char identity[2 * smith_platform_id_bytes + 1] = "";
    const char *fmt_identity = smith_fc_identity();
    if (fmt_identity == nullptr) smith_platform_self_id(identity, sizeof identity);
    smith_sha256_init(&smith_fc.base);
    smith_fc_text_add(&smith_fc.base, "smith-fmt 2\n"); // the answer's kind: another tool's key never equals this one
    smith_fc_text_add(&smith_fc.base, fmt_identity != nullptr ? fmt_identity : identity);
    const char *tool = getenv("SMITH_CLANG_FORMAT"); // a stage0 runs another clang-format
    smith_fc_text_add(&smith_fc.base, tool != nullptr ? tool : "");
    smith_fc.enabled = true;
    return true;
}
