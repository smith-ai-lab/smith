// `smith prove`: what the three parts of the prover share. shim/prove.cpp is the driver (arguments, compiling, the
// report), shim/prove_ir.cpp rewrites LLVM IR until every function can be proved on its own (callees replaced by
// their contracts, loops unrolled, contracts turned into proof points), and shim/prove_alive.cpp gives those points to
// Alive2's encoding of IR semantics and asks Z3. The C++ is a thin layer over LLVM and Alive2; smith.c is C.
#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace prove {

// What a proof point asks.
enum class Kind {
    Requires,  // a callee's smith_requires, at the call that must make it true
    Ensures,   // this function's smith_ensures
    Invariant, // smith_invariant, at each pass of a loop (loops are unrolled)
    Assert,    // assert(): the call that reports a failure is never reached
    Trap,      // an undefined behaviour that UBSan checks, which traps in this build
    Abort,     // abort() or __builtin_trap() is never reached
    Premise,   // this function's smith_requires: assumed, and the inputs that break it are not the function's concern
};

// Where a proof point came from, to print it like a test failure.
struct Point {
    unsigned id = 0;
    Kind kind = Kind::Assert;
    std::string text; // the expression of a contract or an assert, or the kind of undefined behaviour
    std::string file;
    unsigned line = 0;
    bool assume = false;              // what follows holds only when the condition does
    int call_site = -1;               // the call of a callee whose contract this is, an index of Unit::call_sites
    std::vector<std::string> inlined; // "function at file:line" for each helper this point was inlined from
    const llvm::Instruction *where = nullptr; // the call that was replaced, for the values visible there
};

// A call of a function whose contract replaced it.
struct CallSite {
    std::string callee;
    std::string file;
    unsigned line = 0;
    std::vector<llvm::Value *> arguments; // in the function being proved
    std::vector<std::string> parameters;  // the callee's names for them
};

// A function being proved, with everything the report needs about it.
struct Unit {
    std::string name;
    std::string file;
    unsigned line = 0;
    std::vector<Point> points;
    std::vector<CallSite> call_sites;
    std::vector<std::string> notes;
    std::vector<std::pair<unsigned, unsigned>>
        distinct_pointers; // parameters whose pointees cannot be the same object // where LLVM's memory attributes were
                           // not enough to say what a callee leaves alone
};

// Why a function cannot be proved at all, which is an error and never an assumption: where, and what would make it
// provable.
struct Unprovable {
    std::string file;
    unsigned line = 0;
    std::string what;
    std::string fix;
};

// Settings of one run.
struct Options {
    // The budget is CPU time, not wall time, so that a busy machine does not change a verdict. A proof is charged what
    // it would take on `solver_threads` idle cores: the CPU time of what runs one thing after another, plus, for the
    // queries that run side by side, the longest of them or all of them divided by `solver_threads`, whichever is more.
    // Each of those is a CPU time, which does not depend on what else the machine runs. Bitwuzla runs the queries of a
    // proof on that many threads at most, on every machine.
    double budget_seconds = 10;
    static constexpr unsigned solver_threads = 8;
    // The most CPU time the whole process may use before the kernel stops it, the backstop for a solver that does not
    // look at its budget.
    double cpu_limit() const {
        return budget_seconds * solver_threads * 1.25 + 5;
    }
    bool verbose = false;
    std::vector<std::string> only; // function names; empty: every function with a contract
    bool dump = false;             // print the IR each function is proved from
};

// ---- shim/prove_ir.cpp ----

// All of the program: the files compiled and linked into one module, with every function numbered.
struct Program {
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::set<const llvm::Function *> recursive;
    std::set<const llvm::Function *> called; // functions some function of the program calls or takes the address of
};

// Reads the bitcode files, links them and prepares the module: every callee without an smith_ensures is inlined where
// it is called, memory effects are inferred, pointer parameters are marked dereferenceable. Returns false and fills
// `error` when the files cannot be read.
bool program_load(Program &program, const std::vector<std::string> &bitcode, std::string &error);

// The functions that are proved, in the order the files define them: the ones that carry a contract, and the ones no
// function of the program calls that call a function with an smith_ensures, since nothing else checks what they pass.
std::vector<std::string> program_units(const Program &program);

// The name of a function as the source writes it (the module may have renamed a static one).
std::string program_label(const Program &program, const std::string &name);

// A function ready to prove: a copy of the module in which the function's calls are resolved, its loops unrolled and
// its contracts are proof points. Fails with an Unprovable.
struct Prepared {
    std::unique_ptr<llvm::Module> module;
    llvm::Function *function = nullptr;
    Unit unit;
    std::unique_ptr<Unprovable> unprovable;
};
Prepared program_prepare(Program &program, const std::string &name);

// ---- shim/prove_bitwuzla.cpp ----

// Timeout: the deadline passed. Unknown: the solver could not read or decide the query.
enum class Verdict { Unsat, Sat, Unknown, Timeout };

// Asks Bitwuzla whether a query in SMT-LIB 2 is satisfiable. It stops when the calling thread has used `seconds` of CPU
// time, or when the process has used `threads * seconds` since it was at `process_start` (the queries that run side by
// side share a budget).
Verdict bitwuzla_check(const std::string &query, double seconds, double process_start, unsigned threads);

// CPU time (user and system) the calling thread has used.
double thread_cpu_seconds();

// ---- shim/prove_alive.cpp ----

struct Finding {
    const Point *point = nullptr; // nullptr: an undefined behaviour no proof point locates, described by the rest
    std::string detail;           // the instruction, for that
    std::string file;
    unsigned line = 0;
    std::vector<std::pair<std::string, std::string>> values; // name = value at the counterexample
};

struct Outcome {
    enum Status { Proved, Failed, Timeout, Error } status = Proved;
    std::vector<Finding> findings;
    std::string error;      // for Error
    double seconds = 0;     // wall time
    double cpu_seconds = 0; // CPU time of the process, every thread, since the proof began
    double charged = 0;     // what the budget counts: the time on solver_threads idle cores (see Options)
};

// CPU time (user and system, every thread) the process has used.
double process_cpu_seconds();

// Proves every point of one prepared function with Alive2 and Z3 within the budget.
Outcome alive_prove(Prepared &prepared, const Options &options);

} // namespace prove
