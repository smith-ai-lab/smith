// `smith prove`, the proof half: hands a prepared function to Alive2's encoding of LLVM IR semantics (poison and undef
// included) and asks Z3 whether a proof point can fail.
//
// Alive2 proves that one function refines another, so the function under proof is the source and a stub with the same
// signature is the target; TransformVerify::exec runs the symbolic execution of both and hands back the states, where
// each smith_point call has left its condition and the domain (the path and every undefined behaviour so far) at that
// point. A point fails when some input reaches it, without undefined behaviour on the way, with its condition false.
// What the state calls undefined behaviour besides the points (an invalid access, say) is one more query: some input
// reaches neither a return nor a failing point.
#include "prove.hpp"

#include "ir/function.h"
#include "ir/instr.h"
#include "ir/pointer.h"
#include "ir/state.h"
#include "llvm_util/llvm2alive.h"
#include "llvm_util/utils.h"
#include "smt/ctx.h"
#include "smt/smt.h"
#include "smt/solver.h"
#include "tools/transform.h"
#include "util/config.h"
#include "util/errors.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/TargetParser/Triple.h"

#include <z3.h>

#include <dirent.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <sys/resource.h>
#include <thread>
#include <time.h>
#include <vector>

namespace prove {
namespace {

using Clock = std::chrono::steady_clock;

} // namespace

double process_cpu_seconds() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}

double thread_cpu_seconds() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) / 1e9;
}

namespace {

// Runs `work`, which asks Z3 on this thread while nothing else computes, and interrupts Z3 once the process has used
// `seconds` more CPU time (Z3's own time limit is wall time). Returns whether it had to.
template <class Work> bool with_z3_cpu_limit(double seconds, Work work) {
    std::atomic<bool> done{false};
    std::atomic<bool> interrupted{false};
    const double begin = process_cpu_seconds();
    std::thread watcher([&] {
        while (!done) {
            if (process_cpu_seconds() - begin >= seconds) {
                interrupted = true;
                Z3_interrupt(smt::ctx());
                return;
            }
            usleep(5000);
        }
    });
    work();
    done = true;
    watcher.join();
    return interrupted;
}

// Alive2's handler of Z3's errors prints "Severe Z3 error: out of memory" from every solver thread that meets it and
// exits. Z3 runs out of memory on a function whose queries are over the budget anyway (a sixteen-way concurrent set of
// big queries), so that is not another failure: the process ends as the CPU budget ends it (SIGXCPU, which the parent
// says as "over its budget"), and says nothing itself. Any other Z3 error is Alive2's own report.
void z3_error(Z3_context context, Z3_error_code code) {
    std::string_view message = Z3_get_error_msg(context, code);
    if (message == "canceled") return; // the timeout, harmless
    if (message == "out of memory") {
        kill(getpid(), SIGXCPU);
        for (;;) // the signal ends the process; this thread waits for it
            usleep(1000);
    }
    util::config::dbg() << "Severe Z3 error: " << message << " [code=" << code << ']' << std::endl;
    _Exit(-1);
}

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// A proof point in Alive's IR: it evaluates its condition where it stands and does nothing else.
class SmithPoint final : public IR::Instr {
  public:
    SmithPoint(unsigned id, IR::Value *condition, IR::Value *first = nullptr, IR::Value *second = nullptr,
               uint64_t first_bytes = 0, uint64_t second_bytes = 0, IR::Value *first_size = nullptr,
               IR::Value *second_size = nullptr)
        : Instr(IR::Type::voidTy, "#point" + std::to_string(id)), condition(condition), id(id), first(first),
          second(second), first_bytes(first_bytes), second_bytes(second_bytes), first_size(first_size),
          second_size(second_size) {}

    IR::Value *condition;
    unsigned id;
    // A restrict call (issue #65): the objects of two pointers, of this many bytes each, are apart. Said about the
    // blocks and offsets Alive2 holds, not about addresses, which are far dearer for the solver.
    IR::Value *first, *second;
    uint64_t first_bytes, second_bytes;
    // The bytes of a slice's range are its count times its element size, known only at the call: a value, not a number.
    IR::Value *first_size, *second_size;
    mutable IR::StateValue seen;

