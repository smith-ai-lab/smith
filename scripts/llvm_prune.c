// `smith build llvm` runs this when it has built an LLVM (issue #618): the folders of the other LLVM builds under the
// cache go (11 GB each, six of them piled up), and with each its Enzyme. Two stay: the one just built and the one the
// smith that runs the build was linked against (the previous release's), which a build of the previous commit still
// needs. Everything removed is built again from the pinned sources by `smith build llvm`. Plain C23, libc only, no
// allocation.
//
// Usage: llvm_prune LLVM_FOLDER KEEP_NAME KEEP_IDENTITY
//   LLVM_FOLDER     <cache>/smith/llvm, with one folder per build (23.1.3-r12) holding install/lib/smith-clang.id
//   KEEP_NAME       the folder just built (23.1.3-r12)
//   KEEP_IDENTITY   the contents of the smith-clang.id of the previous release's LLVM ("llvm-23.1.3 <hash>")
// A folder with no id file (a build in progress, or not an LLVM at all) is never touched. Enzyme's folders are the
// siblings <cache>/smith/enzyme/<version>-<name>. Prints each folder it removes.
#if defined(__linux__)
#define _GNU_SOURCE // glibc declares nftw only for X/Open or GNU code, not plain -std=c23
#endif
#include <dirent.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

constexpr int walk_files = 64;
constexpr size_t id_size = 256;

static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *where) {
    (void)info;
    (void)type;
    (void)where;
    return remove(path);
}

static void remove_tree(const char *path) {
    if (nftw(path, remove_entry, walk_files, FTW_DEPTH | FTW_PHYS) != 0) remove(path);
}

// The first line of the build's identity file; false when there is none.
static bool read_identity(const char *folder, char id[id_size]) {
    char path[PATH_MAX];
    int written = snprintf(path, sizeof path, "%s/install/lib/smith-clang.id", folder);
    if (written <= 0 || (size_t)written >= sizeof path) return false;
    FILE *file = fopen(path, "r");
    if (file == nullptr) return false;
    bool read = fgets(id, (int)id_size, file) != nullptr;
    fclose(file);
    id[strcspn(id, "\n")] = '\0';
    return read;
}

// <enzyme>/<version>-<name> for the build <name>: Enzyme is keyed by its version and the LLVM it was built against.
static void remove_enzyme(const char *enzyme, const char *name) {
    DIR *dir = opendir(enzyme);
    if (dir == nullptr) return;
    size_t suffix = strlen(name);
    for (size_t guard = 0; guard < 100000; guard++) { // bounded by the entries
        const struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        size_t length = strlen(entry->d_name);
        bool built_against = length > suffix + 1 && entry->d_name[length - suffix - 1] == '-' &&
                             strcmp(entry->d_name + length - suffix, name) == 0;
        char path[PATH_MAX];
        if (!built_against || snprintf(path, sizeof path, "%s/%s", enzyme, entry->d_name) >= (int)sizeof path) continue;
        remove_tree(path);
        printf("removed %s\n", path);
    }
    closedir(dir);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fputs("usage: llvm_prune LLVM_FOLDER KEEP_NAME KEEP_IDENTITY\n", stderr);
        return 2;
    }
    const char *llvm = argv[1];
    const char *keep_name = argv[2];
    const char *keep_identity = argv[3];
    char enzyme[PATH_MAX];
    if (snprintf(enzyme, sizeof enzyme, "%s/../enzyme", llvm) >= (int)sizeof enzyme) return 2;
    DIR *dir = opendir(llvm);
    if (dir == nullptr) return 0;
    for (size_t guard = 0; guard < 100000; guard++) { // bounded by the entries
        const struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        char folder[PATH_MAX];
        char id[id_size];
        if (entry->d_name[0] == '.' || strcmp(entry->d_name, keep_name) == 0) continue;
        if (snprintf(folder, sizeof folder, "%s/%s", llvm, entry->d_name) >= (int)sizeof folder) continue;
        if (!read_identity(folder, id) || strcmp(id, keep_identity) == 0) continue;
        remove_tree(folder);
        printf("removed %s\n", folder);
        remove_enzyme(enzyme, entry->d_name);
    }
    closedir(dir);
    return 0;
}
