// A SessionStart hook with matcher `compact` (#3383): after a compaction the text of the skill a session loaded earlier
// can be gone, so this puts the session's role skill (`smith-dev-lead` or `smith-dev-runner`; a session has at most
// one) back in context. Claude Code adds the hook's standard output to the model's context.
//
// Usage: reload_role_skill [SKILLS_DIR]      (the hook's JSON input on standard input)
//
// Reads `transcript_path` from the input, scans that transcript for a `Skill` tool call naming a role skill (the last
// one wins), and prints "Your role skill, reloaded after compaction:" and the whole SKILL.md of it from SKILLS_DIR
// (default `$CLAUDE_PROJECT_DIR/.claude/skills`, else `.claude/skills`). A session that loaded neither prints nothing.
// It never fails a session: every error prints nothing and exits 0. Plain C23, libc only, no allocation, no network.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int input_size = 65536;
constexpr int chunk_size = 65536;
constexpr int overlap = 128;
constexpr int path_size = 4096;

static const char *const roles[] = {"smith-dev-lead", "smith-dev-runner"};
constexpr int role_count = 2;

// The value of the JSON string field `"transcript_path"` in `input`, unescaped for `\/` and `\\`, into `out`.
static bool transcript_path(const char *input, char *out, int size) {
    const char *key = strstr(input, "\"transcript_path\"");
    if (key == nullptr) return false;
    const char *p = strchr(key + 17, '"');
    if (p == nullptr) return false;
    int n = 0;
    for (p++; *p != '"' && *p != '\0'; p++) {
        if (*p == '\\' && (p[1] == '/' || p[1] == '\\'))
            p++;
        else if (*p == '\\')
            return false;
        if (n + 1 >= size) return false;
        out[n++] = *p;
    }
    out[n] = '\0';
    return *p == '"' && n > 0;
}

// The offset of the last of `needle` in `buf`, or -1.
static long last_match(const char *buf, long len, const char *needle) {
    long best = -1;
    long m = (long)strlen(needle);
    for (long i = 0; i + m <= len; i++)
        if (buf[i] == needle[0] && memcmp(buf + i, needle, (size_t)m) == 0) best = i;
    return best;
}

// Which role skill the transcript loaded last, as an index into `roles`, or -1.
static int loaded_role(FILE *file) {
    static char buf[chunk_size + overlap];
    long base = 0, kept = 0;
    long best_at = -1;
    int best = -1;
    for (;;) {
        size_t got = fread(buf + kept, 1, chunk_size, file);
        if (got == 0) break;
        long len = kept + (long)got;
        for (int r = 0; r < role_count; r++) {
            char a[96], b[96];
            snprintf(a, sizeof a, "\"input\":{\"skill\":\"%s\"", roles[r]);
            snprintf(b, sizeof b, "Launching skill: %s", roles[r]);
            long at = last_match(buf, len, a);
            long at2 = last_match(buf, len, b);
            if (at2 > at) at = at2;
            if (at >= 0 && base + at >= best_at) {
                best_at = base + at;
                best = r;
            }
        }
        kept = len < overlap ? len : overlap;
        memmove(buf, buf + len - kept, (size_t)kept);
        base += len - kept;
    }
    return best;
}

int main(int argc, char **argv) {
    static char input[input_size];
    size_t got = fread(input, 1, sizeof input - 1, stdin);
    input[got] = '\0';
    char path[path_size];
    if (!transcript_path(input, path, sizeof path)) return 0;
    FILE *transcript = fopen(path, "rb");
    if (transcript == nullptr) return 0;
    int role = loaded_role(transcript);
    fclose(transcript);
    if (role < 0) return 0;
    char skill[path_size];
    const char *dir = argc > 1 ? argv[1] : nullptr;
    const char *project = getenv("CLAUDE_PROJECT_DIR");
    if (dir != nullptr)
        snprintf(skill, sizeof skill, "%s/%s/SKILL.md", dir, roles[role]);
    else if (project != nullptr)
        snprintf(skill, sizeof skill, "%s/.claude/skills/%s/SKILL.md", project, roles[role]);
    else
        snprintf(skill, sizeof skill, ".claude/skills/%s/SKILL.md", roles[role]);
    FILE *text = fopen(skill, "rb");
    if (text == nullptr) return 0;
    printf("Your role skill, reloaded after compaction:\n");
    char block[4096];
    size_t n;
    while ((n = fread(block, 1, sizeof block, text)) > 0)
        fwrite(block, 1, n, stdout);
    fclose(text);
    return 0;
}