    bool is_restrict() const {
        return first != nullptr;
    }
    std::vector<IR::Value *> operands() const override {
        if (is_restrict()) {
            std::vector<IR::Value *> all{first, second};
            if (first_size != nullptr) all.push_back(first_size);
            if (second_size != nullptr) all.push_back(second_size);
            return all;
        }
        return condition != nullptr ? std::vector<IR::Value *>{condition} : std::vector<IR::Value *>{};
    }
    bool propagatesPoison() const override {
        return false;
    }
    bool hasSideEffects() const override {
        return true;
    }
    void rauw(const IR::Value &what, IR::Value &with) override {
        if (condition == &what) condition = &with;
        if (first == &what) first = &with;
        if (second == &what) second = &with;
        if (first_size == &what) first_size = &with;
        if (second_size == &what) second_size = &with;
    }
    void print(std::ostream &os) const override {
        os << "point " << id;
    }
    IR::StateValue toSMT(IR::State &state) const override {
        if (condition != nullptr) seen = state[*condition];
        if (is_restrict()) {
            const IR::StateValue &a = state[*first];
            const IR::StateValue &b = state[*second];
            IR::Pointer pa(state.getMemory(), a.value);
            IR::Pointer pb(state.getMemory(), b.value);
            // Offsets are signed and sizes unsigned: the sums are made one bit wider than both, so no range wraps.
            smt::expr offset_a = pa.getOffset(), offset_b = pb.getOffset();
            unsigned bits = offset_a.bits() + 2;
            auto size_of = [&](IR::Value *value, uint64_t constant) -> smt::expr {
                if (value == nullptr) return smt::expr::mkUInt(constant, bits);
                smt::expr size = state[*value].value;
                return size.bits() >= bits ? size.trunc(bits) : size.zext(bits - size.bits());
            };
            smt::expr size_a = size_of(first_size, first_bytes), size_b = size_of(second_size, second_bytes);
            smt::expr start_a = offset_a.sext(2), start_b = offset_b.sext(2);
            smt::expr apart = pa.getBid() != pb.getBid() || size_a == 0 || size_b == 0 ||
                              (start_a + size_a).sle(start_b) || (start_b + size_b).sle(start_a);
            seen = {smt::expr::mkIf(apart, smt::expr::mkUInt(1, 1), smt::expr::mkUInt(0, 1)),
                    a.non_poison && b.non_poison};
        }
        return {};
    }
    smt::expr getTypeConstraints(const IR::Function &) const override {
        return true;
    }
    std::unique_ptr<IR::Instr> dup(IR::Function &, const std::string &) const override {
        return std::make_unique<SmithPoint>(id, condition, first, second, first_bytes, second_bytes, first_size,
                                            second_size);
    }
};

// A stub with the signature of `function`, which Alive2 compares it with.
llvm::Function *make_stub(llvm::Function &function) {
    llvm::Module &module = *function.getParent();
    llvm::Function *stub =
        llvm::Function::Create(function.getFunctionType(), llvm::GlobalValue::ExternalLinkage, "smith_stub", module);
    for (unsigned i = 0; i < function.arg_size(); i++) {
        stub->getArg(i)->setName(function.getArg(i)->getName());
        for (llvm::Attribute attribute : function.getAttributes().getParamAttrs(i))
            stub->addParamAttr(i, attribute);
    }
    auto *block = llvm::BasicBlock::Create(module.getContext(), "entry", stub);
    if (stub->getReturnType()->isVoidTy()) {
        llvm::ReturnInst::Create(module.getContext(), block);
    } else {
        llvm::ReturnInst::Create(module.getContext(), llvm::PoisonValue::get(stub->getReturnType()), block);
    }
    return stub;
}

// A source variable and the LLVM value that holds it where it is recorded.
struct Variable {
    const llvm::DILocalVariable *variable;
    llvm::Value *value;
    const llvm::Instruction *recorded_at;
    std::string name; // Alive's name for the value: Alive2 forgets the LLVM values once a function is translated
};

std::vector<Variable> source_variables(llvm::Function &function) {
    std::vector<Variable> found;
    for (llvm::Instruction &instruction : llvm::instructions(function)) {
        for (llvm::DbgRecord &record : instruction.getDbgRecordRange()) {
            auto *value = llvm::dyn_cast<llvm::DbgVariableRecord>(&record);
            if (value == nullptr || !value->isDbgValue() || value->hasArgList()) continue;
            llvm::Value *location = value->getVariableLocationOp(0);
            if (location == nullptr || !location->getType()->isIntegerTy()) continue;
            if (value->getVariable()->getScope()->getSubprogram() != function.getSubprogram()) continue;
            if (!location->hasName() && !llvm::isa<llvm::Constant>(location))
                location->setName(value->getVariable()->getName());
            found.push_back({value->getVariable(), location, &instruction, ""});
        }
    }
    return found;
}

enum class Shape { Signed, Unsigned, Boolean, Character };

Shape shape_of(const llvm::DIType *type) {
    while (type != nullptr) {
        if (auto *derived = llvm::dyn_cast<llvm::DIDerivedType>(type)) {
            type = derived->getBaseType();
            continue;
        }
        break;
    }
    if (auto *basic = llvm::dyn_cast_or_null<llvm::DIBasicType>(type)) {
        switch (basic->getEncoding()) {
        case llvm::dwarf::DW_ATE_boolean:
            return Shape::Boolean;
        case llvm::dwarf::DW_ATE_unsigned:
        case llvm::dwarf::DW_ATE_unsigned_char:
            return Shape::Unsigned;
        default: // signed, signed_char and the rest
            return Shape::Signed;
        }
    }
    return Shape::Signed;
}

std::string render(const smt::Model *model_or_none, const IR::StateValue &value, unsigned bits, Shape shape) {
    if (model_or_none == nullptr) return "?";
    const smt::Model &model = *model_or_none;
    smt::expr poison = model.eval(value.non_poison, true);
    if (poison.isFalse()) return "poison";
    smt::expr number = model.eval(value.value, true);
    uint64_t unsigned_value = 0;
    if (bits > 64 || !number.isUInt(unsigned_value)) return "?";
    if (shape == Shape::Boolean) return unsigned_value != 0 ? "true" : "false";
    if (shape == Shape::Signed && bits < 64 && (unsigned_value >> (bits - 1)) != 0) {
        return std::to_string(static_cast<int64_t>(unsigned_value | (~0ULL << bits)));
    }
    if (shape == Shape::Signed && bits == 64) return std::to_string(static_cast<int64_t>(unsigned_value));
    return std::to_string(unsigned_value);
}

} // namespace

