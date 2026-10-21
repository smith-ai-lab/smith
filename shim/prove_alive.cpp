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

#include <chrono>
#include <functional>
#include <iostream>
#include <sstream>

namespace prove {
namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// A proof point in Alive's IR: it evaluates its condition where it stands and does nothing else.
class SmithPoint final : public IR::Instr {
  public:
    SmithPoint(unsigned id, IR::Value *condition)
        : Instr(IR::Type::voidTy, "#point" + std::to_string(id)), condition(condition), id(id) {}

    IR::Value *condition;
    unsigned id;
    mutable IR::StateValue seen;

    std::vector<IR::Value *> operands() const override {
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
    }
    void print(std::ostream &os) const override {
        os << "point " << id;
    }
    IR::StateValue toSMT(IR::State &state) const override {
        if (condition != nullptr) seen = state[*condition];
        return {};
    }
    smt::expr getTypeConstraints(const IR::Function &) const override {
        return true;
    }
    std::unique_ptr<IR::Instr> dup(IR::Function &, const std::string &) const override {
        return std::make_unique<SmithPoint>(id, condition);
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
            return Shape::Unsigned;
        case llvm::dwarf::DW_ATE_unsigned_char:
            return Shape::Unsigned;
        case llvm::dwarf::DW_ATE_signed_char:
            return Shape::Signed;
        default:
            return Shape::Signed;
        }
    }
    return Shape::Signed;
}

