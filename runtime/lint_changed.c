// `smith lint --changed [--base=<ref>]` (issue #638): lint only what a change can break, so a preflight does not pay
// for the whole project. Included by smith.c after runtime/changed.c, whose graph it uses (nothing in changed.c is
// edited).
//
// The files linted are the project's own sources the build compiles, narrowed to those that changed against the base
// (where the branch left origin/main, the working tree and untracked files included) and those that include or import
// one that changed, transitively (the graph of `smith test --changed`). A `.smith` library's generated header
// (`<smith/x.h>`, which no git file is) counts as the library: a file that names it is an includer of
// lib/smith/x.smith. Whole project instead (the train lints everything in any case): when .clang-tidy, .clang-format,
// build.c or the default configuration changed, when git or the graph cannot say, or when more than half of the files
// are affected.

static bool smith_lint_changed_whole_path(const char *path) {
    static const char *const files[] = {".clang-tidy", ".clang-format", ".clang-format-ignore", "build.c", nullptr};
    for (size_t i = 0; files[i] != nullptr; i++) { // a short list
        if (strcmp(path, files[i]) == 0) return true;
    }
    return smith_changed_starts(path, "templates/defaults/");
}

// Whether the text of `file` names the generated header of a library that changed: `smith/<name>.h` of a hit
// lib/smith/<name>.smith.
static bool smith_lint_changed_names_header(const char *file) {
    if (smith_changed_read(file) == 0) return false;
    for (size_t i = 0; i < smith_changed_file_count; i++) { // bounded by the files
        const char *path = smith_changed_files[i];
        if (!smith_changed_hit[i] || !smith_changed_starts(path, "lib/smith/") || !smith_changed_ends(path, ".smith"))
            continue;
        char header[PATH_MAX];
        snprintf(header, sizeof header, "smith/%.*s.h", (int)(strlen(path) - strlen("lib/smith/") - 6),
                 path + strlen("lib/smith/"));
        if (strstr(smith_changed_text, header) != nullptr) return true;
    }
    return false;
}

// Narrows `files` (`count` of them) in place to those a change affects; returns the new count, which is `count` when
// the whole project is linted. Says why on stderr.
static size_t smith_lint_changed(const char **files, size_t count, const char *given) {
    char base[256];
    if (getcwd(smith_cov_root, sizeof smith_cov_root) == nullptr || !smith_changed_base(given, base, sizeof base)) {
        fputs("smith lint --changed: no base (no origin/main or main to compare with): the whole project\n", stderr);
        return count;
    }
    static const char *const list[] = {"ls-files", "--cached", "--others", "--exclude-standard", nullptr};
    static const char *const others[] = {"ls-files", "--others", "--exclude-standard", nullptr};
    const char *const diff[] = {"diff", "--name-status", "--no-renames", base, "--", nullptr};
    if (!smith_changed_git(list, smith_changed_on_file) || !smith_changed_git(diff, smith_changed_on_diff) ||
        smith_changed_overflow) {
        fputs("smith lint --changed: git could not say what changed: the whole project\n", stderr);
        return count;
    }
    (void)smith_changed_git(others, smith_changed_on_untracked);
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        if (smith_lint_changed_whole_path(smith_changed_paths[p])) {
            fprintf(stderr, "smith lint --changed: %s changed: the whole project\n", smith_changed_paths[p]);
            return count;
        }
    }
    smith_changed_build_graph();
    if (smith_changed_overflow) {
        fputs("smith lint --changed: the graph overflowed: the whole project\n", stderr);
        return count;
    }
    size_t kept = 0;
    for (size_t i = 0; i < count; i++) { // bounded by the files
        int at = smith_changed_find(files[i]);
        if ((at >= 0 && smith_changed_hit[at]) || smith_lint_changed_names_header(files[i])) files[kept++] = files[i];
    }
    if (kept * 2 > count) {
        fprintf(stderr, "smith lint --changed: %zu of %zu files are affected: the whole project\n", kept, count);
        return count;
    }
    fprintf(stderr, "smith lint --changed: %zu of %zu files changed or include something that did\n", kept, count);
    return kept;
}
