// The proof file of a `.smith` source (issue #221): `smith prove` writes, for each source, the lines whose index and
// cut checks it proved can never fail, and the lowering of a release build (-O1 or more; debug is -O0) leaves those
// checks out. Both sides are in this header, so they cannot disagree about the file, its name or its key.
//
//   .smith/proved/<source's path from the project folder>.pts
//     smith-proved <key>           the key: SHA-256 of the prover's identity and of the source's bytes
//     <line>                       one line number each, ascending: every check of that line is proved
//
// A missing file, another key (the source was edited, or another smith proved it) or an unreadable line keeps every
// check, which is always sound. The prover's identity is its version and LLVM's; the lowering's version is part of it,
// since the lowering decides which line a check is on.
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

// Bump when a change to the prover can change what a function proves or how it fails.
#define SMITH_PROVER_VERSION "9"
// Bump when the lowering changes the line a check is reported at, or which checks a line has.
#define SMITH_PROVED_LOWERING_VERSION "1"

namespace proved {

// What a check carries in the IR, which the prover compares to the text the lowering writes for the same line.
inline const char *check_text() {
    return "an index or a slice is in range";
}

inline std::string key_of(const std::string &text) {
    llvm::SHA256 hash;
    hash.update(std::string("smith-proved-" SMITH_PROVER_VERSION "-" SMITH_PROVED_LOWERING_VERSION " llvm-") +
                LLVM_VERSION_STRING + "\n");
    hash.update(text);
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

// The lines the file at `path` says are proved for a source whose text is `text`; empty when it says nothing for it.
inline std::set<unsigned> read(const std::string &path, const std::string &text) {
    std::set<unsigned> lines;
    std::ifstream file(path);
    std::string word, key;
    if (!(file >> word >> key) || word != "smith-proved" || key != key_of(text)) return lines;
    unsigned line = 0;
    while (file >> line)
        lines.insert(line);
    return lines;
}

// Writes the proof file of a source (the bytes make the same file for the same lines); false when it cannot.
inline bool write(const std::string &path, const std::string &text, const std::set<unsigned> &lines) {
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ignored);
    std::string temporary = path + ".tmp" + std::to_string(getpid());
    {
        std::ofstream file(temporary);
        file << "smith-proved " << key_of(text) << "\n";
        for (unsigned line : lines)
            file << line << "\n";
        if (!file) return false;
    }
    std::filesystem::rename(temporary, path, ignored);
    return !ignored;
}

inline std::string slurp(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace proved