namespace {

// A query as SMT-LIB 2 text. Alive2 writes the query of a check to a file when it is asked for benchmarks and told to
// skip the solving, and that is the one way to print it that its classes allow.
std::string smtlib_of(const smt::expr &query, const char *name) {
    static std::string directory;
    if (directory.empty()) {
        const char *base = std::getenv("TMPDIR");
        std::string pattern =
            std::string(base != nullptr && *base != '\0' ? base : "/tmp") + "/smith-prove-query-XXXXXX";
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        directory = mkdtemp(buffer.data()) != nullptr ? buffer.data() : "/tmp";
        std::atexit([] { rmdir(directory.c_str()); });
    }
    util::config::smt_benchmark_dir = directory;
    util::config::skip_smt = true;
    smt::Solver writer(true);
    writer.add(query);
    writer.check(name);
    util::config::skip_smt = false;
    util::config::smt_benchmark_dir = std::getenv("SMITH_PROVE_SMT") != nullptr ? std::getenv("SMITH_PROVE_SMT") : "";
    std::string text;
    if (DIR *folder = opendir(directory.c_str())) {
        while (dirent *entry = readdir(folder)) {
            if (entry->d_name[0] == '.') continue;
            std::string path = directory + "/" + entry->d_name;
            std::ifstream file(path);
            std::ostringstream contents;
            contents << file.rdbuf();
            text = contents.str();
            unlink(path.c_str());
        }
        closedir(folder);
    }
    if (const char *keep = std::getenv("SMITH_PROVE_KEEP")) {
        static int counter = 0;
        std::ofstream(std::string(keep) + "/q" + std::to_string(counter++) + "_" + name + ".smt2") << text;
    }
    return text;
}

// What a query came to: Bitwuzla's verdict, and Z3's model when there is one.
struct Answer {
    Verdict verdict = Verdict::Unknown;
    smt::Result z3;
    bool has_model = false;
    smt::expr whole;

    bool isSat() const {
        return verdict == Verdict::Sat;
    }
    bool isUnsat() const {
        return verdict == Verdict::Unsat;
    }
    const smt::Model *model() const {
        return has_model ? &z3.getModel() : nullptr;
    }
    std::string reason() const {
        return "it answered unknown";
    }
};

} // namespace

