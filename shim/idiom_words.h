// The words and tables of the idiom checks (shim/idiom.h) and of the names clang-tidy judges (shim/tidy.cpp), said by
// runtime/idiom.smith: the C++ is the edge of clang-tidy (the checks, the matchers, the raw lexer, the fix-its) and
// hands plain text to these functions, which return the text of a finding or the answer of a table. Each C function
// writes into a buffer and returns the length of the whole text; `said` calls it again with a larger buffer when the
// text did not fit, and gives a std::string (empty for "nothing").
#pragma once

#include "llvm/ADT/StringRef.h"

#include <cstddef>
#include <string>

extern "C" {
size_t smith_idiom_sized_type(const char *name, size_t length, char *buffer, size_t capacity);
bool smith_idiom_is_type_keyword(const char *name, size_t length);
size_t smith_idiom_sized_integer(const char *run, size_t length, char *buffer, size_t capacity);
size_t smith_idiom_sized_message(const char *spelled, size_t spelled_length, const char *sized, size_t sized_length,
                                 char *buffer, size_t capacity);
size_t smith_idiom_header_module(const char *text, size_t length, char *buffer, size_t capacity);
size_t smith_idiom_library_import_message(const char *text, size_t length, const char *written, size_t written_length,
                                          char *buffer, size_t capacity);
size_t smith_idiom_library_name_message(const char *text, size_t length, const char *spelled, size_t spelled_length,
                                        char *buffer, size_t capacity);
size_t smith_idiom_loop_message(int kind, char *buffer, size_t capacity);
size_t smith_idiom_zero_braces_message(char *buffer, size_t capacity);
size_t smith_idiom_void_message(char *buffer, size_t capacity);
bool smith_idiom_void_parameter_list(const char *before, size_t before_length, const char *next_word,
                                     size_t next_word_length, char next);
bool smith_idiom_conditional_directive(const char *name, size_t length);
size_t smith_idiom_c23_message(const char *name, size_t length, bool next_is_period, bool in_condition, char *buffer,
                               size_t capacity);
size_t smith_idiom_attribute_message(const char *name, size_t length, char *buffer, size_t capacity);
size_t smith_idiom_constexpr_message(bool is_array, const char *name, size_t length, char *buffer, size_t capacity);
size_t smith_idiom_designated_message(const char *name, size_t length, char *buffer, size_t capacity);
size_t smith_idiom_module_message(const char *path, size_t path_length, const char *root, size_t root_length,
                                  char *buffer, size_t capacity);
bool smith_idiom_lowering_name(const char *name, size_t length);
size_t smith_idiom_judged_name_length(const char *name, size_t length);
}

namespace smith_idiom_words {

// The text a call writes, however long: asked again with room for all of it.
template <typename Call> std::string said(Call call) {
    char small[512];
    size_t length = call(small, sizeof small);
    if (length <= sizeof small) return std::string(small, length);
    std::string whole(length, '\0');
    call(whole.data(), whole.size());
    return whole;
}

inline std::string sized_type(llvm::StringRef name) {
    return said([&](char *out, size_t size) { return smith_idiom_sized_type(name.data(), name.size(), out, size); });
}

inline bool is_type_keyword(llvm::StringRef name) {
    return smith_idiom_is_type_keyword(name.data(), name.size());
}

inline std::string sized_integer(llvm::StringRef run) {
    return said([&](char *out, size_t size) { return smith_idiom_sized_integer(run.data(), run.size(), out, size); });
}

inline std::string sized_message(llvm::StringRef spelled, llvm::StringRef sized) {
    return said([&](char *out, size_t size) {
        return smith_idiom_sized_message(spelled.data(), spelled.size(), sized.data(), sized.size(), out, size);
    });
}

inline std::string header_module(llvm::StringRef text) {
    return said([&](char *out, size_t size) { return smith_idiom_header_module(text.data(), text.size(), out, size); });
}

inline std::string library_import_message(llvm::StringRef header, llvm::StringRef written) {
    return said([&](char *out, size_t size) {
        return smith_idiom_library_import_message(header.data(), header.size(), written.data(), written.size(), out,
                                                  size);
    });
}

inline std::string library_name_message(llvm::StringRef header, llvm::StringRef spelled) {
    return said([&](char *out, size_t size) {
        return smith_idiom_library_name_message(header.data(), header.size(), spelled.data(), spelled.size(), out,
                                                size);
    });
}

inline std::string loop_message(int kind) {
    return said([&](char *out, size_t size) { return smith_idiom_loop_message(kind, out, size); });
}

inline std::string zero_braces_message() {
    return said([&](char *out, size_t size) { return smith_idiom_zero_braces_message(out, size); });
}

inline std::string void_message() {
    return said([&](char *out, size_t size) { return smith_idiom_void_message(out, size); });
}

inline bool void_parameter_list(llvm::StringRef before, llvm::StringRef next_word, char next) {
    return smith_idiom_void_parameter_list(before.data(), before.size(), next_word.data(), next_word.size(), next);
}

inline bool conditional_directive(llvm::StringRef name) {
    return smith_idiom_conditional_directive(name.data(), name.size());
}

inline std::string c23_message(llvm::StringRef name, bool next_is_period, bool in_condition) {
    return said([&](char *out, size_t size) {
        return smith_idiom_c23_message(name.data(), name.size(), next_is_period, in_condition, out, size);
    });
}

inline std::string attribute_message(llvm::StringRef name) {
    return said(
        [&](char *out, size_t size) { return smith_idiom_attribute_message(name.data(), name.size(), out, size); });
}

inline std::string constexpr_message(bool is_array, llvm::StringRef name) {
    return said([&](char *out, size_t size) {
        return smith_idiom_constexpr_message(is_array, name.data(), name.size(), out, size);
    });
}

inline std::string designated_message(llvm::StringRef name) {
    return said(
        [&](char *out, size_t size) { return smith_idiom_designated_message(name.data(), name.size(), out, size); });
}

inline std::string module_message(llvm::StringRef path, llvm::StringRef root) {
    return said([&](char *out, size_t size) {
        return smith_idiom_module_message(path.data(), path.size(), root.data(), root.size(), out, size);
    });
}

inline bool lowering_name(llvm::StringRef name) {
    return smith_idiom_lowering_name(name.data(), name.size());
}

inline llvm::StringRef judged_name(llvm::StringRef member) {
    return member.substr(0, smith_idiom_judged_name_length(member.data(), member.size()));
}

} // namespace smith_idiom_words
