// `smith prove`: what the three parts of the prover share. shim/prove.cpp is the driver (arguments, compiling, the
// report), shim/prove_ir.cpp rewrites LLVM IR until every function can be proved on its own (callees replaced by
// their contracts, loops unrolled, contracts turned into proof points), and shim/prove_alive.cpp gives those points to
// Alive2's encoding of IR semantics and asks Z3. The C++ is a thin layer over LLVM and Alive2; smith.c is C.
#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"

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
    Write,     // a write of a function with a smith_modifies is inside the ranges it names
    Restrict,  // the objects a call reaches through a `restrict` argument and another argument do not overlap (#65)
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
    int induction_loop = -1;                  // a loop proved by induction: its index in Unit::loops
    bool induction_step = false;              // ... and this is its check at the back edge (else: on entry)
    bool contract_check = false;              // an ensures, invariant or assert call: a run-time check the lowering can omit
};

// A call of a function whose contract replaced it.
struct CallSite {
    std::string callee;
    std::string file;
    unsigned line = 0;
    // in the function being proved; a handle, because the clean-up passes after the call is replaced delete and replace
    // values (a null handle is a value that is gone)
    std::vector<llvm::WeakTrackingVH> arguments;
    std::vector<std::string> parameters; // the callee's names for them
};

// A loop the prover unrolled: where it is and how many passes it has (0: not a constant LLVM could read before
// unrolling, which is an error unless unrolling still finds one).
struct UnrolledLoop {
    std::string file;
    unsigned line = 0;
    unsigned passes = 0;
    bool counted = false;              // LLVM can say how many times the loop runs, as an expression of the names below
    unsigned shrink_passes = 0;        // with no count: the most passes a divide or shift of a value to zero takes
    std::string shrink_what;           // "a 64-bit value divided by 10"
    std::vector<std::string> controls; // the variables that decide the trip count (or, with no count, the exit)
    bool induction = false;            // not unrolled: its smith_invariant is proved by induction, one pass of its body
    std::vector<std::string> writes;   // with an invariant: what a pass changes (the variables and objects it writes)
    std::string not_cut;               // with an invariant but unrolled: why it could not be proved by induction
};

// Something in the code being proved that makes a query costly for the solver, with the line it comes from. Facts of
// the IR the solver is given, never guesses: a division by a variable, a multiplication of two variables, a loop that
// was unrolled, the loads and stores of a source line, a copy of a large object.
struct Cost {
    std::string what; // "64-bit unsigned remainder by a variable"
    std::string file;
    unsigned line = 0;
    unsigned count = 1;
    unsigned weight = 0;                                 // 3: arithmetic that blows up, 2: memory, 1: a loop
    std::vector<const llvm::Instruction *> instructions; // for "feeds this query"
};

// A function being proved, with everything the report needs about it.
struct Unit {
    std::string name;
    std::string file;
    unsigned line = 0;
    std::vector<Point> points;
    std::vector<CallSite> call_sites;
    std::vector<std::string> notes;
    std::vector<UnrolledLoop> loops;
    // The files the proof read from besides the unit's own, as `<directory>/<name>` of the debug info: those of every
    // function the unit reaches (its callees, whose contracts or bodies it assumed, and their callees) and of the
    // globals they use. The proof of a source is keyed on their bytes too (shim/proved.hpp).
    std::set<std::string> dependencies;
    std::vector<Cost> costs;                          // filled once the function is prepared
    unsigned instructions = 0, loads = 0, stores = 0; // of the prepared function
    std::vector<std::pair<unsigned, unsigned>>
        distinct_pointers; // parameters the source says are not the same object (`restrict`)
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
    // The most memory one function's proof may use (its process's physical footprint, compressed pages included), in
    // bytes. macOS enforces no RLIMIT_AS or RLIMIT_DATA, so the parent watches the process and stops it. A proof of a
    // function that fits its budget uses well under 1 GB; a query that blows up used 37 GB, and with several functions
    // proved at once a runaway takes the machine with it.
    double memory_bytes = 8.0 * 1024 * 1024 * 1024;
    static constexpr unsigned solver_threads = 8;
    // The most CPU time the whole process may use before the kernel stops it, the backstop for a solver that does not
    // look at its budget.
    double cpu_limit() const {
        return budget_seconds * solver_threads * 1.25 + 5;
    }
    bool verbose = false;
    std::vector<std::string> only; // function names; empty: every function with a contract
    bool in_project = false;     // one group of a project's sources (--in-project): a name defined in no other is fine
    bool dump = false;           // print the IR each function is proved from
    std::string emit_ir;         // a file that gets the IR of each function, as it is checked (--emit-ir)
    bool emit_ir_append = false; // the file is not emptied first (the project verb proves several groups)
    bool no_cache = false;       // prove again what a cached proof has proved
    // A loop with a smith_invariant that has a bound can be unrolled (the invariant is proved on each pass) or proved
    // by induction (once, for any pass). Auto: by induction when it has more than `induction_from_passes` passes or no
    // count. --induction and --unroll say it for every loop.
    enum class Loops { Auto, Induction, Unroll };
    Loops loops = Loops::Auto;
    unsigned induction_from_passes = 16;
};

