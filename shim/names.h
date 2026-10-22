// The names of a .smith file as the source writes them, handed from the hook (shim/hook.cpp, which has the lowering's
// table when a parse ends) to the language server (shim/clangd.cpp, whose messages show the compiler's names).
#pragma once

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace clang {
class DiagnosticsEngine;
}

namespace smith {

// One name the lowering made: `geometry_box_Box` is written `box::Box` in a file that imports the module, `Box` in the
// module itself, and `geometry::box::Box` where the file reaches the module only through another's imports.
struct WrittenName {
    std::string lowered;
    std::string written;
    std::string member;
    std::string module;
    bool direct = true;
};

// A completion that stopped at `x::` (the lowering was given the place of the cursor where a name goes).
struct Qualifier {
    bool present = false;
    bool is_c = false;   // `c::`: the names are C's own
    std::string binding; // box
    std::string prefix;  // geometry_box_
    std::string module;  // geometry::box
    uint32_t line = 0;   // of the cursor, from 0
    uint32_t column = 0;
    std::string partial; // what was typed after the `::`
};

// A call to a generic where the file wrote it: the editor's hover and go-to-definition there are the lowering's own,
// since the compiler sees an instance in its place. Lines and columns are from 0.
struct CallSite {
    uint32_t line = 0;
    uint32_t column = 0;
    uint32_t length = 0;
    std::string hover;
    std::string file; // where the generic is written
    uint32_t definition_line = 0;
    uint32_t definition_column = 0;
};

struct FileNames {
    std::string module; // of the file itself, "" for none
    std::vector<CallSite> calls;
    std::vector<WrittenName> names;
    std::unordered_map<std::string, size_t> index; // by lowered
    // The generics' instances this file made, by the compiler's name: the file each is written in (absolute), and
    // whether its generic is an export of its module.
    struct InstanceHome {
        std::string file;
        bool exported = false;
    };
    std::unordered_map<std::string, InstanceHome> instance_files;

    const WrittenName *find(llvm::StringRef lowered) const {
        auto found = index.find(lowered.str());
        return found == index.end() ? nullptr : &names[found->second];
    }
};

// Puts what shows the diagnostics of `engine` behind the consumer that shows the compiler's, when the translation unit
// being compiled is a .smith file (clang-tidy reports its checks' findings from an engine of its own).
void presentThrough(clang::DiagnosticsEngine &engine);

// What the last parse of the file at `path` that reached its end learned; null for a file that is no .smith file or
// was not parsed.
void publishNames(const std::string &path, std::shared_ptr<const FileNames> names);
std::shared_ptr<const FileNames> namesOf(const std::string &path);

// What the last parse of the file that stopped before its end (a completion stops at the cursor) found at the cursor.
void publishQualifier(const std::string &path, const Qualifier &qualifier);
Qualifier qualifierOf(const std::string &path);

} // namespace smith
