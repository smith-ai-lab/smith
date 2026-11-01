// The edge for front::cimport_cache (#1112 track C, #1130): which files clang reads for one `import <h>;`, the state of
// a file, and a hash. The cache stores a header's declarations beside this list and, on a hit, checks every file in it
// (ccache's "depend mode"), so a cached entry is never stale.
//
// The list comes from a preprocess-only run (no parse, no AST) of the same one-line source shim/cimport.cpp reads, with
// the same arguments, so it names the files that read opened. Paths are absolute, so a hit from another folder checks
// the same files.
//
//   smith_cimport_deps(request, out, capacity)  every file read, each NUL-terminated, one after the other; returns the
//                                               bytes needed (the caller calls again with a bigger buffer when
//                                               `capacity` was too small), or SIZE_MAX when clang refused the source
//   smith_cimport_file_state(path, size, mtime) 0 and the size and the modification time in nanoseconds, -1 when the
//                                               file is not there
//   smith_cimport_hash(bytes, count, out)       a 128-bit hash of bytes (two 64-bit words)
//   smith_cimport_file_hash(path, out)          the same of a file's content; 0, or -1 when it cannot be read
//
// Memory: the preprocess run is on plain malloc, as shim/cimport.cpp's read is (clang keeps process-lifetime state).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/Utils.h"
#include "clang/Tooling/Tooling.h"

extern "C" {
// Keep in step with SmithCimportRequest in shim/cimport.cpp and Request in front/cimport.smith.
typedef struct {
    const char *name;
    int32_t quoted;
    const char *folder;
    const char *flags;
} SmithCimportRequest;
}

struct SmithAllocator;
extern "C" SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator);

namespace {

struct PlainAllocator {
    SmithAllocator *before = smith_handlers_set_allocator(nullptr);
    ~PlainAllocator() {
        smith_handlers_set_allocator(before);
    }
};

// Every file the preprocessor opened, system headers included.
class Everything : public clang::DependencyCollector {
  public:
    bool needSystemDependencies() override {
        return true;
    }
    bool sawDependency(llvm::StringRef, bool, bool, bool, bool, bool is_missing) override {
        return !is_missing;
    }
};

class Collect : public clang::PreprocessOnlyAction {
  public:
    explicit Collect(std::vector<std::string> *files) : files_(files) {}
    void ExecuteAction() override {
        auto collector = std::make_shared<Everything>();
        collector->attachToPreprocessor(getCompilerInstance().getPreprocessor());
        clang::PreprocessOnlyAction::ExecuteAction();
        for (const std::string &file : collector->getDependencies())
            files_->push_back(file);
    }

  private:
    std::vector<std::string> *files_;
};

std::vector<std::string> lines(const char *text) {
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

// The absolute, normalized path of an existing file; empty when there is none.
std::string absolute(const std::string &path) {
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved) == nullptr) return "";
    return resolved;
}

void mix(uint64_t *state, const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        state[0] = (state[0] ^ bytes[i]) * 0x100000001b3ULL;          // FNV-1a
        state[1] = (state[1] + bytes[i] + 1) * 0x9E3779B97F4A7C15ULL; // a second, unrelated mix
        state[1] ^= state[1] >> 29;
    }
}

void finish(uint64_t *state, size_t count, uint64_t *out) {
    uint64_t a = state[0] ^ (static_cast<uint64_t>(count) * 0xff51afd7ed558ccdULL);
    uint64_t b = state[1] ^ (static_cast<uint64_t>(count) * 0xc4ceb9fe1a85ec53ULL);
    a ^= a >> 33;
    a *= 0xff51afd7ed558ccdULL;
    a ^= a >> 33;
    b ^= b >> 33;
    b *= 0xc4ceb9fe1a85ec53ULL;
    b ^= b >> 33;
    out[0] = a;
    out[1] = b;
}

} // namespace

extern "C" size_t smith_cimport_deps(const SmithCimportRequest *request, char *out, size_t capacity) {
    PlainAllocator plain;
    std::string header = request->name;
    std::string code = std::string("#include ") + (request->quoted != 0 ? "\"" : "<") + header +
                       (request->quoted != 0 ? "\"" : ">") + "\n";
    std::vector<std::string> args = {"-x", "c", "-std=gnu23", "-fno-color-diagnostics"};
    if (request->quoted != 0 && request->folder != nullptr && request->folder[0] != '\0') {
        args.push_back("-iquote");
        args.push_back(request->folder);
    }
    for (const std::string &f : lines(request->flags))
        args.push_back(f);
    std::vector<std::string> files;
    bool ran = clang::tooling::runToolOnCodeWithArgs(std::make_unique<Collect>(&files), code, args, "smith_cimport.c",
                                                     "smith-cimport");
    if (!ran) return SIZE_MAX;
    std::string joined;
    for (const std::string &file : files) {
        std::string path = absolute(file);
        if (path.empty()) continue; // the virtual source itself, or a file that went away
        joined += path;
        joined += '\0';
    }
    // The NULs between the paths are the list's own separators.
    if (joined.size() <= capacity && out != nullptr)
        std::memcpy(out, joined.data(), joined.size()); // NOLINT(bugprone-not-null-terminated-result)
    return joined.size();
}

extern "C" int smith_cimport_file_state(const char *path, uint64_t *size, int64_t *mtime_ns) {
    struct stat status;
    if (::stat(path, &status) != 0) return -1;
    *size = static_cast<uint64_t>(status.st_size);
#if defined(__APPLE__)
    *mtime_ns = static_cast<int64_t>(status.st_mtimespec.tv_sec) * 1000000000LL + status.st_mtimespec.tv_nsec;
#else
    *mtime_ns = static_cast<int64_t>(status.st_mtim.tv_sec) * 1000000000LL + status.st_mtim.tv_nsec;
#endif
    return 0;
}

extern "C" void smith_cimport_hash(const void *bytes, size_t count, uint64_t *out) {
    uint64_t state[2] = {0xcbf29ce484222325ULL, 0x243F6A8885A308D3ULL};
    mix(state, static_cast<const uint8_t *>(bytes), count);
    finish(state, count, out);
}

extern "C" int smith_cimport_file_hash(const char *path, uint64_t *out) {
    FILE *file = std::fopen(path, "rb");
    if (file == nullptr) return -1;
    uint64_t state[2] = {0xcbf29ce484222325ULL, 0x243F6A8885A308D3ULL};
    uint8_t buffer[4096];
    size_t total = 0;
    for (;;) {
        size_t got = std::fread(buffer, 1, sizeof buffer, file);
        if (got == 0) break;
        mix(state, buffer, got);
        total += got;
    }
    bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) return -1;
    finish(state, total, out);
    return 0;
}
