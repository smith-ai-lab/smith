// The words of `smith prove`'s report, said by runtime/prove_report.smith: the C++ of the prover (prove_ir.cpp,
// prove_alive.cpp) walks LLVM's IR and Alive2's states and hands plain facts (runtime/prove_edge.h) to these functions,
// which return the text. Each C function writes into a buffer and returns the length of the whole text; the wrappers
// below call it again with a larger buffer when the text did not fit, and give a std::string.
#pragma once

#include <string>

#include "../runtime/prove_edge.h"

extern "C" {
size_t smith_prove_as_written(SmithProveText name, char *buffer, size_t capacity);
size_t smith_prove_strip_group_numbers(SmithProveText text, char *buffer, size_t capacity);
size_t smith_prove_kind_name(int kind, char *buffer, size_t capacity);
size_t smith_prove_instruction_line(const SmithProveInstruction *facts, char *buffer, size_t capacity);
size_t smith_prove_loop_what(const SmithProveLoop *loop, char *buffer, size_t capacity);
size_t smith_prove_loop_fix(const SmithProveLoop *loop, char *buffer, size_t capacity);
size_t smith_prove_boundary_fix(SmithProveText who, SmithProveText alternative, char *buffer, size_t capacity);
size_t smith_prove_write_reason(int kind, SmithProveText name, uint32_t line, char *buffer, size_t capacity);
size_t smith_prove_write_unknown(char *buffer, size_t capacity);
size_t smith_prove_cost_arithmetic(uint32_t bits, bool product, bool is_signed, bool remainder, char *buffer,
                                   size_t capacity);
size_t smith_prove_cost_copy(bool fill, bool known, uint64_t bytes, char *buffer, size_t capacity);
size_t smith_prove_cost_object(uint64_t bytes, char *buffer, size_t capacity);
size_t smith_prove_cost_loop(bool induction, SmithProveText not_cut, uint32_t passes, char *buffer, size_t capacity);
size_t smith_prove_ir_header(SmithProveText name, SmithProveText file, uint32_t line, char *buffer, size_t capacity);
size_t smith_prove_ir_point(uint32_t id, int kind, SmithProveText text, SmithProveText file, uint32_t line,
                            char *buffer, size_t capacity);
size_t smith_prove_ir_unprovable(SmithProveText what, SmithProveText file, uint32_t line, char *buffer,
                                 size_t capacity);
size_t smith_prove_render(int status, uint32_t bits, int shape, uint64_t value, char *buffer, size_t capacity);
}

namespace prove::report {

inline SmithProveText text(const std::string &value) {
    return {value.data(), value.size()};
}

// The text a call writes, however long: asked again with room for all of it.
template <typename Call> std::string said(Call call) {
    char small[256];
    size_t length = call(small, sizeof small);
    if (length <= sizeof small) return std::string(small, length);
    std::string whole(length, '\0');
    call(whole.data(), whole.size());
    return whole;
}

inline std::string as_written(const std::string &name) {
    return said([&](char *out, size_t size) { return smith_prove_as_written(text(name), out, size); });
}

} // namespace prove::report