// ---- shim/prove_ir.cpp ----

// All of the program: the files compiled and linked into one module, with every function numbered.
struct Without {
    std::string name; // as the source writes it
    std::string file;
    unsigned line = 0;
};

// How much of the program has a contract of its own (issue #51), counted as the module is read: the project's own
// defined functions (not smith's libraries, not a generic instance of contract::), and those with a contract of their
// own or smith_trusted(), which are the proved and the trusted ones.
struct Coverage {
    unsigned total = 0, with_contract = 0;
    std::vector<Without> without; // in the order the files define them
};

struct Program {
    Coverage coverage;
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::set<const llvm::Function *> recursive;
    bool verbose = false;                    // program_load prints each phase's time and the module's size
    std::set<const llvm::Function *> called; // functions some function of the program calls or takes the address of
};

// Reads the bitcode files, links them and prepares the module: every callee without an smith_ensures is inlined where
// it is called, memory effects are inferred, pointer parameters are marked dereferenceable. Returns false and fills
// `error` when the files cannot be read. The last `libraries` files are smith's own libraries, read for their contracts
// and bodies and never proved.
bool program_load(Program &program, const std::vector<std::string> &bitcode, std::string &error, size_t libraries = 0);

// The functions that are proved, in the order the files define them: the ones that carry a contract (smith_requires,
// smith_ensures or smith_invariant) and are not trusted. A function with no contract is not proved, nor is main; calls
// in the ones that are proved still use the contracts of their callees.
std::vector<std::string> program_units(const Program &program);

// A function marked smith_trusted(): its contract is assumed by callers and its body is not read.
struct Trusted {
    std::string name; // as the source writes it
    std::string file;
    unsigned line = 0;
    unsigned requires_count = 0, ensures_count = 0;
};
std::vector<Trusted> program_trusted(const Program &program);

// Whether a function (by its name in the module) is trusted.
bool program_is_trusted(const Program &program, const std::string &name);

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
Prepared program_prepare(Program &program, const std::string &name, const Options &options = {});

// What a proof of this prepared function costs the solver, by the line (Unit::costs, Unit::loops are filled by
// program_prepare); the instructions whose value `root` depends on, through operands only.
std::set<const llvm::Instruction *> data_cone(const llvm::Instruction *root);

// The IR of a prepared function as the prover checks it, with the proof points listed in comments.
std::string program_ir_text(const Prepared &prepared);

// A key for a proof of the prepared function: the text of its IR without debug information or the numbering of
// attribute groups, the globals and declarations it uses, and the proof points. Two functions with the same key have
// the same proof.
std::string program_proof_key(const Prepared &prepared);

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

// One query of a proof and what it took.
struct QueryTime {
    std::string what; // "smith_ensures(q * y + r == x)", or the instruction that may have undefined behaviour
    std::string file;
    unsigned line = 0;
    double cpu = 0;
    bool ran_out = false;      // the budget ended while it ran
    std::vector<size_t> feeds; // indexes of Unit::costs whose operations its value depends on
};

struct Outcome {
    enum Status { Proved, Failed, Timeout, Error } status = Proved;
    std::vector<QueryTime> queries; // the slowest first, at most a few
    unsigned query_count = 0;
    double execution_seconds = 0; // CPU time before the first query: Alive2's symbolic execution of the function
    unsigned queries_written = 0;
    std::vector<std::pair<std::string, double>> phases; // where the charged time went, in order
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