std::string render(const smt::Model &model, const IR::StateValue &value, unsigned bits, Shape shape) {
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

Outcome alive_prove(Prepared &prepared, const Options &options) {
    Outcome outcome;
    Clock::time_point start = Clock::now();
    llvm::Function &function = *prepared.function;
    llvm::Module &module = *prepared.module;
    Unit &unit = prepared.unit;
    std::ostringstream log;
    static smt::smt_initializer solver_context;
    try {
        // C has no poison to pass, and what a pointer argument points to holds values.
        util::config::disable_poison_input = false;
        util::config::disable_undef_input = true;
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
                if (!argument->hasName() && !llvm::isa<llvm::Constant>(argument)) argument->setName("argument");
            }
        }
        std::set<std::string> given_memory; // loads of what a parameter or a global holds, which is values
        std::map<std::string, std::pair<std::string, unsigned>> locations; // where each named instruction is written
        for (llvm::Instruction &instruction : llvm::instructions(function)) {
            if (instruction.getType()->isVoidTy()) continue;
            if (!instruction.hasName()) instruction.setName("t");
            if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
                const llvm::Value *object = llvm::getUnderlyingObject(load->getPointerOperand());
                if (llvm::isa<llvm::Argument>(object) || llvm::isa<llvm::GlobalVariable>(object))
                    given_memory.insert(llvm_util::value_name(instruction));
            }
            if (instruction.getDebugLoc()) {
                locations[llvm_util::value_name(instruction)] = {instruction.getDebugLoc()->getFilename().str(),
                                                                 instruction.getDebugLoc().getLine()};
            }
        }
        auto source = llvm_util::llvm2alive(function, library.getTLI(function), true);
        if (!source) {
            outcome.status = Outcome::Error;
            outcome.error = "cannot translate " + unit.name + " to Alive2's IR: " + log.str();
            return outcome;
        }
        std::vector<std::vector<std::string>> call_site_names(unit.call_sites.size());
        for (size_t i = 0; i < unit.call_sites.size(); i++) {
            for (llvm::Value *argument : unit.call_sites[i].arguments) {
                call_site_names[i].push_back(llvm::isa<llvm::ConstantInt>(argument) ? ""
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
                if (call == nullptr || call->getFnName() != "@smith_point") continue;
                auto id = IR::getInt(*call->getArgs()[0].first);
                IR::Value *argument = call->getArgs()[1].first;
                IR::Value *condition = argument;
                if (auto *constant = dynamic_cast<IR::IntConst *>(condition);
                    constant != nullptr && constant->getInt() != nullptr && *constant->getInt() == 0)
                    condition = nullptr; // a point with no condition fails when it is reached
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
        smt::set_query_timeout(std::to_string(static_cast<long>(options.budget_seconds * 1000) + 1));
        solver_context.reset(); // takes the timeout
        transform.preprocess();
        tools::TransformVerify verifier(transform, false);
        auto [state, target_state] = verifier.exec();
        if (options.dump) std::cerr << transform.src << "\n";
        if (options.dump) std::cerr << "symbolic execution took " << seconds_since(start) << " s\n";
        if (seconds_since(start) > options.budget_seconds) {
            outcome.status = Outcome::Timeout;
            outcome.seconds = seconds_since(start);
            return outcome;
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
        // Pointers to objects of different types are different objects.
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
        auto describe = [&](const Point &point, const smt::Model &model) {
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
                    if (auto *constant = llvm::dyn_cast<llvm::ConstantInt>(site.arguments[j])) {
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
        smt::Solver solver;
        solver.add(axioms && assumptions);
        auto check = [&](const smt::expr &formula, const char *name, smt::Result &result) {
            if (seconds_since(start) >= options.budget_seconds) {
                result = smt::Result();
                return false;
            }
            smt::SolverPush push(solver);
            solver.add(formula);
            Clock::time_point query = Clock::now();
            result = solver.check(name);
            if (options.dump)
                std::cerr << "query " << name << ": " << seconds_since(query) << " s, at " << seconds_since(start)
                          << " s\n";
            return true;
        };
        // A failure needs a run that reaches the point with no undefined behaviour before it, and that run has to be
        // described in full. Most points hold anyway, and are shown to with only the path to them and what is assumed
        // on the way: dropping the rest of that description only adds runs, so a proof of the smaller query is a proof
        // of the full one. Only a failure of the small query is asked again in full.
        bool timed_out = false;
        auto decide = [&](const smt::expr &small, const std::function<smt::expr()> &full, const char *name,
                          smt::Result &result) {
            if (!check(small, name, result)) return false;
            if (!result.isSat()) return true;
            return check(full(), name, result);
        };
        auto describe_unlocated = [&](const IR::Instr &instruction, const smt::Model &model) {
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
            if (timed_out || outcome.status == Outcome::Error) break;
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
                auto reached = [&]() -> smt::expr {
                    if (known) return path && (previous != nullptr ? previous->domain() : smt::expr(true));
                    return entered.count(block) != 0 ? entered.at(block) : smt::expr(false);
                };
                if (auto *point = dynamic_cast<const SmithPoint *>(&instruction)) {
                    const Point &info = unit.points[point->id];
                    if (info.kind != Kind::Premise) {
                        smt::expr broken = point->condition != nullptr
                                               ? !(point->seen.value == 1 && point->seen.non_poison)
                                               : smt::expr(true);
                        smt::Result result;
                        if (!decide(
                                path && facts && broken, [&] { return path && facts && value->domain() && broken; },
                                "point", result)) {
                            timed_out = true;
                            break;
                        }
                        if (result.isSat()) {
                            outcome.findings.push_back(describe(info, result.getModel()));
                        } else if (!result.isUnsat()) {
                            if (seconds_since(start) >= options.budget_seconds)
                                timed_out = true;
                            else {
                                outcome.status = Outcome::Error;
                                outcome.error = "the solver gave up on a proof point: " + result.getReason();
                            }
                            break;
                        }
                    }
                } else if (auto *assume = dynamic_cast<const IR::Assume *>(&instruction)) {
                    auto operands = assume->operands();
                    const IR::State::ValTy *condition = operands.empty() ? nullptr : state->at(*operands[0]);
                    if (condition != nullptr) {
                        smt::expr holds = condition->val.value == 1 && condition->val.non_poison;
                        facts = facts && holds;
                        assumed.push_back({block, holds});
                    }
                } else if (!added.isTrue()) {
                    smt::Result result;
                    if (options.dump) {
                        std::cerr << "ub check of ";
                        instruction.print(std::cerr);
                        std::cerr << "\n";
                    }
                    if (!decide(path && facts && !added, [&] { return reached() && facts && !added; }, "ub", result)) {
                        timed_out = true;
                        break;
                    }
                    if (result.isSat()) {
                        outcome.findings.push_back(describe_unlocated(instruction, result.getModel()));
                    } else if (!result.isUnsat()) {
                        if (seconds_since(start) >= options.budget_seconds)
                            timed_out = true;
                        else {
                            outcome.status = Outcome::Error;
                            outcome.error = "the solver gave up on undefined behaviour: " + result.getReason();
                        }
                        break;
                    }
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
    return outcome;
}

} // namespace prove
