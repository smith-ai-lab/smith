// The plain data `smith prove`'s C++ (shim/prove_ir.cpp, shim/prove_alive.cpp) hands to the words of its report
// (runtime/prove_report.smith): characters that are not terminated, and the facts a sentence is made of. The C++
// side owns every LLVM type; what crosses is text, numbers and flags, so the sentences are written in smith's language
// and the C++ only asks. The functions are declared for C++ by shim/prove_report.h.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Characters with a count, not terminated.
typedef struct {
    const char *data;
    size_t length;
} SmithProveText;

// What an instruction is called in a message: its printed form (`%x = add i32 %a, %b, !dbg !12`), the place of its
// own line, the nearest line before it when it has none, and the helper it was inlined from.
typedef struct {
    SmithProveText printed;
    SmithProveText where;      // file:line of the instruction, or empty
    SmithProveText near_where; // file:line of the nearest earlier instruction, when `where` is empty
    bool inlined;              // it came from a helper that was inlined into the function
    SmithProveText inlined_in; // the helper, as the source names it
} SmithProveInstruction;

// What is known of a loop the prover could not bound (shim/prove.hpp UnrolledLoop).
typedef struct {
    bool known;                 // the loop is one the unroller recorded
    bool counted;               // LLVM can say how many times it runs
    uint32_t shrink_passes;     // with no count: the most passes a divide or shift to zero takes
    SmithProveText shrink_what; // "a 64-bit value divided by 10"
    SmithProveText not_cut;     // with an invariant: why induction could not prove it
    SmithProveText controls;    // the variables that decide the count, each ended by a newline
} SmithProveLoop;
