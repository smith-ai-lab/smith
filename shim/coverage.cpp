// Source-based coverage, behind a C interface: the profiles a test's processes wrote and the programs they ran, as the
// lines and functions the test executed. This is what llvm-profdata merge and llvm-cov export -format=lcov do, in
// smith's own process, so a project's `smith test --changed` (#456) needs no LLVM tools installed beside smith
// (runtime/coverage.c).
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/ProfileData/InstrProfWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"

namespace {

void say(char *problem, size_t size, const std::string &text) {
    if (problem != nullptr && size > 0) std::snprintf(problem, size, "%s", text.c_str());
}

// The names of a list file, one a line.
std::vector<std::string> read_list(const char *path) {
    std::vector<std::string> names;
    std::FILE *file = std::fopen(path, "r");
    if (file == nullptr) return names;
    char line[4096];
    while (std::fgets(line, sizeof line, file) != nullptr) {
        size_t length = std::strcspn(line, "\r\n");
        if (length > 0) names.emplace_back(line, length);
    }
    std::fclose(file);
    return names;
}

// Sums the raw profiles into one indexed profile at `output` (what `llvm-profdata merge -sparse` writes).
bool merge(const std::vector<std::string> &raw, const char *output, std::string &problem) {
    llvm::InstrProfWriter writer(true);
    bool any = false;
    for (const std::string &path : raw) {
        auto reader = llvm::InstrProfReader::create(path, *llvm::vfs::getRealFileSystem());
        if (!reader) {
            problem = path + ": " + llvm::toString(reader.takeError());
            return false;
        }
        for (auto &record : **reader) {
            writer.addRecord(std::move(record), 1, [](llvm::Error error) { llvm::consumeError(std::move(error)); });
            any = true;
        }
        if ((*reader)->hasError()) {
            problem = path + ": the profile is damaged";
            return false;
        }
    }
    if (!any) {
        problem = "the profiles hold no counters";
        return false;
    }
    std::error_code code;
    llvm::raw_fd_ostream out(output, code, llvm::sys::fs::OF_None);
    if (code) {
        problem = std::string(output) + ": " + code.message();
        return false;
    }
    if (llvm::Error error = writer.write(out)) {
        problem = llvm::toString(std::move(error));
        return false;
    }
    return true;
}

// Whether `file` lies below `root` (a path of the project), the only files a record names.
bool below(const std::string &file, const std::string &root) {
    return file.size() > root.size() && file.compare(0, root.size(), root) == 0 && file[root.size()] == '/';
}

void export_file(std::FILE *out, const llvm::coverage::CoverageMapping &mapping, llvm::StringRef file,
                 const std::string &shown) {
    std::fprintf(out, "SF:%s\n", shown.c_str());
    for (const auto &function : mapping.getCoveredFunctions(file)) {
        if (function.CountedRegions.empty()) continue;
        std::fprintf(out, "FN:%u,%s\n", function.CountedRegions.front().LineStart, function.Name.c_str());
    }
    for (const auto &function : mapping.getCoveredFunctions(file)) {
        if (function.CountedRegions.empty()) continue;
        std::fprintf(out, "FNDA:%llu,%s\n", (unsigned long long)function.ExecutionCount, function.Name.c_str());
    }
    llvm::coverage::CoverageData data = mapping.getCoverageForFile(file);
    if (data.begin() != data.end()) {
        for (const auto &line : llvm::coverage::getLineCoverageStats(data)) {
            if (line.isMapped())
                std::fprintf(out, "DA:%u,%llu\n", line.getLine(), (unsigned long long)line.getExecutionCount());
        }
    }
    std::fputs("end_of_record\n", out);
}

} // namespace

extern "C" {

// The raw profiles named in the file `list`, merged, read against `binaries` (the programs they were written by), and
// written to `lcov` as llvm-cov's lcov: `SF:`, `FN:<line>,<name>`, `FNDA:<count>,<name>`, `DA:<line>,<count>` and
// `end_of_record`, for the files below `root`. `profdata` is a scratch file. Returns 0, or 1 with `problem` set.
int smith_coverage_lcov(const char *list, const char *const *binaries, size_t binary_count, const char *root,
                        const char *profdata, const char *lcov, char *problem, size_t problem_size) {
    std::string reason;
    std::vector<std::string> raw = read_list(list);
    if (raw.empty() || !merge(raw, profdata, reason)) {
        say(problem, problem_size, reason.empty() ? "no profiles" : reason);
        return 1;
    }
    std::vector<llvm::StringRef> objects;
    for (size_t i = 0; i < binary_count; i++)
        objects.push_back(binaries[i]);
    auto mapping =
        llvm::coverage::CoverageMapping::load(objects, llvm::StringRef(profdata), *llvm::vfs::getRealFileSystem());
    if (!mapping) {
        say(problem, problem_size, llvm::toString(mapping.takeError()));
        return 1;
    }
    std::FILE *out = std::fopen(lcov, "w");
    if (out == nullptr) {
        say(problem, problem_size, std::string("cannot write ") + lcov);
        return 1;
    }
    // A source is named as the compiler was given it, which may reach the project through a link (/var on macOS is
    // /private/var): the file is shown as a path below `root` as the caller spells it.
    std::string project = root;
    std::string real_project = project;
    char resolved[PATH_MAX];
    if (realpath(root, resolved) != nullptr) real_project = resolved;
    for (llvm::StringRef file : (*mapping)->getUniqueSourceFiles()) {
        std::string name = file.str();
        std::string shown;
        if (below(name, project)) {
            shown = name;
        } else if (realpath(name.c_str(), resolved) != nullptr && below(resolved, real_project)) {
            shown = project + std::string(resolved).substr(real_project.size());
        } else {
            continue;
        }
        export_file(out, **mapping, file, shown);
    }
    return std::fclose(out) == 0 ? 0 : 1;
}

} // extern "C"
