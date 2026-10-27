// The proof file of a `.smith` source (issue #221): `smith prove` writes, for each source, the lines whose index and
// cut checks and whose `contract::ensures`, `invariant` and `assert` checks (issue #325) it proved can never fail, and
// the lowering of a release build (-O1 or more; debug is -O0) leaves those checks out. Both sides are in this header,
// so they cannot disagree about the file, its name or its key.
//
//   .smith/proved/<source's path from the project folder>.pts
//     smith-proved <key>           the key: SHA-256 of the prover's identity, the source's bytes and the bytes of every
//                                  file in the `dep` lines below, in that order
//     dep <path>                   each other file the proof read a function or a global from (a callee whose
//                                  `requires` and `ensures` the proof assumed, a helper it inlined, a generic it
//                                  instanced); the path is relative to the project folder when it is inside it
//     <line>                       one line number each, ascending: every check of that line is proved
//
// A missing file, another key (the source or any file it depends on was edited, or another smith proved it), a file of
// the `dep` lines that cannot be read, or an unreadable line keeps every check, which is always sound. The build also
// makes each `dep` file an input of the compile (`smith_object`), so a change to a callee runs the compile again and
// the lowering finds the key stale. The prover's identity is its version and LLVM's; the lowering's version is part of
// it, since the lowering decides which line a check is on.
#pragma once

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/SHA256.h"

#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Bump when a change to the prover can change what a function proves or how it fails.
#define SMITH_PROVER_VERSION "10"
// Bump when the lowering changes the line a check is reported at, or which checks a line has, or the file's format.
#define SMITH_PROVED_LOWERING_VERSION "3"

namespace proved {

// What a check carries in the IR, which the prover compares to the text the lowering writes for the same line.
inline const char *check_text() {
    return "an index or a slice is in range";
}

// The text of the files a proof depends on, in the order of `dependencies`; false when one cannot be read.
inline bool read_dependencies(const std::vector<std::string> &dependencies, std::string &texts) {
    for (const std::string &dependency : dependencies) {
        std::ifstream file(dependency, std::ios::binary);
        if (!file) return false;
        std::ostringstream text;
        text << file.rdbuf();
        texts += "\ndep " + dependency + "\n" + text.str();
    }
    return true;
}

// The key of a source's proof: `text` is the source and `dependencies` the texts read_dependencies made.
inline std::string key_of(const std::string &text, const std::string &dependencies = "") {
    llvm::SHA256 hash;
    hash.update(std::string("smith-proved-" SMITH_PROVER_VERSION "-" SMITH_PROVED_LOWERING_VERSION " llvm-") +
                LLVM_VERSION_STRING + "\n");
    hash.update(text);
    hash.update(dependencies);
    std::array<uint8_t, 32> digest = hash.final();
    static const char *const hex = "0123456789abcdef";
    std::string name;
    for (uint8_t byte : digest) {
        name += hex[byte >> 4];
        name += hex[byte & 15];
    }
    return name;
}

// `.smith/proved/<path>.pts` for a source under the working folder of a project (one with a `.smith` folder), else "".
inline std::string path_of(const std::string &file) {
    struct stat info{};
    if (file.empty() || stat(".smith", &info) != 0 || !S_ISDIR(info.st_mode)) return "";
    std::error_code ignored;
    std::filesystem::path real = std::filesystem::canonical(file, ignored);
    std::filesystem::path base = std::filesystem::canonical(".", ignored);
    if (real.empty() || base.empty()) return "";
    std::filesystem::path relative = real.lexically_relative(base);
    if (relative.empty() || *relative.begin() == "..") return "";
    return ".smith/proved/" + relative.string() + ".pts";
}

// The dependencies a proof file names (the `dep` lines), in the order it names them.
inline std::vector<std::string> dependencies_of(const std::string &path) {
    std::vector<std::string> named;
    std::ifstream file(path);
    std::string row;
    while (std::getline(file, row)) {
        if (row.compare(0, 4, "dep ") == 0) named.push_back(row.substr(4));
    }
    return named;
}

// The lines the file at `path` says are proved for a source whose text is `text`; empty when it says nothing for it
// (no file, another key, a dependency that is gone or changed, anything it cannot read).
inline std::set<unsigned> read(const std::string &path, const std::string &text) {
    std::set<unsigned> lines;
    std::ifstream file(path);
    std::string row;
    if (!std::getline(file, row) || row.compare(0, 13, "smith-proved ") != 0) return lines;
    std::string key = row.substr(13);
    while (std::getline(file, row)) {
        if (row.compare(0, 4, "dep ") == 0) continue;
        if (row.empty() || row[0] < '0' || row[0] > '9') return std::set<unsigned>();
        lines.insert(static_cast<unsigned>(std::strtoul(row.c_str(), nullptr, 10)));
    }
    std::string texts;
    if (!read_dependencies(dependencies_of(path), texts) || key != key_of(text, texts)) return std::set<unsigned>();
    return lines;
}

// Writes the proof file of a source (the bytes make the same file for the same lines and dependencies); false when it
// cannot, or when a dependency cannot be read: a proof that cannot be keyed is not written.
inline bool write(const std::string &path, const std::string &text, const std::set<unsigned> &lines,
                  const std::set<std::string> &dependencies) {
    std::vector<std::string> sorted(dependencies.begin(), dependencies.end());
    std::string texts;
    if (!read_dependencies(sorted, texts)) return false;
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ignored);
    std::string temporary = path + ".tmp" + std::to_string(getpid());
    {
        std::ofstream file(temporary);
        file << "smith-proved " << key_of(text, texts) << "\n";
        for (const std::string &dependency : sorted)
            file << "dep " << dependency << "\n";
        for (unsigned line : lines)
            file << line << "\n";
        if (!file) return false;
    }
    std::filesystem::rename(temporary, path, ignored);
    return !ignored;
}

// A file's path as the proof file names it: relative to the project folder when inside it, else absolute; "" when the
// file does not exist (a debug-info name of no real file).
inline std::string dependency_path(const std::string &directory, const std::string &name) {
    std::filesystem::path path(name);
    if (path.is_relative() && !directory.empty()) path = std::filesystem::path(directory) / path;
    std::error_code ignored;
    std::filesystem::path real = std::filesystem::canonical(path, ignored);
    if (real.empty()) return "";
    std::filesystem::path base = std::filesystem::canonical(".", ignored);
    std::filesystem::path relative = base.empty() ? std::filesystem::path() : real.lexically_relative(base);
    if (!relative.empty() && *relative.begin() != "..") return relative.string();
    return real.string();
}

inline std::string slurp(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace proved
