// The second edge to clang for `import <h>;` (#1112 track C, #1130): what front::cimport_cache needs to know about the
// world, beside shim/cimport.cpp's read of a header.
//
//   smith_cimport_identity   the clang that reads headers (its version and revision), for the cache folder: the
//                            declarations depend on the clang that read them.
//   smith_cimport_deps       the files clang opens to read one header, one absolute path on each line: the header
//                            and everything it includes. The cache records each with its size, mtime and hash, and a
//                            hit checks them (ccache's "depend mode").
//   smith_cimport_stat       a file's size and modification time in nanoseconds.
//
// The request is the same as smith_cimport_read's (the struct is repeated here, in step with shim/cimport.cpp and
// front/cimport.smith). Text goes into the caller's buffer; when it is too small the call returns the bytes needed and
// the caller calls again.
#include <sys/stat.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"

extern "C" {
// Keep in step with shim/cimport.cpp.
typedef struct {
    const char *name;
    int32_t quoted;
    const char *folder;
    const char *flags;
} SmithCimportRequest;
}

// lib/handlers.c: nullptr is plain malloc. Clang keeps state for the life of the process, so it must not live in a
// caller's arena (the same reason as in shim/cimport.cpp).
struct SmithAllocator;
extern "C" SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator);

namespace {

struct PlainAllocator {
    SmithAllocator *before = smith_handlers_set_allocator(nullptr);
    ~PlainAllocator() {
        smith_handlers_set_allocator(before);
    }
};

std::vector<std::string> split_lines(const char *text) {
    std::vector<std::string> out;
    std::string line;
    for (const char *p = text; p != nullptr && *p != '\0'; p++) {
        if (*p == '\n') {
            if (!line.empty()) out.push_back(line);
            line.clear();
        } else {
            line += *p;
        }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// Copies `text` into the caller's buffer when it fits; returns the bytes the text needs (with its NUL).
size_t give(const std::string &text, char *out, size_t capacity) {
    size_t needed = text.size() + 1;
    if (out != nullptr && capacity >= needed) std::memcpy(out, text.c_str(), needed);
    return needed;
}

} // namespace

extern "C" size_t smith_cimport_identity(char *out, size_t capacity) {
    PlainAllocator plain;
    return give(clang::getClangFullVersion(), out, capacity);
}

extern "C" int32_t smith_cimport_stat(const char *path, uint64_t *size, uint64_t *mtime_ns) {
    struct stat info;
    if (::stat(path, &info) != 0) return -1;
    *size = static_cast<uint64_t>(info.st_size);
#ifdef __APPLE__
    const struct timespec &time = info.st_mtimespec;
#else
    const struct timespec &time = info.st_mtim;
#endif
    *mtime_ns = static_cast<uint64_t>(time.tv_sec) * 1000000000ULL + static_cast<uint64_t>(time.tv_nsec);
    return 0;
}

extern "C" size_t smith_cimport_deps(const SmithCimportRequest *request, char *out, size_t capacity) {
    PlainAllocator plain;
    std::string header = request->name;
    std::string code = std::string("#include ") + (request->quoted != 0 ? "\"" : "<") + header +
                       (request->quoted != 0 ? "\"" : ">") + "\n";
    std::vector<std::string> args = {"-x", "c", "-std=gnu23", "-fno-color-diagnostics", "-fsyntax-only"};
    if (request->quoted != 0 && request->folder != nullptr && request->folder[0] != '\0') {
        args.push_back("-iquote");
        args.push_back(request->folder);
    }
    for (const std::string &f : split_lines(request->flags))
        args.push_back(f);
    std::unique_ptr<clang::ASTUnit> unit = clang::tooling::buildASTFromCodeWithArgs(
        code, args, "smith_cimport.c", "smith-cimport", std::make_shared<clang::PCHContainerOperations>(),
        clang::tooling::getClangStripDependencyFileAdjuster());
    std::string text;
    if (unit != nullptr) {
        clang::SourceManager &sources = unit->getSourceManager();
        std::set<std::string> seen;
        for (unsigned i = 1; i < sources.local_sloc_entry_size(); i++) {
            const clang::SrcMgr::SLocEntry &entry = sources.getLocalSLocEntry(i);
            if (!entry.isFile()) continue;
            clang::OptionalFileEntryRef file = entry.getFile().getContentCache().OrigEntry;
            if (!file) continue;
            std::string name = file->getFileEntry().tryGetRealPathName().str();
            if (name.empty()) name = file->getName().str();
            if (name == "smith_cimport.c" || !seen.insert(name).second) continue;
            text += name;
            text += '\n';
        }
    }
    return give(text, out, capacity);
}