Outcome alive_prove(Prepared &prepared, const Options &options) {
    Outcome outcome;
    Clock::time_point start = Clock::now();
    const double cpu_start = process_cpu_seconds();
    // What the budget counts (see Options): this thread's CPU time except where it ran queries with the others, plus
    // the time the queries take on solver_threads cores, round by round.
    const double thread_start = thread_cpu_seconds();
    double in_rounds = 0;
    double round_charges = 0;
    auto charged = [&] { return thread_cpu_seconds() - thread_start - in_rounds + round_charges; };
    llvm::Function &function = *prepared.function;
    llvm::Module &module = *prepared.module;
    Unit &unit = prepared.unit;
    std::ostringstream log;
    static smt::smt_initializer solver_context;
    try {
        // C has no poison to pass, and what a pointer argument points to holds values.
        util::config::disable_poison_input = false;
        util::config::disable_undef_input = true;
        if (const char *directory = std::getenv("SMITH_PROVE_SMT")) util::config::smt_benchmark_dir = directory;
        llvm_util::init_llvm_utils(log, module.getDataLayout());
        llvm_util::reset_state();
        llvm::TargetLibraryInfoWrapperPass library(llvm::Triple(module.getTargetTriple()));
        llvm::Function *stub = make_stub(function);

        // A value that holds a source variable gets its name, so that the counterexample can name it.
        std::vector<Variable> variables = source_variables(function);
        for (Variable &variable : variables)
            variable.name = llvm_util::value_name(*variable.value);
        for (CallSite &site : unit.call_sites) {
            for (llvm::Value *argument : site.arguments) {
                if (argument != nullptr && !argument->hasName() && !llvm::isa<llvm::Constant>(argument))
                    argument->setName("argument");
            }
        }
        std::set<std::string> given_memory;         // loads of what a parameter or a global holds, which is values
        std::set<const llvm::AllocaInst *> havoced; // the bookkeeping of the preparing is not for Alive2
        for (llvm::Instruction &instruction : llvm::instructions(function)) {
            if (instruction.getMetadata("smith.havoced") == nullptr) continue;
            havoced.insert(llvm::cast<llvm::AllocaInst>(&instruction));
            instruction.setMetadata("smith.havoced", nullptr);
        }
        std::map<std::string, std::pair<std::string, unsigned>> locations; // where each named instruction is written
        std::map<std::string, const llvm::Instruction *> by_llvm_name;     // the instruction a name stands for
        for (llvm::Instruction &instruction : llvm::instructions(function)) {
            if (instruction.getType()->isVoidTy()) continue;
            if (!instruction.hasName()) instruction.setName("t");
            if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
                const llvm::Value *object = llvm::getUnderlyingObject(load->getPointerOperand());
                auto *local = llvm::dyn_cast<llvm::AllocaInst>(object);  // one a call was allowed to change
                auto *returned = llvm::dyn_cast<llvm::CallBase>(object); // a pointer a callee was proved to return
                if (llvm::isa<llvm::Argument>(object) || llvm::isa<llvm::GlobalVariable>(object) ||
                    (returned != nullptr && returned->getMetadata("smith.valid_return") != nullptr) ||
                    (local != nullptr && havoced.count(local) != 0))
                    given_memory.insert(llvm_util::value_name(instruction));
            }
            by_llvm_name[llvm_util::value_name(instruction)] = &instruction;
            if (instruction.getDebugLoc()) {
                locations[llvm_util::value_name(instruction)] = {instruction.getDebugLoc()->getFilename().str(),
                                                                 instruction.getDebugLoc().getLine()};
            }
        }
        for (llvm::Instruction &instruction : llvm::instructions(function))
            instruction.setMetadata("smith.valid_return", nullptr); // Alive2 does not read it
        auto source = llvm_util::llvm2alive(function, library.getTLI(function), true);
        if (!source) {
            outcome.status = Outcome::Error;
            outcome.error = "cannot translate " + unit.name + " to Alive2's IR: " + log.str();
            return outcome;
        }
        std::vector<std::vector<std::string>> call_site_names(unit.call_sites.size());
        for (size_t i = 0; i < unit.call_sites.size(); i++) {
            for (llvm::Value *argument : unit.call_sites[i].arguments) {
                call_site_names[i].push_back(argument == nullptr || llvm::isa<llvm::ConstantInt>(argument)
                                                 ? ""
                                                 : llvm_util::value_name(*argument));
            }
        }
        auto target = llvm_util::llvm2alive(*stub, library.getTLI(*stub), false, source->getGlobalVars());
        if (!target) {
            outcome.status = Outcome::Error;
            outcome.error = "cannot translate the stub of " + unit.name + ": " + log.str();
            return outcome;
        }

        std::vector<SmithPoint *> points;
        for (IR::BasicBlock *block : source->getBBs()) {
            for (size_t i = 0; i < block->size(); i++) {
                auto *call = dynamic_cast<IR::FnCall *>(&block->at(i));
                if (call != nullptr && call->getFnName() == "@smith_restrict") {
                    auto id = IR::getInt(*call->getArgs()[0].first);
                    IR::Value *first_size = call->getArgs()[3].first, *second_size = call->getArgs()[4].first;
                    auto first_bytes = IR::getInt(*first_size);
                    auto second_bytes = IR::getInt(*second_size);
                    if (!id) continue;
                    auto *point = new SmithPoint(static_cast<unsigned>(*id), nullptr, call->getArgs()[1].first,
                                                 call->getArgs()[2].first, first_bytes ? *first_bytes : 0,
                                                 second_bytes ? *second_bytes : 0, first_bytes ? nullptr : first_size,
                                                 second_bytes ? nullptr : second_size);
                    points.push_back(point);
                    block->getInstr(i) = std::unique_ptr<IR::Instr>(point);
                    continue;
                }
                if (call == nullptr || call->getFnName() != "@smith_point") continue;
                auto id = IR::getInt(*call->getArgs()[0].first);
                IR::Value *argument = call->getArgs()[1].first;
                IR::Value *condition = argument;
                if (auto *constant = dynamic_cast<IR::IntConst *>(condition);
                    constant != nullptr && constant->getInt() != nullptr && *constant->getInt() == 0)
                    condition = nullptr; // a point with no condition fails when it is reached
                if (!id) continue;
                auto *point = new SmithPoint(static_cast<unsigned>(*id), condition);
                points.push_back(point);
                block->getInstr(i) = std::unique_ptr<IR::Instr>(point);
                if (unit.points[point->id].assume) { // what follows holds only when the condition does
                    block->addInstrAt(std::make_unique<IR::Assume>(*argument, IR::Assume::AndNonPoison), point, false);
                    i++;
                }
            }
        }

        if (options.dump) std::cerr << "before preprocessing:\n" << *source << "\n";
        tools::Transform transform;
        transform.src = std::move(*source);
        transform.tgt = std::move(*target);
        // Z3 (the fallback for a query Bitwuzla cannot read, and the values of a counterexample) has only a wall clock,
        // and the budget is CPU time (with_z3_cpu_limit interrupts it there), so its own limit is only a backstop, as
        // long as the parent's: on a busy machine a wall limit of the budget's size ends a proof the budget has not.
        smt::set_query_timeout(std::to_string(static_cast<long>((20 * options.budget_seconds + 30) * 1000)));
        solver_context.reset(); // takes the timeout
        Z3_set_error_handler(smt::ctx(), z3_error);
        transform.preprocess();
        tools::TransformVerify verifier(transform, false);
        auto [state, target_state] = verifier.exec();
        outcome.execution_seconds = thread_cpu_seconds() - thread_start;
        double phase_mark = 0; // what charged() was when the last phase ended
        auto phase = [&](const std::string &what) {
            double now = charged();
            outcome.phases.emplace_back(what, now - phase_mark);
            phase_mark = now;
        };
        phase("translating the function to Alive2's IR and executing it symbolically");
        if (options.dump) std::cerr << transform.src << "\n";
        if (options.dump) std::cerr << "symbolic execution took " << seconds_since(start) << " s\n";
        if (charged() > options.budget_seconds) {
            outcome.status = Outcome::Timeout;
            outcome.seconds = seconds_since(start);
            outcome.cpu_seconds = process_cpu_seconds() - cpu_start;
            outcome.charged = charged();
            return outcome; // the symbolic execution alone used the budget: no query was asked
        }

        // The values to show at a counterexample, by their names in the function that was proved.
        std::map<std::string, IR::Value *> by_name;
        for (const IR::Value &input : transform.src.getInputs()) { // "noundef %x": the attributes come first
            std::string name = input.getName();
            by_name[name.substr(name.rfind(' ') + 1)] = const_cast<IR::Value *>(&input);
        }
        for (const IR::Instr &instruction : transform.src.instrs())
            by_name[instruction.getName()] = const_cast<IR::Instr *>(&instruction);
        std::vector<std::pair<Variable, IR::Value *>> shown;
        for (const Variable &variable : variables) {
            auto found = by_name.find(variable.name);
            if (found != by_name.end()) shown.emplace_back(variable, found->second);
        }
        std::vector<std::vector<IR::Value *>> call_site_values(unit.call_sites.size());
        for (size_t i = 0; i < call_site_names.size(); i++) {
            for (const std::string &name : call_site_names[i]) {
                auto found = by_name.find(name);
                call_site_values[i].push_back(found == by_name.end() || name.empty() ? nullptr : found->second);
            }
        }
        smt::expr axioms = state->getAxioms()() && target_state->getAxioms()();
        smt::expr assumptions = state->getPre()() && target_state->getPre()() && state->getFnPre();
        // What the function is called with: arguments with the attributes it declares (a value, not undef or poison; a
        // pointer to an object).
        for (const IR::Value &input : transform.src.getInputs()) {
            if (const IR::State::ValTy *value = state->at(input)) assumptions &= value->domain();
        }
        // Pointers the source declares apart (`restrict`) are different objects; any other two may be the same.
        {
            std::vector<const IR::Value *> inputs;
            for (const IR::Value &input : transform.src.getInputs())
                inputs.push_back(&input);
            for (auto [first, second] : unit.distinct_pointers) {
                const IR::State::ValTy *a = state->at(*inputs[first]);
                const IR::State::ValTy *b = state->at(*inputs[second]);
                if (a == nullptr || b == nullptr) continue;
                IR::Pointer pa(state->getMemory(), a->val.value);
                IR::Pointer pb(state->getMemory(), b->val.value);
                assumptions &= pa.getBid() != pb.getBid();
            }
        }
        smt::expr normal = state->returnVal().domain();

        // The values to show for a point that fails, from the model.
        llvm::DominatorTree dominators(function);
        auto describe = [&](const Point &point, const smt::Model *model) {
            Finding finding;
            finding.point = &point;
            std::set<std::string> seen_names;
            std::vector<std::pair<std::string, std::string>> values;
            // the variable's latest record that dominates the point
            std::vector<std::pair<Variable, IR::Value *>> candidates;
            for (auto &entry : shown) {
                const llvm::Instruction *at = entry.first.recorded_at;
                if (point.where != nullptr && at != point.where && !dominators.dominates(at, point.where)) continue;
                candidates.push_back(entry);
            }
            std::map<std::string, std::pair<Variable, IR::Value *>> latest;
            for (auto &entry : candidates) {
                std::string name = entry.first.variable->getName().str();
                auto found = latest.find(name);
                if (found == latest.end() || found->second.first.recorded_at == entry.first.recorded_at ||
                    dominators.dominates(found->second.first.recorded_at, entry.first.recorded_at))
                    latest[name] = entry;
            }
            for (auto &[name, entry] : latest) {
                const IR::State::ValTy *value = state->at(*entry.second);
                if (value == nullptr) continue;
                values.emplace_back(name, render(model, value->val, entry.first.value->getType()->getIntegerBitWidth(),
                                                 shape_of(entry.first.variable->getType())));
            }
            if (point.call_site >= 0) {
                const CallSite &site = unit.call_sites[static_cast<size_t>(point.call_site)];
                for (size_t j = 0; j < site.arguments.size(); j++) {
                    std::string name = site.callee + "." + site.parameters[j];
                    if (auto *constant =
                            llvm::dyn_cast_or_null<llvm::ConstantInt>(site.arguments[j].operator llvm::Value *())) {
                        values.emplace_back(name, std::to_string(constant->getSExtValue()));
                        continue;
                    }
                    IR::Value *translated = call_site_values[static_cast<size_t>(point.call_site)][j];
                    const IR::State::ValTy *value = translated == nullptr ? nullptr : state->at(*translated);
                    if (value != nullptr)
                        values.emplace_back(name, render(model, value->val, value->val.bits(), Shape::Signed));
                }
            }
            finding.values = std::move(values);
            return finding;
        };

        // A callee Alive2 cannot see may, as far as it knows, have undefined behaviour of its own; one that has a
        // contract is not allowed to. And only the runs in which a call returns matter.
        for (const smt::expr &variable : normal.vars()) {
            std::ostringstream name;
            name << variable;
            if (name.str().find("#ub!") != std::string::npos) assumptions &= variable;
            if (name.str().find("#noreturn!") != std::string::npos) assumptions &= !variable;
        }
        // A failure needs a run that reaches the point with no undefined behaviour before it, and that run has to be
        // described in full. Most points hold anyway, and are shown to with only the path to them and what is assumed
        // on the way: dropping the rest of that description only adds runs, so a proof of the smaller query is a proof
        // of the full one. Only a failure of the small query is asked again in full. The queries do not depend on each
        // other, so they are collected while the function is walked and then decided together, Bitwuzla on every core;
        // Z3 is asked only for the values of a counterexample, which it shows as "?" if it does not find them quickly.
        struct Job {
            smt::expr whole;
            std::string text;
            Verdict verdict = Verdict::Unknown;
            double cpu = 0; // the CPU time its query took
        };
        auto prepare = [&](Job &job, const smt::expr &formula, const char *name) {
            job.whole = axioms && assumptions && formula;
            job.text.clear();
            if (!job.whole.isValid())
                job.verdict = Verdict::Unknown;
            else if (job.whole.isFalse())
                job.verdict = Verdict::Unsat;
            else if (job.whole.isTrue())
                job.verdict = Verdict::Sat;
            else {
                job.verdict = Verdict::Unknown;
                job.text = smtlib_of(job.whole, name);
            }
        };
        // Decides the jobs on several threads; false when the budget ran out first. The round is charged the longest
        // query or all of them divided by the threads, whichever is more: what it takes on that many idle cores.
        auto solve = [&](const std::vector<Job *> &jobs, const char *name) {
            double left = options.budget_seconds - charged();
            if (left <= 0) return false;
            std::atomic<size_t> next{0};
            const double process_start = process_cpu_seconds();
            auto work = [&] {
                for (size_t i = next++; i < jobs.size(); i = next++) {
                    if (jobs[i]->text.empty()) continue;
                    double before = thread_cpu_seconds();
                    jobs[i]->verdict = bitwuzla_check(jobs[i]->text, left, process_start, Options::solver_threads);
                    jobs[i]->cpu = thread_cpu_seconds() - before;
                }
            };
            std::vector<std::thread> threads;
            size_t count = std::min<size_t>(
                std::min(Options::solver_threads, std::max(1u, std::thread::hardware_concurrency())), jobs.size());
            for (size_t i = 1; i < count; i++)
                threads.emplace_back(work);
            double main_before = thread_cpu_seconds();
            work();
            in_rounds += thread_cpu_seconds() - main_before;
            for (std::thread &thread : threads)
                thread.join();
            double longest = 0;
            double total = 0;
            for (Job *job : jobs) {
                longest = std::max(longest, job->cpu);
                total += job->cpu;
            }
            round_charges += std::max(longest, total / Options::solver_threads);
            for (Job *job : jobs) {
                if (job->verdict == Verdict::Timeout) return false;
                if (job->verdict != Verdict::Unknown || job->text.empty()) continue;
                smt::Solver fallback(true); // a query Bitwuzla cannot read: Z3 decides it, if it can
                fallback.add(job->whole);
                smt::Result answer;
                const double z3_before = thread_cpu_seconds();
                bool out_of_time = with_z3_cpu_limit(std::max(0.0, options.budget_seconds - charged()),
                                                     [&] { answer = fallback.check(name); });
                job->cpu += thread_cpu_seconds() - z3_before;
                if (out_of_time) {
                    job->verdict = Verdict::Timeout;
                    return false;
                }
                job->verdict = answer.isSat() ? Verdict::Sat : answer.isUnsat() ? Verdict::Unsat : Verdict::Unknown;
            }
            return true;
        };
        struct Task {
            smt::expr small;
            std::function<smt::expr()> full;
            const char *name;
            const Point *point;           // a proof point, else
            const IR::Instr *instruction; // an instruction whose own conditions may fail
            Job job;
        };
        // The query as the report names it: the contract or check it asks about, or the instruction that may have
        // undefined behaviour, with its line.
        auto name_query = [&](const Task &task) {
            QueryTime time;
            if (task.point != nullptr) {
                const Point &point = *task.point;
                const char *kinds[] = {"smith_requires", "smith_ensures", "smith_invariant", "assert", "", "", ""};
                if (point.kind == Kind::Requires && point.call_site >= 0) {
                    const CallSite &site = unit.call_sites[static_cast<size_t>(point.call_site)];
                    time.what = "smith_requires(" + point.text + ") of " + site.callee + ", at its call";
                    time.file = site.file;
                    time.line = site.line;
                    return time;
                }
                time.what = point.kind <= Kind::Assert
                                ? std::string(kinds[static_cast<int>(point.kind)]) + "(" + point.text + ")"
                                : point.text;
                time.file = point.file;
                time.line = point.line;
                return time;
            }
            std::ostringstream text;
            task.instruction->print(text);
            std::string printed = text.str();
            if (printed.size() > 70) printed = printed.substr(0, 67) + "...";
            time.what = "undefined behaviour of `" + printed + "`";
            auto own = locations.find(task.instruction->getName());
            if (own != locations.end()) {
                time.file = own->second.first;
                time.line = own->second.second;
            }
            for (const IR::Value *operand : task.instruction->operands()) { // a phi has no line: its operands do
                auto found = locations.find(operand->getName());
                if (time.line == 0 && found != locations.end()) {
                    time.file = found->second.first;
                    time.line = found->second.second;
                }
            }
            return time;
        };
        std::vector<Task> tasks;
        bool timed_out = false;
        auto describe_unlocated = [&](const IR::Instr &instruction, const smt::Model *model) {
            Finding finding;
            std::ostringstream text;
            instruction.print(text);
            finding.detail = text.str();
            for (const IR::Value *operand : instruction.operands()) {
                auto found = locations.find(operand->getName());
                if (finding.line == 0 && found != locations.end()) {
                    finding.file = found->second.first;
                    finding.line = found->second.second;
                }
            }
            auto own = locations.find(instruction.getName());
            if (own != locations.end()) {
                finding.file = own->second.first;
                finding.line = own->second.second;
            }
            for (auto &entry : shown) {
                const IR::State::ValTy *shown_value = state->at(*entry.second);
                if (shown_value == nullptr || !entry.first.variable->isParameter()) continue;
                finding.values.emplace_back(entry.first.variable->getName().str(),
                                            render(model, shown_value->val,
                                                   entry.first.value->getType()->getIntegerBitWidth(),
                                                   shape_of(entry.first.variable->getType())));
            }
            return finding;
        };

        // Every block, in the order they run: the points, and, for the rest, what each instruction adds to the
        // conditions that must hold (an access that may be invalid, a poison value that is used).
        std::map<const IR::BasicBlock *, std::vector<const IR::BasicBlock *>> predecessors;
        std::map<const IR::BasicBlock *, smt::expr> entered;
        for (IR::BasicBlock *block : transform.src.getBBs()) {
            for (const IR::BasicBlock &target : block->targets()) {
                predecessors[&target].push_back(block);
                smt::expr edge = state->getJumpCond(*block, target);
                auto found = entered.find(&target);
                if (found == entered.end())
                    entered.emplace(&target, edge);
                else
                    found->second = found->second || edge;
            }
        }
        IR::DomTree dominators_of_blocks(transform.src, IR::CFG(transform.src));
        struct Assumed {
            const IR::BasicBlock *block;
            smt::expr condition;
        };
        std::vector<Assumed> assumed;
        for (IR::BasicBlock *block : transform.src.getBBs()) {
            if (block->getName() == "#init") continue;
            smt::expr path = state->getPath(*block);
            smt::expr facts = true;
            for (const Assumed &fact : assumed) {
                if (fact.block != block && dominators_of_blocks.dominates(fact.block, block))
                    facts = facts && fact.condition;
            }
            // what the block starts with: the last instruction of its one predecessor, nothing in the first block,
            // else it is not known
            const IR::State::ValTy *previous = nullptr;
            bool known = block == &transform.src.getFirstBB();
            auto before = predecessors.find(block);
            if (!known && before != predecessors.end() && before->second.size() == 1 &&
                before->second[0]->size() != 0) {
                previous = state->at(before->second[0]->back());
                known = previous != nullptr;
            }
            for (const IR::Instr &instruction : block->instrs()) {
                const IR::State::ValTy *value = state->at(instruction);
                if (value == nullptr) break; // the path to the rest was dropped
                smt::expr added;
                if (known) {
                    smt::AndExpr difference = value->domain;
                    if (previous != nullptr) difference.del(previous->domain);
                    added = difference();
                } else {
                    added = value->domain();
                }
                auto reached = [=, &entered]() -> smt::expr {
                    if (known) return path && (previous != nullptr ? previous->domain() : smt::expr(true));
                    return entered.count(block) != 0 ? entered.at(block) : smt::expr(false);
                };
                if (auto *point = dynamic_cast<const SmithPoint *>(&instruction)) {
                    const Point &info = unit.points[point->id];
                    if (info.kind != Kind::Premise) {
                        smt::expr broken = point->condition != nullptr || point->is_restrict()
                                               ? !(point->seen.value == 1 && point->seen.non_poison)
                                               : smt::expr(true);
                        tasks.push_back({path && facts && broken,
                                         [=] { return path && facts && value->domain() && broken; },
                                         "point",
                                         &info,
                                         nullptr,
                                         {}});
                    }
                } else if (auto *assume = dynamic_cast<const IR::Assume *>(&instruction)) {
                    auto operands = assume->operands();
                    const IR::State::ValTy *condition = operands.empty() ? nullptr : state->at(*operands[0]);
                    if (assume->getKind() == IR::Assume::Dereferenceable ||
                        assume->getKind() == IR::Assume::Align) { // what the callee returned is a valid object
                        facts = facts && added;
                        assumed.push_back({block, added});
                    } else if (condition != nullptr) {
                        smt::expr holds = condition->val.value == 1 && condition->val.non_poison;
                        facts = facts && holds;
                        assumed.push_back({block, holds});
                    }
                } else if (!added.isTrue()) {
                    tasks.push_back({path && facts && !added,
                                     [=] { return reached() && facts && !added; },
                                     "ub",
                                     nullptr,
                                     &instruction,
                                     {}});
                }
                if (dynamic_cast<const IR::Load *>(&instruction) != nullptr &&
                    given_memory.count(instruction.getName()) != 0 && value->val.non_poison.isBool()) {
                    // C has no poison to find in memory a caller gives: this is what the config flag asks for, one load
                    // at a time instead of for every byte (a quantifier the solver does not finish)
                    facts = facts && value->val.non_poison;
                    assumed.push_back({block, value->val.non_poison});
                }
                previous = value;
                known = true;
            }
        }
        phase("collecting the " + std::to_string(tasks.size()) + " conditions to check");
        std::vector<Job *> jobs;
        // Writing a query takes time of its own, in a function with thousands of checks, so the budget is looked at
        // between queries: a proof that cannot finish does not spend minutes before the solver starts.
        for (Task &task : tasks) {
            if (charged() > options.budget_seconds) {
                timed_out = true;
                break;
            }
            prepare(task.job, task.small, task.name);
            jobs.push_back(&task.job);
            outcome.queries_written++;
        }
        phase("writing " + std::to_string(outcome.queries_written) + " of the " + std::to_string(tasks.size()) +
              " queries as SMT");
        if (!timed_out) timed_out = !solve(jobs, "queries");
        phase("the solver");
        if (options.dump) std::cerr << tasks.size() << " queries decided at " << seconds_since(start) << " s\n";
        if (!timed_out) { // the small query failed: ask in full
            std::vector<Job *> again;
            for (Task &task : tasks) {
                if (task.job.verdict != Verdict::Sat) continue;
                prepare(task.job, task.full(), task.name);
                again.push_back(&task.job);
            }
            timed_out = !solve(again, "full queries");
            if (options.dump)
                std::cerr << again.size() << " asked in full, decided at " << seconds_since(start) << " s\n";
        }
        for (Task &task : tasks) {
            if (timed_out) break;
            if (task.job.verdict == Verdict::Unsat) continue;
            if (task.job.verdict != Verdict::Sat) {
                outcome.status = Outcome::Error;
                outcome.error = std::string("the solver gave up on ") +
                                (task.point != nullptr ? "a proof point" : "undefined behaviour");
                break;
            }
            // a counterexample: Z3 finds its values, if it does it quickly
            smt::set_query_timeout("3000000");
            smt::Solver finder(true);
            finder.add(task.job.whole);
            smt::Result found;
            with_z3_cpu_limit(std::min(3.0, std::max(0.5, options.budget_seconds - charged())),
                              [&] { found = finder.check(task.name); });
            const smt::Model *model = found.isSat() ? &found.getModel() : nullptr;
            outcome.findings.push_back(task.point != nullptr ? describe(*task.point, model)
                                                             : describe_unlocated(*task.instruction, model));
        }
        // Which queries were slow, and which costly operations their values depend on.
        {
            outcome.query_count = static_cast<unsigned>(tasks.size());
            std::vector<const Task *> order;
            for (const Task &task : tasks) {
                if (task.job.cpu > 0) order.push_back(&task);
            }
            // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order): ordered by CPU time, not by address
            std::stable_sort(order.begin(), order.end(),
                             [](const Task *a, const Task *b) { return a->job.cpu > b->job.cpu; });
            for (size_t i = 0; i < order.size() && i < 4; i++) {
                QueryTime time = name_query(*order[i]);
                time.cpu = order[i]->job.cpu;
                time.ran_out = order[i]->job.verdict == Verdict::Timeout;
                std::set<const llvm::Instruction *> cone;
                std::vector<const llvm::Instruction *> roots;
                if (order[i]->point != nullptr && order[i]->point->where != nullptr) {
                    roots.push_back(order[i]->point->where);
                } else if (order[i]->instruction != nullptr) {
                    std::vector<std::string> names = {order[i]->instruction->getName()};
                    for (const IR::Value *operand : order[i]->instruction->operands())
                        names.push_back(operand->getName());
                    for (const std::string &name : names) {
                        auto found = by_llvm_name.find(name);
                        if (found != by_llvm_name.end()) roots.push_back(found->second);
                    }
                }
                for (const llvm::Instruction *root : roots) {
                    std::set<const llvm::Instruction *> part = data_cone(root);
                    cone.insert(part.begin(), part.end());
                }
                for (size_t c = 0; c < unit.costs.size(); c++) {
                    if (unit.costs[c].weight != 3) continue;
                    for (const llvm::Instruction *operation : unit.costs[c].instructions) {
                        if (cone.count(operation) != 0) {
                            time.feeds.push_back(c);
                            break;
                        }
                    }
                }
                outcome.queries.push_back(time);
            }
        }
        if (timed_out)
            outcome.status = Outcome::Timeout;
        else if (outcome.status != Outcome::Error && !outcome.findings.empty())
            outcome.status = Outcome::Failed;
    } catch (util::AliveException &exception) {
        outcome.status = Outcome::Error;
        outcome.error = "Alive2: " + exception.msg;
    } catch (std::exception &exception) {
        outcome.status = Outcome::Error;
        outcome.error = std::string("Alive2: ") + exception.what();
    } catch (...) {
        outcome.status = Outcome::Error;
        outcome.error = "Alive2 failed on " + unit.name + " (out of memory, or a construct it does not know)";
    }
    outcome.seconds = seconds_since(start);
    outcome.cpu_seconds = process_cpu_seconds() - cpu_start;
    outcome.charged = charged();
    return outcome;
}

} // namespace prove
