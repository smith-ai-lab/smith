// `smith prove`, the LLVM IR half: makes each function provable on its own.
//
// The module that clang emits (UBSan checks in trap mode, debug info, one call per contract) goes through these steps:
//   1. program_load: the files are linked, locals are promoted to registers, and every function is flattened: a callee
//      without an smith_ensures is inlined where it is called, so it is part of the caller and its own contracts become
//      proof points there. The memory a function touches is inferred from its body (LLVM's memory attributes).
//   2. program_prepare, for one function in a copy of the module: a call of a function with an smith_ensures stays a
//   call
//      (LLVM's attributes say what it may write) and gets three slices of that function's body inlined around it: its
//      smith_requires evaluated before the call, the old values its smith_ensures reads, and its smith_ensures after
//      the call. A slice is the callee cloned with only the instructions a contract depends on.
//   3. loops are unrolled; one that is not is an error.
//   4. the contract calls, the traps and the failing branch of assert become calls to smith_point(id, condition),
//      which shim/prove_alive.cpp turns into proof points.
#include "prove.hpp"

#include <chrono>
#include <cstdio>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

using namespace llvm;

namespace prove {
namespace {

// ---- names the macros and clang use ----

enum class Contract { None, Requires, Ensures, Invariant, Assert, Trusted };

Contract contract_of(const Instruction *instruction) {
    auto *call = dyn_cast_or_null<CallBase>(instruction);
    if (call == nullptr || call->getCalledFunction() == nullptr) return Contract::None;
    StringRef name = call->getCalledFunction()->getName();
    if (name == "smith_contract_requires") return Contract::Requires;
    if (name == "smith_contract_ensures") return Contract::Ensures;
    if (name == "smith_contract_invariant") return Contract::Invariant;
    if (name == "smith_contract_assert") return Contract::Assert;
    if (name == "smith_contract_trusted") return Contract::Trusted;
    return Contract::None;
}

bool contract_name(StringRef name) {
    return name == "smith_contract_requires" || name == "smith_contract_ensures" ||
           name == "smith_contract_invariant" || name == "smith_contract_assert" || name == "smith_contract_trusted";
}

// The function a call names, even when the call uses another type than the function declares (an Objective-C message
// send to objc_msgSend$hash does).
Function *direct_callee(const CallBase *call) {
    return dyn_cast<Function>(call->getCalledOperand()->stripPointerCasts());
}

bool is_trap(const Instruction *instruction) {
    auto *call = dyn_cast_or_null<CallBase>(instruction);
    if (call == nullptr || call->getCalledFunction() == nullptr) return false;
    return call->getCalledFunction()->getIntrinsicID() == Intrinsic::ubsantrap;
}

bool is_assert_failure(const Instruction *instruction) {
    auto *call = dyn_cast_or_null<CallBase>(instruction);
    if (call == nullptr || call->getCalledFunction() == nullptr) return false;
    StringRef name = call->getCalledFunction()->getName();
    return name == "__assert_rtn" || name == "__assert_fail" || name == "__assert";
}

bool is_abort(const Instruction *instruction) {
    auto *call = dyn_cast_or_null<CallBase>(instruction);
    if (call == nullptr || call->getCalledFunction() == nullptr) return false;
    Function *function = call->getCalledFunction();
    return function->getName() == "abort" || function->getIntrinsicID() == Intrinsic::trap;
}

// What the ids of llvm.ubsantrap mean: the order of clang's SanitizerHandler list
// (clang/lib/CodeGen/SanitizerHandler.h).
const char *const trap_messages[] = {
    "signed integer overflow in addition",
    "__builtin_unreachable() is reached",
    "control flow integrity check failed",
    "integer division or remainder overflows",
    "dynamic type cache miss",
    "floating-point to integer conversion overflows",
    "function called with a mismatched signature",
    "implicit conversion overflows or loses data",
    "invalid use of a builtin function",
    "invalid Objective-C cast",
    "a loaded value is invalid for its type",
    "execution reaches the end of a function that returns a value",
    "signed integer overflow in multiplication",
    "signed integer overflow in negation",
    "null passed as an argument annotated _Nonnull",
    "null returned from a function annotated _Nonnull",
    "null passed as an argument declared never null",
    "null returned from a function declared never null",
    "array index out of bounds",
    "pointer arithmetic overflows",
    "shift exponent is too large or negative for the type",
    "signed integer overflow in subtraction",
    "null, misaligned or too small object",
    "alignment assumption violated",
    "variable length array bound is not positive",
};

// ---- metadata that numbers the functions and ties each contract to the function that wrote it ----

MDNode *id_node(LLVMContext &context, unsigned id) {
    return MDNode::get(context, ConstantAsMetadata::get(ConstantInt::get(Type::getInt32Ty(context), id)));
}

unsigned id_of(const MDNode *node) {
    if (node == nullptr || node->getNumOperands() == 0) return 0;
    auto *constant = mdconst::dyn_extract<ConstantInt>(node->getOperand(0));
    return constant == nullptr ? 0 : static_cast<unsigned>(constant->getZExtValue());
}

unsigned function_id(const Function &function) {
    return id_of(function.getMetadata("smith.id"));
}
unsigned owner_id(const Instruction &instruction) {
    return id_of(instruction.getMetadata("smith.owner"));
}

// A function with no body in the module that its header marks [[clang::annotate("smith::edge")]] (smith::io's exports)
// is the edge of the program, as a trusted function is, with no contract: it gets the attribute "smith.edge".
void mark_edges(Module &module) {
    GlobalVariable *annotations = module.getGlobalVariable("llvm.global.annotations");
    if (annotations == nullptr || !annotations->hasInitializer()) return;
    auto *entries = dyn_cast<ConstantArray>(annotations->getInitializer());
    if (entries == nullptr) return;
    for (const Use &entry : entries->operands()) {
        auto *record = dyn_cast<ConstantStruct>(entry.get());
        if (record == nullptr || record->getNumOperands() < 2) continue;
        auto *function = dyn_cast<Function>(record->getOperand(0)->stripPointerCasts());
        auto *text = dyn_cast<GlobalVariable>(record->getOperand(1)->stripPointerCasts());
        if (function == nullptr || text == nullptr || !text->hasInitializer()) continue;
        auto *data = dyn_cast<ConstantDataArray>(text->getInitializer());
        if (data != nullptr && data->isCString() && data->getAsCString() == "smith::edge")
            function->addFnAttr("smith.edge");
    }
}

// Gives every defined function a number, and every contract call the number of the function it is written in.
void annotate(Module &module, unsigned &counter) {
    mark_edges(module);
    for (Function &function : module) {
        if (function.isDeclaration()) continue;
        MDNode *node = id_node(module.getContext(), ++counter);
        function.setMetadata("smith.id", node);
        for (Instruction &instruction : instructions(function)) {
            if (contract_of(&instruction) != Contract::None || is_assert_failure(&instruction))
                instruction.setMetadata("smith.owner", node);
        }
    }
}

bool has_own_contract(const Function &function, Contract kind) {
    for (const Instruction &instruction : instructions(function)) {
        if (contract_of(&instruction) == kind && owner_id(instruction) == function_id(function)) return true;
    }
    return false;
}

// A smith_requires, smith_ensures, smith_invariant, smith_assert or C `assert` the function wrote itself
// (smith_trusted() is not one). A function with only asserts is proved as a unit, and callers still inline it, since it
// has no ensures.
bool has_any_own_contract(const Function &function) {
    for (const Instruction &instruction : instructions(function)) {
        Contract kind = contract_of(&instruction);
        bool is_contract = kind != Contract::None && kind != Contract::Trusted;
        // C's assert (a macro) is the call of its failure function, written in the function that holds it
        if ((is_contract || is_assert_failure(&instruction)) && owner_id(instruction) == function_id(function))
            return true;
    }
    return false;
}

// A function that carries smith_trusted(): assumed through its contract, not proved, its body not read.
bool is_trusted(const Function &function) {
    return !function.isDeclaration() && has_own_contract(function, Contract::Trusted);
}

// What program_units proves, and what program_load asks before it flattens anything: a defined function, not a
// snapshot and not trusted, with a contract of its own. One definition, so the gate and the units cannot disagree.
bool is_unit(const Function &function) {
    return !function.isDeclaration() && !function.hasFnAttribute("smith.snapshot") && !is_trusted(function) &&
           has_any_own_contract(function);
}

// A function with an smith_ensures, or a trusted one, is used through its contract; any other is part of its caller.
bool is_modular(const Function &function) {
    return !function.isDeclaration() && (has_own_contract(function, Contract::Ensures) || is_trusted(function));
}

// ---- passes ----

struct Analyses {
    LoopAnalysisManager loops;
    FunctionAnalysisManager functions;
    CGSCCAnalysisManager components;
    ModuleAnalysisManager modules;
    PassBuilder builder;

    Analyses() {
        builder.registerModuleAnalyses(modules);
        builder.registerCGSCCAnalyses(components);
        builder.registerFunctionAnalyses(functions);
        builder.registerLoopAnalyses(loops);
        builder.crossRegisterProxies(loops, functions, components, modules);
    }

    bool run_function(Function &function, const char *pipeline, std::string &error) {
        FunctionPassManager manager;
        if (Error failure = builder.parsePassPipeline(manager, pipeline)) {
            error = toString(std::move(failure));
            return false;
        }
        manager.run(function, functions);
        functions.invalidate(function, PreservedAnalyses::none());
        return true;
    }

    bool run_module(Module &module, const char *pipeline, std::string &error) {
        ModulePassManager manager;
        if (Error failure = builder.parsePassPipeline(manager, pipeline)) {
            error = toString(std::move(failure));
            return false;
        }
        manager.run(module, modules);
        return true;
    }
};

// Locals live in registers, redundant loads go, and the control flow is simple. These only refine the program, so a
// proof of the result holds for the original in every case LLVM keeps defined.
const char *const clean_pipeline = "sroa<modify-cfg>,early-cse,simplifycfg,dce";

// The same without early-cse, which would replace a load by the value an earlier store wrote. A contract is evaluated
// where the caller can read the memory the callee wrote, so the loads of its snapshot must stay loads.
const char *const light_pipeline = "sroa<modify-cfg>,simplifycfg,dce";

// Loops of constant bounds are unrolled, whatever the cost model thinks.
const char *const unroll_pipeline =
    "loop-simplify,lcssa,loop(indvars),loop-unroll<O3;full-unroll-max=1000000;no-partial;no-runtime;upperbound>,"
    "sroa<modify-cfg>,early-cse,simplifycfg,instcombine,gvn,instcombine,simplifycfg,dse,dce";

void set_option(const char *name, const char *value) {
    auto &options = cl::getRegisteredOptions();
    auto found = options.find(name);
    if (found != options.end()) found->second->addOccurrence(0, name, value);
}

void allow_unlimited_unrolling() {
    set_option("unroll-threshold", "100000000");
    set_option("unroll-max-percent-threshold-boost", "100000");
    set_option("unroll-max-iteration-count-to-analyze", "1000000");
    set_option("unroll-max-upperbound", "1000000");
}

// ---- pointer parameters ----

// The size of what a pointer type points to, in bytes, from debug info; 0 when it is not known (void, incomplete).
uint64_t pointee_bytes(const DIType *type) {
    while (type != nullptr) {
        auto *derived = dyn_cast<DIDerivedType>(type);
        if (derived == nullptr) break;
        unsigned tag = derived->getTag();
        if (tag == dwarf::DW_TAG_pointer_type) {
            type = derived->getBaseType();
            break;
        }
        if (tag == dwarf::DW_TAG_typedef || tag == dwarf::DW_TAG_const_type || tag == dwarf::DW_TAG_volatile_type ||
            tag == dwarf::DW_TAG_restrict_type || tag == dwarf::DW_TAG_atomic_type) {
            type = derived->getBaseType();
            continue;
        }
        return 0;
    }
    if (type == nullptr) return 0;
    while (auto *derived = dyn_cast<DIDerivedType>(type)) {
        unsigned tag = derived->getTag();
        if (tag == dwarf::DW_TAG_typedef || tag == dwarf::DW_TAG_const_type || tag == dwarf::DW_TAG_volatile_type ||
            tag == dwarf::DW_TAG_restrict_type || tag == dwarf::DW_TAG_atomic_type) {
            if (derived->getBaseType() == nullptr) return 0;
            type = derived->getBaseType();
            continue;
        }
        break;
    }
    return (type->getSizeInBits() + 7) / 8;
}

// A pointer parameter points to a whole object of its type: it is not null and its object is all there. That is the
// meaning `Moves *moves` has in the contracts of the issue, and a call that passes anything else breaks it.
void mark_pointer_parameters(Function &function) {
    DISubprogram *subprogram = function.getSubprogram();
    if (subprogram == nullptr || subprogram->getType() == nullptr) return;
    auto types = subprogram->getType()->getTypeArray();
    if (types.size() != function.arg_size() + 1) return;
    for (unsigned i = 0; i < function.arg_size(); i++) {
        Argument *argument = function.getArg(i);
        if (!argument->getType()->isPointerTy() || argument->hasStructRetAttr() || argument->hasByValAttr()) continue;
        uint64_t bytes = pointee_bytes(types[i + 1]);
        if (bytes == 0) continue;
        argument->addAttr(Attribute::getWithDereferenceableBytes(function.getContext(), bytes));
        argument->addAttr(Attribute::NonNull);
    }
}

// Whether a const lies between a pointer type and what it points to (a `const Board *`).
bool points_to_const(const DIType *type) {
    while (auto *derived = dyn_cast_or_null<DIDerivedType>(type)) {
        unsigned tag = derived->getTag();
        if (tag == dwarf::DW_TAG_pointer_type) {
            type = derived->getBaseType();
            while (auto *inner = dyn_cast_or_null<DIDerivedType>(type)) {
                if (inner->getTag() == dwarf::DW_TAG_const_type) return true;
                if (inner->getTag() != dwarf::DW_TAG_typedef && inner->getTag() != dwarf::DW_TAG_volatile_type)
                    return false;
                type = inner->getBaseType();
            }
            return false;
        }
        type = derived->getBaseType(); // typedef and qualifiers of the pointer itself
    }
    return false;
}

// What the body does with a pointer parameter says what the object must be: aligned for the widest access it makes, and
// writable when it stores through it and the type is not const. (After the body is flat, so that what helpers do
// counts.)
void mark_accessed_parameters(Function &function) {
    DISubprogram *subprogram = function.getSubprogram();
    if (subprogram == nullptr || subprogram->getType() == nullptr) return;
    auto types = subprogram->getType()->getTypeArray();
    if (types.size() != function.arg_size() + 1) return;
    for (unsigned i = 0; i < function.arg_size(); i++) {
        Argument *argument = function.getArg(i);
        if (!argument->getType()->isPointerTy() || !argument->hasAttribute(Attribute::NonNull)) continue;
        Align widest(1);
        bool stored = false;
        for (Instruction &instruction : instructions(function)) {
            if (auto *load = dyn_cast<LoadInst>(&instruction)) {
                if (getUnderlyingObject(load->getPointerOperand()) == argument)
                    widest = std::max(widest, load->getAlign());
            } else if (auto *store = dyn_cast<StoreInst>(&instruction)) {
                if (getUnderlyingObject(store->getPointerOperand()) == argument) {
                    widest = std::max(widest, store->getAlign());
                    stored = true;
                }
            } else if (auto *call = dyn_cast<CallBase>(&instruction)) {
                // Passed on to a function that stores through its parameter (one used through its contract is not
                // inlined): the object must be writable and aligned for that too.
                Function *callee = direct_callee(call);
                if (callee == nullptr || callee->isIntrinsic()) continue;
                for (unsigned a = 0; a < call->arg_size() && a < callee->arg_size(); a++) {
                    if (getUnderlyingObject(call->getArgOperand(a)) != argument) continue;
                    if (callee->getArg(a)->hasAttribute(Attribute::Writable)) stored = true;
                    if (MaybeAlign align = callee->getArg(a)->getParamAlign()) widest = std::max(widest, *align);
                }
            }
        }
        if (widest > Align(1)) argument->addAttr(Attribute::getWithAlignment(function.getContext(), widest));
        if (stored && !points_to_const(types[i + 1])) argument->addAttr(Attribute::Writable);
    }
}

// ---- the call graph ----

// Inlines, into `function`, every call of a defined function that is not used through its contract.
bool inline_helpers(Function &function, const std::set<const Function *> &recursive, std::string &error) {
    for (;;) {
        CallBase *target = nullptr;
        for (Instruction &instruction : instructions(function)) {
            auto *call = dyn_cast<CallBase>(&instruction);
            if (call == nullptr) continue;
            Function *callee = call->getCalledFunction();
            if (callee == nullptr || callee->isDeclaration() || callee == &function || is_modular(*callee) ||
                recursive.count(callee) != 0 || callee->isVarArg())
                continue; // a variable argument list cannot be inlined into a function that has none
            target = call;
            break;
        }
        if (target == nullptr) return true;
        InlineFunctionInfo info;
        InlineResult result = InlineFunction(*target, info);
        if (!result.isSuccess()) {
            error = std::string("cannot inline ") + target->getCalledFunction()->getName().str() + ": " +
                    result.getFailureReason();
            return false;
        }
    }
}

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point begin) {
    return std::chrono::duration<double>(Clock::now() - begin).count();
}

unsigned instruction_count(const Module &module) {
    unsigned count = 0;
    for (const Function &function : module)
        count += function.getInstructionCount();
    return count;
}

const char *const snapshot_suffix = ".smith.contract";

// A modular function is used through its contract, and a contract about memory is about the memory after the call.
// The function that is proved (and called) is cleaned until a load that follows a store is the stored value; the
// contract is read from this copy, made before that, in which a load of the callee's memory is still a load.
void make_snapshot(Function &function) {
    ValueToValueMapTy map;
    Function *copy = CloneFunction(&function, map);
    copy->setName(function.getName() + snapshot_suffix);
    copy->setLinkage(GlobalValue::InternalLinkage);
    copy->addFnAttr("smith.snapshot");
    // A check that traps (UBSan's, an unreachable) does not decide whether a contract after it holds: the function is
    // proved on its own not to trap. Left in, every block after a check would be control dependent on every branch
    // before it (a trap is an exit, so nothing after it post-dominates what came before), and the slice of a contract
    // would take in the body's loads.
    SmallVector<CondBrInst *, 16> checks;
    for (BasicBlock &block : *copy) {
        auto *branch = dyn_cast<CondBrInst>(block.getTerminator());
        if (branch == nullptr) continue;
        if (isa<UnreachableInst>(branch->getSuccessor(0)->getTerminator()) ||
            isa<UnreachableInst>(branch->getSuccessor(1)->getTerminator()))
            checks.push_back(branch);
    }
    for (CondBrInst *branch : checks) {
        bool first_traps = isa<UnreachableInst>(branch->getSuccessor(0)->getTerminator());
        BasicBlock *kept = branch->getSuccessor(first_traps ? 1 : 0);
        if (isa<UnreachableInst>(kept->getTerminator())) continue; // both trap: leave it
        UncondBrInst::Create(kept, branch->getIterator());
        branch->eraseFromParent();
    }
    removeUnreachableBlocks(*copy);
}

bool is_snapshot(const Function &function) {
    return function.hasFnAttribute("smith.snapshot");
}

} // namespace

bool program_load(Program &program, const std::vector<std::string> &bitcode, std::string &error) {
    Clock::time_point begin = Clock::now();
    program.context = std::make_unique<LLVMContext>();
    unsigned counter = 0;
    for (const std::string &path : bitcode) {
        SMDiagnostic diagnostic;
        std::unique_ptr<Module> module = parseIRFile(path, diagnostic, *program.context);
        if (module == nullptr) {
            error = "cannot read " + path + ": " + diagnostic.getMessage().str();
            return false;
        }
        annotate(*module, counter);
        if (program.module == nullptr) {
            program.module = std::move(module);
        } else if (Linker::linkModules(*program.module, std::move(module))) {
            error = "cannot link " + path;
            return false;
        }
    }
    Module &module = *program.module;
    double read_seconds = seconds_since(begin);
    // Flattening every function costs far more than reading the module (a program of 300k instructions grew to 100M),
    // so it is done only when some function has a contract of its own (an assert included), which is what
    // program_units proves.
    unsigned own = 0, trusted = 0;
    for (const Function &function : module) {
        if (function.isDeclaration()) continue;
        if (is_unit(function)) {
            own++;
        } else if (is_trusted(function)) {
            trusted++;
        }
    }
    unsigned before = instruction_count(module);
    if (program.verbose) {
        std::printf("phase read and link: %.2f s, %u instructions\n", read_seconds, before);
        std::printf("phase gate: %u functions with a contract of their own, %u trusted%s\n", own, trusted,
                    own == 0 ? "; skipped flattening, nothing to prove" : "");
    }
    if (own == 0) return true;
    Clock::time_point flatten_begin = Clock::now();
    for (Function &function : module) {
        for (const Use &use : function.uses()) {
            auto *call = dyn_cast<CallBase>(use.getUser());
            bool as_callee = call != nullptr && call->isCallee(&use);
            if (as_callee && call->getFunction() == &function) continue; // calling itself does not make it called
            program.called.insert(&function);
        }
    }
    for (Function &function : module) { // a contract reads its condition and nothing else
        if (contract_name(function.getName())) {
            function.setMemoryEffects(MemoryEffects::none());
            function.addFnAttr(Attribute::NoUnwind);
        }
    }
    Analyses analyses;
    for (Function &function : module) {
        if (function.isDeclaration()) continue;
        if (!analyses.run_function(function, light_pipeline, error)) return false;
        mark_pointer_parameters(function);
    }

    // Callees first. A function that calls itself, directly or through others, is not inlined, and cannot be proved.
    CallGraph graph(module);
    std::vector<Function *> order;
    for (scc_iterator<CallGraph *> component = scc_begin(&graph); !component.isAtEnd(); ++component) {
        bool cycle = component.hasCycle();
        for (CallGraphNode *node : *component) {
            Function *function = node->getFunction();
            if (function == nullptr || function->isDeclaration()) continue;
            if (cycle) program.recursive.insert(function);
            order.push_back(function);
        }
    }
    for (Function *function : order) {
        if (program.recursive.count(function) != 0) continue;
        if (!inline_helpers(*function, program.recursive, error)) return false;
        if (is_modular(*function)) make_snapshot(*function);
        if (!analyses.run_function(*function, clean_pipeline, error)) return false;
        mark_accessed_parameters(*function);
    }
    // A trusted function is assumed to do what its contract says. What the prover could infer from its body (inline
    // assembly, a call into the operating system) is "may write any memory", which would make a caller forget
    // everything it knew. It is assumed instead to touch only what its pointer parameters point to (its smith_ensures
    // says what it does to them) and nothing else a proved function can see; inference may only narrow that.
    for (Function &function : module) {
        if (!is_trusted(function) && !function.hasFnAttribute("smith.edge")) continue;
        // What it returns is a value, never poison: a caller may branch on it.
        if (!function.getReturnType()->isVoidTy()) function.addRetAttr(Attribute::NoUndef);
        function.setMemoryEffects(MemoryEffects::argMemOnly(ModRefInfo::ModRef) |
                                  MemoryEffects::inaccessibleMemOnly(ModRefInfo::ModRef));
        // It frees nothing the caller can see either, unless it is given a pointer it may write through (it could be
        // free's wrapper); without nofree a caller could not rely on any object it was given still being there.
        bool may_free = false;
        for (const Argument &argument : function.args()) {
            if (argument.getType()->isPointerTy() && !argument.hasAttribute(Attribute::ReadOnly) &&
                !function.getParamAttribute(argument.getArgNo(), Attribute::ReadOnly).isValid())
                may_free = true;
        }
        if (!may_free || function.hasFnAttribute("smith.edge")) function.addFnAttr(Attribute::NoFree);
    }
    if (!analyses.run_module(module, "inferattrs,cgscc(function-attrs)", error)) return false;
    if (program.verbose)
        std::printf("phase flatten: %.2f s, %u instructions before, %u after\n", seconds_since(flatten_begin), before,
                    instruction_count(module));
    Clock::time_point verify_begin = Clock::now();
    if (verifyModule(module, &errs())) {
        error = "the linked module does not verify";
        return false;
    }
    if (program.verbose) std::printf("phase verify: %.2f s\n", seconds_since(verify_begin));
    return true;
}

std::vector<std::string> program_units(const Program &program) {
    std::vector<std::string> names;
    for (const Function &function : *program.module) {
        if (is_unit(function)) names.push_back(function.getName().str());
    }
    return names;
}

bool program_is_trusted(const Program &program, const std::string &name) {
    const Function *function = program.module->getFunction(name);
    return function != nullptr && !is_snapshot(*function) && is_trusted(*function);
}

namespace {
std::string source_name(const Function &function);
}

std::string program_label(const Program &program, const std::string &name) {
    const Function *function = program.module->getFunction(name);
    return function == nullptr ? name : source_name(*function);
}

std::vector<Trusted> program_trusted(const Program &program) {
    std::vector<Trusted> found;
    for (const Function &function : *program.module) {
        if (function.isDeclaration() || is_snapshot(function) || !is_trusted(function)) continue;
        Trusted trusted;
        trusted.name = source_name(function);
        if (const DISubprogram *subprogram = function.getSubprogram()) {
            trusted.file = subprogram->getFilename().str();
            trusted.line = subprogram->getLine();
        }
        for (const Instruction &instruction : instructions(function)) {
            if (owner_id(instruction) != function_id(function)) continue;
            Contract kind = contract_of(&instruction);
            trusted.requires_count += kind == Contract::Requires;
            trusted.ensures_count += kind == Contract::Ensures;
        }
        found.push_back(trusted);
    }
    return found;
}

namespace {

// ---- slices ----

// What a set of roots (contract calls, or loads) depends on in a function, with the control flow that decides it.
struct Cone {
    DenseSet<const Instruction *> keep;
    SmallVector<LoadInst *, 4> old_loads; // loads of the state before the function ran, which an smith_ensures reads
};

struct Analysis {
    Function &function;
    DominatorTree dominators;
    PostDominatorTree post;
    DenseMap<const BasicBlock *, SmallPtrSet<const BasicBlock *, 4>> control; // block -> the blocks that decide it
    SmallVector<const Instruction *, 16> writers;
    mutable DenseMap<const BasicBlock *, SmallPtrSet<const BasicBlock *, 32>> reachable;

    explicit Analysis(Function &function) : function(function), dominators(function), post(function) {
        for (BasicBlock &block : function) {
            for (BasicBlock *successor : successors(&block)) {
                if (post.properlyDominates(successor, &block)) continue;
                DomTreeNode *stop = post.getNode(&block) == nullptr ? nullptr : post.getNode(&block)->getIDom();
                for (DomTreeNode *node = post.getNode(successor); node != nullptr && node != stop;
                     node = node->getIDom()) {
                    if (node->getBlock() != nullptr) control[node->getBlock()].insert(&block);
                }
            }
        }
        for (Instruction &instruction : instructions(function)) {
            if (writes_state(instruction)) writers.push_back(&instruction);
        }
    }

    // Anything but the contract machinery that may change memory the function does not own.
    static bool writes_state(const Instruction &instruction) {
        if (contract_of(&instruction) != Contract::None || is_trap(&instruction)) return false;
        if (instruction.isDebugOrPseudoInst() || isa<AssumeInst>(instruction)) return false;
        if (auto *call = dyn_cast<IntrinsicInst>(&instruction)) {
            if (call->getIntrinsicID() == Intrinsic::lifetime_start ||
                call->getIntrinsicID() == Intrinsic::lifetime_end)
                return false;
        }
        if (auto *store = dyn_cast<StoreInst>(&instruction)) {
            return !isa<AllocaInst>(getUnderlyingObject(store->getPointerOperand()));
        }
        // A memset or memcpy of a local (the zero-initialization of a struct) writes memory the function owns.
        if (auto *memory = dyn_cast<MemIntrinsic>(&instruction)) {
            return !isa<AllocaInst>(getUnderlyingObject(memory->getRawDest()));
        }
        return instruction.mayWriteToMemory();
    }

    // Whether execution can go from `from` to `to`. LLVM's isPotentiallyReachable gives up after 32 blocks and answers
    // yes, and a function with its contracts' helpers inlined has hundreds: every load would be "read after a write".
    bool reaches(const Instruction *from, const Instruction *to) const {
        const BasicBlock *start = from->getParent();
        if (start == to->getParent() && from->comesBefore(to)) return true;
        auto found = reachable.find(start);
        if (found == reachable.end()) { // the blocks reachable by one step or more, from the end of `start`
            SmallPtrSet<const BasicBlock *, 32> seen;
            SmallVector<const BasicBlock *, 32> work(successors(start).begin(), successors(start).end());
            while (!work.empty()) {
                const BasicBlock *block = work.pop_back_val();
                if (!seen.insert(block).second) continue;
                work.append(successors(block).begin(), successors(block).end());
            }
            found = reachable.insert({start, std::move(seen)}).first;
        }
        return found->second.contains(to->getParent());
    }

    // A write that can run before / after the load, or nullptr.
    const Instruction *write_before(const Instruction &load) const {
        for (const Instruction *writer : writers) {
            if (writer != &load && reaches(writer, &load)) return writer;
        }
        return nullptr;
    }

    const Instruction *write_after(const Instruction &load) const {
        for (const Instruction *writer : writers) {
            if (writer != &load && reaches(&load, writer)) return writer;
        }
        return nullptr;
    }

    bool written_before(const Instruction &load) const {
        return write_before(load) != nullptr;
    }
};

std::string location_of(const Instruction *instruction) {
    if (instruction == nullptr || !instruction->getDebugLoc()) return "";
    return instruction->getDebugLoc()->getFilename().str() + ":" + std::to_string(instruction->getDebugLoc().getLine());
}

// An instruction for a message: where it is in the source, what the instruction is, and, when it came from a helper
// that was inlined, which helper.
std::string describe_instruction(const Instruction *instruction) {
    std::string text;
    raw_string_ostream stream(text);
    instruction->print(stream);
    stream.flush();
    size_t start = text.find_first_not_of(" \t\n");
    text = start == std::string::npos ? text : text.substr(start);
    size_t metadata = text.find(", !");
    if (metadata != std::string::npos) text.resize(metadata);
    if (text.size() > 90) text = text.substr(0, 87) + "...";
    std::string where = location_of(instruction);
    if (where.empty()) {
        // an instruction a pass made has no line; the nearest one before it in its block says where it is near
        for (const Instruction *before = instruction->getPrevNode(); before != nullptr && where.empty();
             before = before->getPrevNode())
            where = location_of(before);
        where = where.empty() ? "no line" : "near " + where;
    }
    std::string result = where + ": `" + text + "`";
    if (instruction->getDebugLoc() && instruction->getDebugLoc()->getInlinedAt() != nullptr)
        result += " (in " + instruction->getDebugLoc()->getScope()->getSubprogram()->getName().str() + ")";
    return result;
}

// Collects what `roots` depend on. `stop` becomes a parameter of the slice, so what computes it is not needed;
// `split_old` turns the loads of the state before the function ran into parameters as well.
bool compute_cone(Analysis &analysis, ArrayRef<Instruction *> roots, const Value *stop, bool split_old, Cone &cone,
                  std::string &error) {
    SmallVector<const Instruction *, 32> work;
    auto add = [&](const Instruction *instruction) {
        if (cone.keep.insert(instruction).second) work.push_back(instruction);
    };
    for (Instruction *root : roots)
        add(root);
    while (!work.empty()) {
        const Instruction *instruction = work.pop_back_val();
        if (instruction == stop) { // it becomes a parameter of the slice
            cone.keep.erase(instruction);
            continue;
        }
        if (auto *load = dyn_cast<LoadInst>(instruction)) {
            if (isa<AllocaInst>(getUnderlyingObject(load->getPointerOperand()))) {
                error = "its contract reads a local array or struct of the function (" + location_of(load) +
                        "); give the contract plain values, which it can read";
                return false;
            }
            if (split_old && !analysis.written_before(*load)) {
                // Once: the load leaves `keep`, so a second user of it adds it to the work again, and a load listed
                // twice is one parameter of the slice taken twice.
                if (!llvm::is_contained(cone.old_loads, load)) cone.old_loads.push_back(const_cast<LoadInst *>(load));
                cone.keep.erase(instruction);
                continue;
            }
        }
        for (const Value *operand : instruction->operands()) {
            if (auto *dependency = dyn_cast<Instruction>(operand)) add(dependency);
        }
        const BasicBlock *block = instruction->getParent();
        auto control = analysis.control.find(block);
        if (control != analysis.control.end()) {
            for (const BasicBlock *decider : control->second)
                add(decider->getTerminator());
        }
        if (isa<PHINode>(instruction)) {
            for (const BasicBlock *predecessor : predecessors(block)) {
                auto deciders = analysis.control.find(predecessor);
                if (deciders == analysis.control.end()) continue;
                for (const BasicBlock *decider : deciders->second)
                    add(decider->getTerminator());
            }
        }
    }
    return true;
}

// A copy of `original` that keeps only `cone`, with extra parameters after the original ones.
Function *make_slice(Function &original, ArrayRef<Type *> extra, const std::string &name, ValueToValueMapTy &map,
                     std::vector<Argument *> &extras) {
    Module &module = *original.getParent();
    std::vector<Type *> parameters;
    for (Argument &argument : original.args())
        parameters.push_back(argument.getType());
    for (Type *type : extra)
        parameters.push_back(type);
    FunctionType *type = FunctionType::get(Type::getVoidTy(module.getContext()), parameters, false);
    Function *slice = Function::Create(type, GlobalValue::InternalLinkage, name, &module);
    unsigned index = 0;
    for (Argument &argument : original.args()) {
        slice->getArg(index)->setName(argument.getName());
        map[&argument] = slice->getArg(index++);
    }
    for (unsigned i = 0; i < extra.size(); i++)
        extras.push_back(slice->getArg(index + i));
    SmallVector<ReturnInst *, 4> returns;
    CloneFunctionInto(slice, &original, map, CloneFunctionChangeType::LocalChangesOnly, returns);
    slice->setAttributes(AttributeList());
    slice->setSubprogram(nullptr);
    slice->addFnAttr(Attribute::NoUnwind);
    return slice;
}

// Deletes from the slice everything the cone does not keep. A branch that is not kept goes straight to where its
// paths meet again, so an unrelated loop or an unrelated if disappears.
void prune_slice(Function &slice, Function &original, const Cone &cone, ValueToValueMapTy &map, Analysis &analysis) {
    LLVMContext &context = slice.getContext();
    SmallVector<Instruction *, 64> dead;
    SmallVector<std::pair<BasicBlock *, BasicBlock *>, 16> redirects; // block, where its paths meet again
    for (BasicBlock &block : original) {
        auto *copy = cast<BasicBlock>(map[&block]);
        Instruction *terminator = block.getTerminator();
        if (cone.keep.count(terminator) != 0) continue;
        DomTreeNode *node = analysis.post.getNode(&block);
        DomTreeNode *meet = node == nullptr ? nullptr : node->getIDom();
        BasicBlock *target =
            meet == nullptr || meet->getBlock() == nullptr ? nullptr : cast<BasicBlock>(map[meet->getBlock()]);
        redirects.emplace_back(copy, target);
    }
    for (BasicBlock &block : original) {
        for (Instruction &instruction : block) {
            if (cone.keep.count(&instruction) != 0 || instruction.isTerminator()) continue;
            if (auto *copy = dyn_cast_or_null<Instruction>(map[&instruction])) dead.push_back(copy);
        }
    }
    for (Instruction *instruction : dead) {
        instruction->replaceAllUsesWith(PoisonValue::get(instruction->getType()));
        instruction->eraseFromParent();
    }
    for (auto &[block, target] : redirects) {
        Instruction *terminator = block->getTerminator();
        for (BasicBlock *successor : successors(block)) {
            if (successor != target) successor->removePredecessor(block);
        }
        terminator->eraseFromParent();
        if (target == nullptr) {
            ReturnInst::Create(context, block);
        } else {
            UncondBrInst::Create(target, block);
            for (PHINode &phi : target->phis()) {
                if (phi.getBasicBlockIndex(block) < 0) phi.addIncoming(PoisonValue::get(phi.getType()), block);
            }
        }
    }
    for (BasicBlock &block : slice) { // returns of the original that were kept as they were
        if (auto *ret = dyn_cast<ReturnInst>(block.getTerminator())) {
            if (ret->getNumOperands() != 0) {
                ReturnInst::Create(context, &block);
                ret->eraseFromParent();
            }
        }
    }
}

struct Slices {
    bool ok = false;
    std::string error;
    Function *requires_slice = nullptr;
    Function *old_slice = nullptr;
    Function *ensures_slice = nullptr;
    std::vector<Type *> old_types;
    bool has_result = false;
};

// The value that `ret` returns, when the smith_ensures can name it; sets `none` when there is nothing to name.
bool single_result(Function &function, Value *&result, std::string &error) {
    ReturnInst *only = nullptr;
    for (BasicBlock &block : function) {
        if (auto *ret = dyn_cast<ReturnInst>(block.getTerminator())) {
            if (only != nullptr) {
                error = "it returns from several places; end it with one return, after one smith_ensures";
                return false;
            }
            only = ret;
        }
    }
    result = only == nullptr ? nullptr : only->getReturnValue();
    return true;
}

void run_cleanup(Function &function) {
    Analyses analyses;
    std::string error;
    analyses.run_function(function, "simplifycfg,dce", error);
}

Slices build_slices(Function &callee) {
    Slices slices;
    Analysis analysis(callee);
    SmallVector<Instruction *, 4> requires_roots;
    SmallVector<Instruction *, 4> ensures_roots;
    for (Instruction &instruction : instructions(callee)) {
        if (owner_id(instruction) != function_id(callee)) continue;
        if (contract_of(&instruction) == Contract::Requires) requires_roots.push_back(&instruction);
        if (contract_of(&instruction) == Contract::Ensures) ensures_roots.push_back(&instruction);
    }
    std::string name = callee.getName().str();
    if (StringRef(name).ends_with(snapshot_suffix)) name.resize(name.size() - strlen(snapshot_suffix));
    if (!requires_roots.empty()) {
        Cone cone;
        if (!compute_cone(analysis, requires_roots, nullptr, false, cone, slices.error)) return slices;
        for (const Instruction *instruction : cone.keep) {
            auto *load = dyn_cast<LoadInst>(instruction);
            const Instruction *written = load != nullptr ? analysis.write_before(*load) : nullptr;
            if (written != nullptr) {
                slices.error = "its smith_requires reads memory at " + describe_instruction(load) +
                               " after the function has written, at " + describe_instruction(written) +
                               "; put every smith_requires before the first write, or read only parameters in it";
                return slices;
            }
        }
        ValueToValueMapTy map;
        std::vector<Argument *> extras;
        Function *slice = make_slice(callee, {}, name + ".requires", map, extras);
        prune_slice(*slice, callee, cone, map, analysis);
        run_cleanup(*slice);
        slices.requires_slice = slice;
    }
    if (!ensures_roots.empty()) {
        Value *result = nullptr;
        if (!single_result(callee, result, slices.error)) return slices;
        if (result != nullptr && (isa<Constant>(result) || isa<Argument>(result))) result = nullptr;
        Cone cone;
        if (!compute_cone(analysis, ensures_roots, result, true, cone, slices.error)) return slices;
        for (const Instruction *instruction : cone.keep) {
            auto *load = dyn_cast<LoadInst>(instruction);
            if (load == nullptr || instruction == result) continue;
            bool is_old = llvm::is_contained(cone.old_loads, load);
            const Instruction *later = is_old ? nullptr : analysis.write_after(*load);
            if (later != nullptr) {
                slices.error = "its smith_ensures reads memory at " + describe_instruction(load) +
                               " before a later write, at " + describe_instruction(later) +
                               "; put every smith_ensures just before the return, after the last write";
                return slices;
            }
        }
        // The old values: a slice that stores each into a slot of the caller.
        if (!cone.old_loads.empty()) {
            Cone old_cone;
            SmallVector<Instruction *, 4> loads(cone.old_loads.begin(), cone.old_loads.end());
            if (!compute_cone(analysis, loads, nullptr, false, old_cone, slices.error)) return slices;
            std::vector<Type *> slot_types(cone.old_loads.size(), PointerType::getUnqual(callee.getContext()));
            ValueToValueMapTy map;
            std::vector<Argument *> extras;
            Function *slice = make_slice(callee, slot_types, name + ".old", map, extras);
            for (unsigned i = 0; i < cone.old_loads.size(); i++) {
                auto *copy = cast<Instruction>(map[cone.old_loads[i]]);
                auto *store = new StoreInst(copy, extras[i], copy->getNextNode()->getIterator());
                store->setDebugLoc(copy->getDebugLoc());
                old_cone.keep.insert(cone.old_loads[i]);
                slices.old_types.push_back(cone.old_loads[i]->getType());
            }
            prune_slice(*slice, callee, old_cone, map, analysis);
            run_cleanup(*slice);
            slices.old_slice = slice;
        }
        std::vector<Type *> extra;
        if (result != nullptr) extra.push_back(result->getType());
        for (LoadInst *load : cone.old_loads)
            extra.push_back(load->getType());
        ValueToValueMapTy map;
        std::vector<Argument *> extras;
        Function *slice = make_slice(callee, extra, name + ".ensures", map, extras);
        unsigned next = 0;
        auto replace_with_parameter = [&](Value *original, Argument *parameter) {
            auto *clone = cast<Instruction>(map[original]);
            clone->replaceAllUsesWith(parameter);
            clone->eraseFromParent(); // the map follows the replacement, so prune_slice skips it
            next++;
        };
        if (result != nullptr) replace_with_parameter(result, extras[next]);
        for (LoadInst *load : cone.old_loads)
            replace_with_parameter(load, extras[next]);
        prune_slice(*slice, callee, cone, map, analysis);
        run_cleanup(*slice);
        slices.ensures_slice = slice;
        slices.has_result = result != nullptr;
    }
    slices.ok = true;
    return slices;
}

// ---- one function ----

struct UnitContext {
    Module &module;
    Function &function;
    Unit &unit;
    const std::set<std::string> &recursive; // the names of the functions that call themselves
    std::map<Function *, Slices> slices;
    std::unique_ptr<Unprovable> unprovable;
    unsigned havocs = 0; // the objects a call has been allowed to change
    // The copies of read-only parameters that calls are given (see pass_copies_of_readonly), one for each parameter and
    // size when nothing in the function writes the parameter's object.
    std::map<std::pair<const Value *, uint64_t>, AllocaInst *> shared_copies;

    void fail(const Instruction *at, std::string what, std::string fix) {
        auto failure = std::make_unique<Unprovable>();
        if (at != nullptr && at->getDebugLoc()) {
            failure->file = at->getDebugLoc()->getFilename().str();
            failure->line = at->getDebugLoc().getLine();
        }
        failure->what = std::move(what);
        failure->fix = std::move(fix);
        unprovable = std::move(failure);
    }
};

// Library functions Alive2 knows the meaning of.
bool known_library_function(StringRef name) {
    static const char *const names[] = {"memcpy", "memmove", "memset", "memcmp", "bcmp",
                                        "strlen", "abs",     "labs",   "llabs"};
    for (const char *known : names) {
        if (name == known) return true;
    }
    return false;
}

std::string source_name(const Function &function) {
    if (const DISubprogram *subprogram = function.getSubprogram()) return subprogram->getName().str();
    return function.getName().str();
}

// The function whose body wrote the call, as the source names it: the one a trusted marker would go in.
std::string writer_of(const UnitContext &context, const Instruction *call) {
    if (call != nullptr && call->getDebugLoc()) {
        if (const DISubprogram *subprogram = call->getDebugLoc()->getScope()->getSubprogram())
            return subprogram->getName().str();
    }
    return source_name(context.function);
}

// What to do about a call the prover cannot read: the program's edge is a trusted function, whose contract the callers
// use. `alternative` is the other way out, when there is one.
std::string boundary_fix(const UnitContext &context, const Instruction *call, const std::string &alternative) {
    std::string who = writer_of(context, call);
    std::string fix = "this is the edge of the program: put smith_trusted() as the first statement of " + who +
                      ", so that its callers use its smith_requires and smith_ensures and its body is not proved";
    if (!alternative.empty()) fix += "; or " + alternative;
    return fix;
}

bool inline_slice(UnitContext &context, CallBase &call, int call_site) {
    Function &function = context.function;
    DenseSet<const Instruction *> before;
    for (const Instruction &instruction : instructions(function))
        before.insert(&instruction);
    InlineFunctionInfo info;
    InlineResult result = InlineFunction(call, info);
    if (!result.isSuccess()) {
        context.fail(&call, "cannot use a contract: " + std::string(result.getFailureReason()), "");
        return false;
    }
    if (call_site >= 0) {
        for (Instruction &instruction : instructions(function)) {
            if (before.count(&instruction) == 0 && contract_of(&instruction) != Contract::None)
                instruction.setMetadata("smith.callsite", id_node(function.getContext(), call_site));
        }
    }
    return true;
}

// Copies `bytes` from `source` to `destination` with loads and stores of the widest integer the alignment allows,
// inserted before `at`. (llvm.memcpy of a whole object becomes a lambda over the memory array in Alive2's encoding,
// which Bitwuzla cannot read.) Returns the last store.
Instruction *copy_object(Instruction *at, const DebugLoc &location, Value *destination, Value *source, uint64_t bytes,
                         Align align) {
    IRBuilder<> builder(at);
    builder.SetCurrentDebugLocation(location);
    unsigned widest = align.value() >= 8 ? 8 : static_cast<unsigned>(align.value());
    Instruction *last = nullptr;
    for (uint64_t offset = 0; offset < bytes;) {
        unsigned size = widest;
        while (size > 1 && (offset + size > bytes || offset % size != 0))
            size /= 2;
        Type *type = builder.getIntNTy(size * 8);
        Align here = commonAlignment(align, offset);
        Value *from = builder.CreateConstInBoundsGEP1_64(builder.getInt8Ty(), source, offset);
        Value *to = builder.CreateConstInBoundsGEP1_64(builder.getInt8Ty(), destination, offset);
        last = builder.CreateAlignedStore(builder.CreateAlignedLoad(type, from, here), to, here);
        offset += size;
    }
    return last;
}

// Alive2 does not change a local object (an alloca of the function) when a call writes through a pointer to it: its
// model of a call havocs the memory of other blocks and leaves a TODO for locals. A caller would then know what the
// callee never promised (`first == 9` after a call that overwrote it). So after the call each local object it was given
// is overwritten by the bytes of a global of its own that nothing initializes: arbitrary, and different at every call,
// before the callee's contract is assumed of it. Returns the last instruction it inserted (`call` when none).
Instruction *havoc_locals(UnitContext &context, CallBase &call, Function &callee, Instruction *last,
                          const SmallPtrSetImpl<const Value *> &readonly) {
    MemoryEffects effects = callee.getMemoryEffects();
    if (!isModSet(effects.getModRef(IRMemLocation::ArgMem)) && !isModSet(effects.getModRef(IRMemLocation::Other)))
        return last;
    const DataLayout &layout = context.module.getDataLayout();
    SmallPtrSet<const AllocaInst *, 4> done;
    for (Value *argument : call.args()) {
        if (!argument->getType()->isPointerTy() || readonly.contains(argument)) continue;
        auto *object = dyn_cast<AllocaInst>(getUnderlyingObject(argument));
        if (object == nullptr || !done.insert(object).second) continue;
        std::optional<TypeSize> size = object->getAllocationSize(layout);
        if (!size || size->isScalable() || size->getFixedValue() == 0) continue;
        LLVMContext &llvm_context = context.module.getContext();
        auto *type = ArrayType::get(Type::getInt8Ty(llvm_context), size->getFixedValue());
        auto *source =
            new GlobalVariable(context.module, type, false, GlobalValue::InternalLinkage,
                               ConstantAggregateZero::get(type), "smith.havoc." + std::to_string(context.havocs++));
        source->setAlignment(object->getAlign());
        object->setMetadata("smith.havoced",
                            MDNode::get(llvm_context, {}));          // what it holds is values, as a parameter's
        context.function.setMemoryEffects(MemoryEffects::unknown()); // it now reads a global, whatever it inferred
        last = copy_object(last->getNextNode(), call.getDebugLoc(), object, source, size->getFixedValue(),
                           object->getAlign());
    }
    return last;
}

// A pointer the callee takes as `readonly` (a `const T *`) is not written by the call. Alive2 is told only that the
// call may write through its pointers (a `readonly` call argument it takes as the call's promise, and reports a call
// that might write it as undefined behaviour), so the attribute is dropped from the call and the promise kept by hand:
// the call is given a copy of the object, made before it, and everything else (the contract's slices, the code after
// the call) keeps reading the object itself, which no call can have changed.
// Whether every use of a pointer reads: a load, or a pointer passed on to a parameter the callee only reads.
bool only_read(const Value *root) {
    SmallVector<const Value *, 8> work = {root};
    SmallPtrSet<const Value *, 8> visited;
    while (!work.empty()) {
        const Value *value = work.pop_back_val();
        if (!visited.insert(value).second) continue;
        for (const Use &use : value->uses()) {
            const User *user = use.getUser();
            if (isa<LoadInst>(user)) continue;
            if (isa<GetElementPtrInst>(user) || isa<BitCastInst>(user) || isa<AddrSpaceCastInst>(user)) {
                work.push_back(user);
                continue;
            }
            auto *call = dyn_cast<CallBase>(user);
            if (call == nullptr) return false;
            Function *callee = call->getCalledFunction();
            if (callee == nullptr) return false;
            if (callee->isIntrinsic()) {
                Intrinsic::ID id = callee->getIntrinsicID();
                if (id == Intrinsic::lifetime_start || id == Intrinsic::lifetime_end || id == Intrinsic::assume)
                    continue;
                return false;
            }
            unsigned index = call->getArgOperandNo(&use);
            if (callee->hasParamAttribute(index, Attribute::ReadOnly) ||
                callee->hasParamAttribute(index, Attribute::ReadNone))
                continue;
            return false;
        }
    }
    return true;
}

void pass_copies_of_readonly(UnitContext &context, CallBase &call, Function &callee, IRBuilder<> &entry,
                             SmallPtrSetImpl<const Value *> &copies) {
    Function &function = context.function;
    MemoryEffects effects = callee.getMemoryEffects();
    if (!isModSet(effects.getModRef(IRMemLocation::ArgMem)) && !isModSet(effects.getModRef(IRMemLocation::Other)))
        return;
    for (unsigned i = 0; i < call.arg_size() && i < callee.arg_size(); i++) {
        Value *pointer = call.getArgOperand(i);
        if (!pointer->getType()->isPointerTy()) continue;
        if (!callee.hasParamAttribute(i, Attribute::ReadOnly) && !callee.hasParamAttribute(i, Attribute::ReadNone))
            continue;
        uint64_t bytes = callee.getParamDereferenceableBytes(i);
        if (bytes == 0) continue;
        Align align = callee.getParamAlign(i).valueOrOne();
        // A parameter of the function that nothing writes is the same object at every call: one copy, made at the
        // start, serves all of them. A function that calls a callee 64 times with the same board would otherwise copy
        // the board 64 times, and every copy is loads and stores for the solver.
        bool shared = isa<Argument>(pointer) && only_read(pointer);
        AllocaInst *copy = nullptr;
        auto found = shared ? context.shared_copies.find({pointer, bytes}) : context.shared_copies.end();
        if (found != context.shared_copies.end()) {
            copy = found->second;
        } else {
            copy = entry.CreateAlloca(ArrayType::get(Type::getInt8Ty(function.getContext()), bytes));
            copy->setAlignment(align);
            if (shared) {
                copy_object(&*entry.GetInsertPoint(), call.getDebugLoc(), copy, pointer, bytes, align);
                context.shared_copies[{pointer, bytes}] = copy;
            } else {
                copy_object(&call, call.getDebugLoc(), copy, pointer, bytes, align);
            }
        }
        call.setArgOperand(i, copy);
        copies.insert(copy);
    }
}

// Replaces a call of `callee` by its contract: the slices inlined around the call.
bool apply_contract(UnitContext &context, CallBase &call, Function &callee) {
    auto cached = context.slices.find(&callee);
    if (cached == context.slices.end()) {
        Function *snapshot = context.module.getFunction(callee.getName().str() + snapshot_suffix);
        cached = context.slices.emplace(&callee, build_slices(snapshot != nullptr ? *snapshot : callee)).first;
    }
    Slices &slices = cached->second;
    if (!slices.ok) {
        context.fail(&call, "cannot use the contract of " + source_name(callee) + ": " + slices.error, "");
        return false;
    }
    Function &function = context.function;
    int call_site = static_cast<int>(context.unit.call_sites.size());
    CallSite site;
    site.callee = source_name(callee);
    if (call.getDebugLoc()) {
        site.file = call.getDebugLoc()->getFilename().str();
        site.line = call.getDebugLoc().getLine();
    }
    SmallVector<Value *, 8> arguments(call.args());
    DISubprogram *subprogram = callee.getSubprogram();
    for (unsigned i = 0; i < arguments.size(); i++) {
        site.arguments.push_back(arguments[i]);
        site.parameters.push_back(callee.getArg(i)->getName().str());
    }
    (void)subprogram;
    context.unit.call_sites.push_back(site);
    MemoryEffects effects = callee.getMemoryEffects();
    if (isModSet(effects.getModRef(IRMemLocation::Other))) {
        context.unit.notes.push_back("note: " + site.file + ":" + std::to_string(site.line) + ": " +
                                     source_name(callee) +
                                     " may write memory it was not given (LLVM infers it from the body); what the "
                                     "caller knows about that memory is "
                                     "lost across the call");
    }
    IRBuilder<> entry(&*function.getEntryBlock().getFirstInsertionPt());
    SmallPtrSet<const Value *, 2> copies; // what the call is given in place of the objects it may not write
    pass_copies_of_readonly(context, call, callee, entry, copies);
    SmallVector<Value *, 4> slots;
    for (Type *type : slices.old_types)
        slots.push_back(entry.CreateAlloca(type));
    SmallVector<Value *, 4> olds;
    if (slices.old_slice != nullptr) {
        SmallVector<Value *, 8> with_slots(arguments.begin(), arguments.end());
        with_slots.append(slots.begin(), slots.end());
        auto *old_call = CallInst::Create(slices.old_slice, with_slots, "", call.getIterator());
        old_call->setDebugLoc(call.getDebugLoc());
        if (!inline_slice(context, *old_call, -1)) return false;
        for (unsigned i = 0; i < slots.size(); i++) {
            auto *load = new LoadInst(slices.old_types[i], slots[i], "old", call.getIterator());
            load->setDebugLoc(call.getDebugLoc());
            olds.push_back(load);
        }
    }
    if (slices.requires_slice != nullptr) {
        auto *requires_call = CallInst::Create(slices.requires_slice, arguments, "", call.getIterator());
        requires_call->setDebugLoc(call.getDebugLoc());
        if (!inline_slice(context, *requires_call, call_site)) return false;
    }
    if (slices.ensures_slice != nullptr) {
        SmallVector<Value *, 8> with_result(arguments.begin(), arguments.end());
        if (slices.has_result) with_result.push_back(&call);
        with_result.append(olds.begin(), olds.end());
        Instruction *after = havoc_locals(context, call, callee, &call, copies);
        auto *ensures_call =
            CallInst::Create(slices.ensures_slice, with_result, "", after->getNextNode()->getIterator());
        ensures_call->setDebugLoc(call.getDebugLoc());
        if (!inline_slice(context, *ensures_call, call_site)) return false;
    }
    call.setMetadata("smith.resolved", MDNode::get(function.getContext(), {}));
    return true;
}

// Every call in the function is inlined, replaced by its contract, or an Unprovable.
bool resolve_calls(UnitContext &context) {
    Function &function = context.function;
    for (;;) {
        CallBase *target = nullptr;
        for (Instruction &instruction : instructions(function)) {
            auto *call = dyn_cast<CallBase>(&instruction);
            if (call == nullptr || call->getMetadata("smith.resolved") != nullptr) continue;
            target = call;
            Function *callee = direct_callee(call);
            if (call->isInlineAsm()) {
                context.fail(call, "inline assembly cannot be proved in " + writer_of(context, call),
                             boundary_fix(context, call,
                                          "move it into a function of its own and mark that one trusted, with a "
                                          "smith_ensures describing what it gives"));
                return false;
            }
            if (callee == nullptr) {
                context.fail(call,
                             "a call through a function pointer in " + writer_of(context, call) +
                                 ": the target is not known",
                             boundary_fix(context, call, "call the function directly"));
                return false;
            }
            StringRef name = callee->getName();
            if (name.starts_with("objc_msgSend")) {
                context.fail(call,
                             "an Objective-C message send in " + writer_of(context, call) +
                                 ": the method it reaches is not known",
                             boundary_fix(context, call, "call a plain C function"));
                return false;
            }
            if (callee->isIntrinsic() || contract_of(call) != Contract::None || is_assert_failure(call) ||
                is_abort(call) || name == "smith_point") {
                call->setMetadata("smith.resolved", MDNode::get(function.getContext(), {}));
                target = nullptr;
                continue;
            }
            if (callee->isVarArg() && !callee->isDeclaration()) {
                context.fail(call, "a call of " + source_name(*callee) + ", which takes a variable argument list",
                             "call a function with fixed parameters; a variable argument list is not proved");
                return false;
            }
            if (!callee->isDeclaration()) {
                if (callee == &function) {
                    context.fail(call, "recursion: " + source_name(function) + " calls itself",
                                 "write it as a loop with a constant bound; a recursive function is not proved");
                    return false;
                }
                if (context.recursive.count(callee->getName().str()) != 0 && !is_modular(*callee)) {
                    context.fail(call,
                                 "recursion: " + source_name(*callee) +
                                     " calls itself, directly or through other functions",
                                 "write it as a loop with a constant bound; a recursive function is not proved");
                    return false;
                }
                break;
            }
            if (known_library_function(name) || (callee->isDeclaration() && callee->hasFnAttribute("smith.edge"))) {
                call->setMetadata("smith.resolved", MDNode::get(function.getContext(), {}));
                target = nullptr;
                continue;
            }
            context.fail(
                call, "a call of " + name.str() + ", which has neither a body nor a contract",
                boundary_fix(context, call,
                             "define " + name.str() + " in a file given to smith prove, or give it a contract"));
            return false;
        }
        if (target == nullptr) return true;
        Function *callee = direct_callee(target);
        if (is_modular(*callee)) {
            if (!apply_contract(context, *target, *callee)) return false;
        } else {
            InlineFunctionInfo info;
            InlineResult result = InlineFunction(*target, info);
            if (!result.isSuccess()) {
                context.fail(target,
                             "cannot inline " + source_name(*callee) + ": " + std::string(result.getFailureReason()),
                             "");
                return false;
            }
        }
    }
}

// The proof points. A contract written in this function is a promise (smith_ensures) or a premise (smith_requires); one
// that came from a callee is the other way round.
class Normalizer {
  public:
    explicit Normalizer(UnitContext &context) : context(context), module(context.module) {
        point_function = module.getOrInsertFunction(
            "smith_point",
            FunctionType::get(Type::getVoidTy(module.getContext()),
                              {Type::getInt32Ty(module.getContext()), Type::getInt1Ty(module.getContext())}, false));
    }

    bool run() {
        std::vector<Instruction *> calls;
        for (Instruction &instruction : instructions(context.function)) {
            if (isa<CallBase>(&instruction)) calls.push_back(&instruction);
        }
        for (Instruction *instruction : calls) {
            Contract kind = contract_of(instruction);
            bool own = kind != Contract::None && owner_id(*instruction) == function_id(context.function);
            if ((kind == Contract::Requires && own) || (kind == Contract::Ensures && !own)) {
                point(instruction, Kind::Premise, instruction->getOperand(0), true);
            } else if (kind != Contract::None) {
                Kind point_kind = kind == Contract::Requires  ? Kind::Requires
                                  : kind == Contract::Ensures ? Kind::Ensures
                                  : kind == Contract::Assert  ? Kind::Assert
                                                              : Kind::Invariant;
                point(instruction, point_kind, instruction->getOperand(0), true);
            } else if (is_trap(instruction)) {
                point(instruction, Kind::Trap, nullptr, false);
            } else if (is_assert_failure(instruction)) {
                point(instruction, Kind::Assert, nullptr, false);
            } else if (is_abort(instruction)) {
                point(instruction, Kind::Abort, nullptr, false);
            }
        }
        // Inlining a function with a `restrict` parameter leaves scopes of what it does not alias; Alive2 takes no
        // metadata argument, and the scopes only say more than the prover assumes.
        SmallVector<Instruction *, 4> scopes;
        for (Instruction &instruction : instructions(context.function)) {
            if (auto *intrinsic = dyn_cast<IntrinsicInst>(&instruction);
                intrinsic != nullptr && intrinsic->getIntrinsicID() == Intrinsic::experimental_noalias_scope_decl)
                scopes.push_back(intrinsic);
            instruction.setMetadata(LLVMContext::MD_alias_scope, nullptr);
            instruction.setMetadata(LLVMContext::MD_noalias, nullptr);
        }
        for (Instruction *scope : scopes)
            scope->eraseFromParent();
        for (Instruction &instruction : instructions(context.function)) { // the bookkeeping is not for Alive2
            instruction.setMetadata("smith.resolved", nullptr);
            instruction.setMetadata("smith.callsite", nullptr);
            instruction.setMetadata("smith.owner", nullptr);
        }
        return true;
    }

  private:
    UnitContext &context;
    Module &module;
    FunctionCallee point_function;

    // The condition as the source writes it: the argument of the contract on its line.
    static std::string source_line(const std::string &file, unsigned line) {
        std::ifstream input(file);
        std::string text;
        for (unsigned number = 0; number < line && std::getline(input, text);)
            number++;
        size_t open = text.find('(');
        size_t close = text.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) return text;
        return text.substr(open + 1, close - open - 1);
    }

    static std::string string_of(const Value *value) {
        StringRef text;
        if (value != nullptr && getConstantStringInfo(value, text)) return text.str();
        return "";
    }

    void point(Instruction *call, Kind kind, Value *condition, bool assume_after) {
        auto &context_points = context.unit.points;
        Point info;
        info.id = static_cast<unsigned>(context_points.size());
        info.kind = kind;
        auto *base = cast<CallBase>(call);
        if (kind == Kind::Requires || kind == Kind::Ensures || kind == Kind::Invariant || kind == Kind::Premise) {
            if (MDNode *site = call->getMetadata("smith.callsite")) info.call_site = static_cast<int>(id_of(site));
        } else if (kind == Kind::Assert && condition != nullptr) {
            // smith_assert(condition) of <smith/contract.h>: the text is the line's, below
        } else if (kind == Kind::Assert) {
            // __assert_rtn(function, file, line, expression) on macOS, __assert_fail(expression, file, line, function)
            bool apple = base->getCalledFunction()->getName() == "__assert_rtn";
            info.text = string_of(base->getArgOperand(apple ? 3 : 0));
            info.file = string_of(base->getArgOperand(1));
            if (auto *line = dyn_cast<ConstantInt>(base->getArgOperand(2)))
                info.line = static_cast<unsigned>(line->getZExtValue());
        } else if (kind == Kind::Trap) {
            auto *number = cast<ConstantInt>(base->getArgOperand(0));
            uint64_t index = number->getZExtValue();
            info.text =
                index < sizeof trap_messages / sizeof trap_messages[0] ? trap_messages[index] : "undefined behaviour";
        } else {
            info.text = base->getCalledFunction()->getName() == "abort" ? "abort() is called" : "the program traps";
        }
        if (call->getDebugLoc()) {
            const DILocation *location = call->getDebugLoc().get();
            // clang gives the trap of a UBSan check a made-up function that names the check, inlined at the source
            // line.
            StringRef scope = location->getScope()->getSubprogram()->getName();
            if (kind != Kind::Trap &&
                (scope.starts_with("smith_") || location->getFilename().ends_with("smith/contract.h")) &&
                location->getInlinedAt() != nullptr) {
                location = location->getInlinedAt(); // the contract function inlined at the line that wrote it
            } else if (scope.starts_with("__clang_trap_msg") && location->getInlinedAt() != nullptr) {
                SmallVector<StringRef, 3> parts;
                scope.split(parts, '$');
                if (kind == Kind::Trap && parts.size() == 3) info.text = parts[2].str();
                location = location->getInlinedAt();
            }
            if (info.file.empty()) info.file = location->getFilename().str();
            if (info.line == 0) info.line = location->getLine();
            for (const DILocation *outer = location->getInlinedAt(); outer != nullptr; outer = outer->getInlinedAt()) {
                if (outer->getScope()->getSubprogram() == context.function.getSubprogram()) continue; // this function
                info.inlined.push_back(outer->getScope()->getSubprogram()->getName().str() + " at " +
                                       outer->getFilename().str() + ":" + std::to_string(outer->getLine()));
            }
        }
        if (info.kind == Kind::Requires || info.kind == Kind::Ensures || info.kind == Kind::Invariant ||
            info.kind == Kind::Premise || (info.kind == Kind::Assert && condition != nullptr))
            info.text = source_line(info.file, info.line);
        IRBuilder<> builder(call);
        Value *value = condition != nullptr ? condition : builder.getFalse();
        auto *marker = builder.CreateCall(point_function, {builder.getInt32(info.id), value});
        marker->setDebugLoc(call->getDebugLoc());
        info.assume = assume_after && condition != nullptr;
        info.where = marker;
        context_points.push_back(info);
        call->eraseFromParent();
    }
};

// Attributes LLVM 23 infers that the Alive2 this was built with does not know. Dropping nofree only loses what it said.
void drop_unsupported_attributes(Function &function) {
    // `readnone` on a pointer a callee never touches (inferred too) makes Alive2 report the call as undefined behaviour
    // when the caller's pointer is `readonly`, an access it does not make.
    // `readonly` goes too (the promise is kept by hand where a contract is used, see pass_copies_of_readonly), and
    // `initializes`, which LLVM infers from a body, makes Alive2 check that the function stores those bytes and report
    // a load of what a call left there as undefined behaviour; what the bytes hold after a call is the callee's
    // contract.
    for (unsigned i = 0; i < function.arg_size(); i++) {
        function.removeParamAttr(i, Attribute::NoFree);
        function.removeParamAttr(i, Attribute::Initializes);
    }
    for (Function &callee : *function.getParent()) {
        for (unsigned i = 0; i < callee.arg_size(); i++) {
            callee.removeParamAttr(i, Attribute::NoFree);
            callee.removeParamAttr(i, Attribute::Initializes);
            callee.removeParamAttr(i, Attribute::ReadNone);
            callee.removeParamAttr(i, Attribute::ReadOnly);
        }
    }
    for (Instruction &instruction : instructions(function)) {
        auto *call = dyn_cast<CallBase>(&instruction);
        if (call == nullptr) continue;
        for (unsigned i = 0; i < call->arg_size(); i++) {
            call->removeParamAttr(i, Attribute::NoFree);
            call->removeParamAttr(i, Attribute::Initializes);
            call->removeParamAttr(i, Attribute::ReadNone);
            call->removeParamAttr(i, Attribute::ReadOnly);
        }
    }
}

// LLVM deletes a loop it can solve (n counted down to 0) and the proof would then say nothing of it, so each loop gets
// a call the optimizer cannot remove: when unrolling is done, a loop that is left is a loop with no constant bound.
// The name a variable has in the source: a parameter, or what the debug info calls the value.
std::string source_name_of(Value *value) {
    if (auto *argument = dyn_cast<Argument>(value); argument != nullptr && argument->hasName())
        return argument->getName().str();
    SmallVector<DbgVariableRecord *, 4> records;
    findDbgUsers(value, records);
    for (DbgVariableRecord *record : records) {
        if (DILocalVariable *variable = record->getVariable(); variable != nullptr && !variable->getName().empty())
            return variable->getName().str();
    }
    return "";
}

// The variables a loop's trip count depends on, named as in the source. With a count LLVM can compute, the values its
// expression is built from; without one (a loop that divides its way to zero), the values that its exit tests read,
// followed back through the loop's own arithmetic.
void name_loop_controls(Loop *loop, ScalarEvolution &evolution, UnrolledLoop &info) {
    auto add = [&](Value *value) {
        std::string name = source_name_of(value);
        if (name.empty()) return;
        for (const std::string &known : info.controls)
            if (known == name) return;
        info.controls.push_back(name);
    };
    const SCEV *count = evolution.getBackedgeTakenCount(loop);
    if (!isa<SCEVCouldNotCompute>(count)) {
        info.counted = true;
        struct Collect {
            decltype(add) &note;
            bool follow(const SCEV *expression) {
                if (auto *unknown = dyn_cast<SCEVUnknown>(expression)) note(unknown->getValue());
                return true;
            }
            bool isDone() {
                return false;
            }
        } collect{add};
        visitAll(count, collect);
        return;
    }
    for (BasicBlock *block : loop->blocks()) { // a value divided or shifted down to zero takes a known number of passes
        for (Instruction &instruction : *block) {
            auto *operation = dyn_cast<BinaryOperator>(&instruction);
            if (operation == nullptr || info.shrink_passes != 0) continue;
            auto *by = dyn_cast<ConstantInt>(operation->getOperand(1));
            if (by == nullptr || by->getValue().ule(1)) continue;
            unsigned bits = operation->getType()->getIntegerBitWidth();
            bool shift = operation->getOpcode() == Instruction::LShr || operation->getOpcode() == Instruction::AShr;
            bool divide = operation->getOpcode() == Instruction::UDiv || operation->getOpcode() == Instruction::SDiv;
            if (!shift && !divide) continue;
            double step =
                shift ? static_cast<double>(by->getZExtValue()) : std::log2(static_cast<double>(by->getZExtValue()));
            if (shift && by->getZExtValue() >= bits) continue;
            info.shrink_passes = static_cast<unsigned>(std::ceil(bits / step));
            info.shrink_what = "a " + std::to_string(bits) + "-bit value " +
                               (shift ? "shifted right by " : "divided by ") + std::to_string(by->getZExtValue());
        }
    }
    SmallVector<BasicBlock *, 4> exiting;
    loop->getExitingBlocks(exiting);
    SmallPtrSet<Value *, 16> seen;
    SmallVector<std::pair<Value *, unsigned>, 16> work;
    for (BasicBlock *block : exiting) {
        if (auto *branch = dyn_cast<CondBrInst>(block->getTerminator())) work.push_back({branch->getCondition(), 0});
    }
    while (!work.empty()) {
        auto [value, depth] = work.pop_back_val();
        if (!seen.insert(value).second) continue;
        auto *instruction = dyn_cast<Instruction>(value);
        if (!isa<ICmpInst>(value)) add(value);
        if (instruction == nullptr || depth >= 6 || !loop->contains(instruction)) continue;
        for (Value *operand : instruction->operands())
            if (!isa<Constant>(operand) && !isa<BasicBlock>(operand)) work.push_back({operand, depth + 1});
    }
}

void mark_loops(Analyses &analyses, Function &function, Unit &unit) {
    Module &module = *function.getParent();
    FunctionCallee marker = module.getOrInsertFunction("smith_loop", Type::getVoidTy(module.getContext()),
                                                       Type::getInt32Ty(module.getContext()));
    cast<Function>(marker.getCallee())->setMemoryEffects(MemoryEffects::inaccessibleMemOnly());
    auto &loops = analyses.functions.getResult<LoopAnalysis>(function);
    auto &evolution = analyses.functions.getResult<ScalarEvolutionAnalysis>(function);
    DenseMap<const Loop *, unsigned> passes; // the passes of a loop in all, its outer loops' passes counted
    for (Loop *loop : loops.getLoopsInPreorder()) {
        BasicBlock *header = loop->getHeader();
        CallInst::Create(marker, {ConstantInt::get(Type::getInt32Ty(module.getContext()), unit.loops.size())}, "",
                         header->getFirstInsertionPt());
        UnrolledLoop info;
        name_loop_controls(loop, evolution, info);
        if (DebugLoc location = loop->getStartLoc()) {
            info.file = location->getFilename().str();
            info.line = location.getLine();
        }
        // A loop with a check that traps in its body has several exits; the exact count is then not asked, and the
        // largest the counted exit allows is the number of passes.
        unsigned own = evolution.getSmallConstantTripCount(loop);
        if (own == 0) own = evolution.getSmallConstantMaxTripCount(loop);
        // The count is how often the loop's test runs when the test is at the top (a C `for`): its body runs once less.
        if (own > 0 && loop->isLoopExiting(loop->getHeader())) own--;
        const Loop *outer = loop->getParentLoop();
        unsigned outside = outer == nullptr ? 1 : passes.lookup(outer);
        info.passes = own == 0 || outside == 0 ? 0 : own * outside;
        passes[loop] = info.passes;
        unit.loops.push_back(info);
    }
    analyses.functions.invalidate(function, PreservedAnalyses::none());
}

void remove_loop_marks(Function &function) {
    std::vector<Instruction *> marks;
    for (Instruction &instruction : instructions(function)) {
        auto *call = dyn_cast<CallBase>(&instruction);
        if (call != nullptr && call->getCalledFunction() != nullptr &&
            call->getCalledFunction()->getName() == "smith_loop")
            marks.push_back(&instruction);
    }
    for (Instruction *mark : marks)
        mark->eraseFromParent();
}

// What to say of a loop the prover could not bound. A loop LLVM can count is bounded by a `requires` on the variables
// of its count; one it cannot (it divides its way to zero) needs a counter that a constant bounds.
void describe_unbounded_loop(const Unit &unit, int index, Unprovable &failure) {
    const UnrolledLoop *info =
        index >= 0 && static_cast<size_t>(index) < unit.loops.size() ? &unit.loops[index] : nullptr;
    if (info != nullptr && failure.line == 0) {
        failure.file = info->file;
        failure.line = info->line;
    }
    std::string names;
    if (info != nullptr) {
        for (const std::string &name : info->controls)
            names += (names.empty() ? "`" : ", `") + name + "`";
    }
    if (info != nullptr && info->counted && !info->controls.empty()) {
        failure.what = "the loop has no bound the prover can see: it runs " + names + " times";
        failure.fix = "state its worst case, contract::requires(" + info->controls.front() +
                      " <= ...), which callers then prove (a loop's count comes from a constant or from a requires "
                      "that bounds it)";
    } else if (info != nullptr && info->shrink_passes != 0) {
        failure.what = "the loop has no bound the prover can see: it ends when " +
                       (info->controls.empty() ? std::string("a value") : names) + " reaches zero, and " +
                       info->shrink_what + " each pass is not a count LLVM can derive, so no requires gives one";
        failure.fix = "count the passes in a variable that a constant bounds: at most " +
                      std::to_string(info->shrink_passes) + " (a loop `for (usize pass = 0; pass < " +
                      std::to_string(info->shrink_passes) + "; pass++)` that breaks when the value is zero)";
    } else if (info != nullptr && !info->controls.empty()) {
        failure.what = "the loop has no bound the prover can see: its exit depends on " + names;
        failure.fix = "state the worst case, contract::requires(" + info->controls.front() +
                      " <= ...), which callers then prove; if the loop still fails, its exit is not a step by a "
                      "constant, so count the passes in a variable that a constant bounds";
    } else {
        failure.what = "the loop has no bound the prover can see";
        failure.fix = "give it a count: a constant (for (int i = 0; i < 8; i++)) or a variable that "
                      "contract::requires(count <= ...) bounds";
    }
}

// The function's own smith_requires are premises of its proof, so a loop's worst case may come from one:
// `smith_requires(count <= 5)` before `for (i = 0; i < count; i++)` bounds the loop at 5 passes. The unroller reads a
// parameter's `range` attribute (SCEV reads an llvm.assume only for a value computed after it, never for a parameter),
// so each premise of the form `parameter <op> constant`, alone or joined by `&&`, gives the parameter that range for
// the unrolling. The attribute is removed before the proof, where the premises are assumed anyway, so what is proved
// is the function as it was, unrolled.
void collect_ranges(Value *condition, std::map<Argument *, ConstantRange> &ranges) {
    if (auto *compare = dyn_cast<ICmpInst>(condition)) {
        auto *argument = dyn_cast<Argument>(compare->getOperand(0));
        auto *constant = dyn_cast<ConstantInt>(compare->getOperand(1));
        if (argument == nullptr || constant == nullptr) return;
        ConstantRange range = ConstantRange::makeExactICmpRegion(compare->getPredicate(), constant->getValue());
        auto found = ranges.find(argument);
        if (found == ranges.end())
            ranges.emplace(argument, range);
        else
            found->second = found->second.intersectWith(range);
    } else if (auto *both = dyn_cast<BinaryOperator>(condition);
               both != nullptr && both->getOpcode() == Instruction::And) {
        collect_ranges(both->getOperand(0), ranges);
        collect_ranges(both->getOperand(1), ranges);
    } else if (auto *choice = dyn_cast<SelectInst>(condition);
               choice != nullptr && match(choice->getFalseValue(), PatternMatch::m_Zero())) {
        collect_ranges(choice->getCondition(), ranges);
        collect_ranges(choice->getTrueValue(), ranges);
    }
}

std::vector<Argument *> assume_requires(Function &function) {
    std::vector<Argument *> bounded;
    std::map<Argument *, ConstantRange> ranges;
    for (Instruction &instruction : instructions(function)) {
        if (contract_of(&instruction) == Contract::Requires && owner_id(instruction) == function_id(function))
            collect_ranges(cast<CallBase>(&instruction)->getArgOperand(0), ranges);
    }
    for (auto &[argument, range] : ranges) {
        if (argument->hasAttribute(Attribute::Range) || range.isFullSet() || range.isEmptySet()) continue;
        argument->addAttr(Attribute::get(function.getContext(), Attribute::Range, range));
        bounded.push_back(argument);
    }
    return bounded;
}

void remove_loop_ranges(const std::vector<Argument *> &bounded) {
    for (Argument *argument : bounded)
        argument->removeAttr(Attribute::Range);
}

// A loop that is still there after unrolling: where it is (a source line, if there is one), and which of the unit's
// loops it is (the argument of its mark), or -1. It is the innermost one: an outer loop stays only while an inner does.
bool find_loop(Function &function, std::string &where, int &index) {
    DominatorTree dominators(function);
    LoopInfo loops(dominators);
    if (loops.empty()) return false;
    Loop *loop = *loops.begin();
    while (!loop->getSubLoops().empty())
        loop = loop->getSubLoops().front();
    index = -1;
    for (const Instruction &instruction : *loop->getHeader()) {
        auto *call = dyn_cast<CallBase>(&instruction);
        if (call != nullptr && call->getCalledFunction() != nullptr &&
            call->getCalledFunction()->getName() == "smith_loop")
            index = static_cast<int>(cast<ConstantInt>(call->getArgOperand(0))->getZExtValue());
    }
    const Instruction *found = loop->getHeader()->getTerminator();
    for (const BasicBlock *block : loop->blocks()) {
        for (const Instruction &instruction : *block) {
            if (instruction.getDebugLoc()) {
                found = &instruction;
                break;
            }
        }
        if (found->getDebugLoc()) break;
    }
    where = location_of(found);
    return true;
}

// ---- what a proof costs the solver ----

unsigned type_bits(const Value *value) {
    return value->getType()->isIntegerTy() ? value->getType()->getIntegerBitWidth() : 0;
}

std::pair<std::string, unsigned> source_line_of(const Instruction *instruction) {
    for (const Instruction *at = instruction; at != nullptr; at = at->getPrevNode()) {
        if (at->getDebugLoc()) return {at->getDebugLoc()->getFilename().str(), at->getDebugLoc().getLine()};
    }
    return {"", 0};
}

// The operations of the prepared function that make queries costly, found in its IR (never estimated): a division or
// remainder by a variable and a multiplication of two variables (the solver blasts them into thousands of gates), the
// loads and stores of a source line (each is a read or write of a memory array, and unrolled loops make many), a copy
// of a large object, and the loops that were unrolled.
void collect_costs(Function &function, Unit &unit) {
    std::vector<Cost> found;
    std::map<std::pair<std::string, unsigned>, Cost> memory; // by source line
    std::map<std::pair<std::string, std::string>, Cost> arithmetic;
    unit.instructions = unit.loads = unit.stores = 0;
    for (Instruction &instruction : instructions(function)) {
        unit.instructions++;
        auto [file, line] = source_line_of(&instruction);
        auto add_to = [&](std::map<std::pair<std::string, unsigned>, Cost> &table, const std::string &what,
                          unsigned weight) {
            Cost &cost = table[{file, line}];
            if (cost.instructions.empty()) {
                cost.what = what;
                cost.file = file;
                cost.line = line;
                cost.count = 0;
                cost.weight = weight;
            }
            cost.count++;
            cost.instructions.push_back(&instruction);
        };
        if (isa<LoadInst>(instruction)) {
            unit.loads++;
            add_to(memory, "loads and stores of memory", 2);
        } else if (isa<StoreInst>(instruction)) {
            unit.stores++;
            add_to(memory, "loads and stores of memory", 2);
        } else if (auto *operation = dyn_cast<BinaryOperator>(&instruction)) {
            unsigned bits = type_bits(operation);
            unsigned opcode = operation->getOpcode();
            bool divides = opcode == Instruction::UDiv || opcode == Instruction::SDiv || opcode == Instruction::URem ||
                           opcode == Instruction::SRem;
            bool variable_divisor = divides && !isa<Constant>(operation->getOperand(1));
            bool variable_product = opcode == Instruction::Mul && bits >= 16 &&
                                    !isa<Constant>(operation->getOperand(0)) &&
                                    !isa<Constant>(operation->getOperand(1));
            if (!variable_divisor && !variable_product) continue;
            std::string what = std::to_string(bits) + "-bit ";
            if (variable_product) {
                what += "multiplication of two variables";
            } else {
                bool is_signed = opcode == Instruction::SDiv || opcode == Instruction::SRem;
                bool remainder = opcode == Instruction::URem || opcode == Instruction::SRem;
                what += std::string(is_signed ? "signed " : "unsigned ") + (remainder ? "remainder" : "division") +
                        " by a variable";
            }
            Cost &cost = arithmetic[{file + ":" + std::to_string(line), what}];
            if (cost.instructions.empty()) {
                cost.what = what;
                cost.file = file;
                cost.line = line;
                cost.count = 0;
                cost.weight = 3;
            }
            cost.count++;
            cost.instructions.push_back(&instruction);
        } else if (auto *memory_call = dyn_cast<MemIntrinsic>(&instruction)) {
            auto *length = dyn_cast<ConstantInt>(memory_call->getLength());
            if (length != nullptr && length->getZExtValue() < 32) continue;
            Cost cost;
            cost.what = std::string(isa<MemSetInst>(memory_call) ? "memset" : "memcpy") + " of " +
                        (length != nullptr ? std::to_string(length->getZExtValue()) + " bytes" : "a variable size");
            cost.file = file;
            cost.line = line;
            cost.weight = length != nullptr ? 2 : 3;
            cost.instructions.push_back(&instruction);
            found.push_back(cost);
        }
    }
    for (auto &entry : arithmetic)
        found.push_back(entry.second);
    std::vector<Cost> lines;
    for (auto &entry : memory) {
        if (entry.second.count >= 8) lines.push_back(entry.second);
    }
    std::stable_sort(lines.begin(), lines.end(), [](const Cost &a, const Cost &b) { return a.count > b.count; });
    for (size_t i = 0; i < lines.size() && i < 3; i++)
        found.push_back(lines[i]);
    for (Instruction &instruction : instructions(function)) { // large local objects
        auto *object = dyn_cast<AllocaInst>(&instruction);
        if (object == nullptr) continue;
        std::optional<TypeSize> size = object->getAllocationSize(function.getParent()->getDataLayout());
        if (!size || size->isScalable() || size->getFixedValue() < 128) continue;
        Cost cost;
        const Instruction *user = object->user_empty() ? object : cast<Instruction>(*object->user_begin());
        auto [file, line] = source_line_of(user);
        cost.what = "a local object of " + std::to_string(size->getFixedValue()) + " bytes";
        cost.file = file;
        cost.line = line;
        cost.weight = 2;
        cost.instructions.push_back(object);
        found.push_back(cost);
    }
    for (const UnrolledLoop &loop : unit.loops) {
        Cost cost;
        cost.what = loop.passes != 0 ? "a loop unrolled into " + std::to_string(loop.passes) + " passes"
                                     : "a loop unrolled (its pass count is not a constant LLVM reads before unrolling)";
        cost.file = loop.file;
        cost.line = loop.line;
        cost.count = loop.passes;
        cost.weight = 1;
        found.push_back(cost);
    }
    std::stable_sort(found.begin(), found.end(), [](const Cost &a, const Cost &b) { return a.weight > b.weight; });
    unit.costs = std::move(found);
}

} // namespace

Prepared program_prepare(Program &program, const std::string &name) {
    Prepared prepared;
    prepared.module = CloneModule(*program.module);
    prepared.function = prepared.module->getFunction(name);
    Function &function = *prepared.function;
    // A value a caller passes is a value. clang writes noundef on a scalar parameter, but not on the integers a struct
    // passed by value is split into (a 4-byte Move arrives as one i32), which Alive2 would then take as poison.
    for (Argument &argument : function.args()) {
        if (!argument.getType()->isPointerTy()) argument.addAttr(Attribute::NoUndef);
    }
    prepared.unit.name = source_name(function);
    if (const DISubprogram *subprogram = function.getSubprogram()) {
        prepared.unit.file = subprogram->getFilename().str();
        prepared.unit.line = subprogram->getLine();
    }
    // Pointer parameters may be the same object, unless the source says they are not: `restrict` (noalias) on one of
    // two that are not both only read (issue #57). Alive2 does not take the attribute; what it says is assumed below.
    for (unsigned i = 0; i < function.arg_size(); i++) {
        if (!function.getArg(i)->getType()->isPointerTy() || !function.hasParamAttribute(i, Attribute::NoAlias))
            continue;
        for (unsigned j = 0; j < function.arg_size(); j++) {
            if (j == i || !function.getArg(j)->getType()->isPointerTy()) continue;
            auto only_read = [&](unsigned k) {
                return function.hasParamAttribute(k, Attribute::ReadOnly) ||
                       function.hasParamAttribute(k, Attribute::ReadNone);
            };
            if (only_read(i) && only_read(j)) continue;
            prepared.unit.distinct_pointers.emplace_back(std::min(i, j), std::max(i, j));
        }
    }
    for (Function &other : *prepared.module) {
        for (unsigned i = 0; i < other.arg_size(); i++)
            other.removeParamAttr(i, Attribute::NoAlias);
        for (Instruction &instruction : instructions(other)) {
            if (auto *call = dyn_cast<CallBase>(&instruction)) {
                for (unsigned i = 0; i < call->arg_size(); i++)
                    call->removeParamAttr(i, Attribute::NoAlias);
            }
        }
    }
    std::set<std::string> recursive;
    for (const Function *function : program.recursive)
        recursive.insert(function->getName().str());
    UnitContext context{*prepared.module, function, prepared.unit, recursive, {}, nullptr, 0, {}};
    auto finish = [&]() {
        prepared.unprovable = std::move(context.unprovable);
        return std::move(prepared);
    };
    if (program.recursive.count(program.module->getFunction(name)) != 0) {
        const Instruction *at = &*function.getEntryBlock().getFirstNonPHIIt();
        for (const Instruction &instruction : instructions(function)) { // the call that goes round
            auto *call = dyn_cast<CallBase>(&instruction);
            if (call != nullptr && call->getCalledFunction() != nullptr &&
                recursive.count(call->getCalledFunction()->getName().str()) != 0) {
                at = call;
                break;
            }
        }
        context.fail(at, "recursion: " + prepared.unit.name + " calls itself, directly or through other functions",
                     "write it as a loop with a constant bound; a recursive function is not proved");
        return finish();
    }
    if (!resolve_calls(context)) return finish();

    Analyses analyses;
    std::string error;
    allow_unlimited_unrolling();
    analyses.run_function(function, clean_pipeline, error);
    std::string loop;
    int index = -1;
    std::vector<Argument *> bounded = assume_requires(function);
    analyses.functions.invalidate(function, PreservedAnalyses::none());
    mark_loops(analyses, function, prepared.unit);
    for (int round = 0; round < 4 && find_loop(function, loop, index); round++) {
        analyses.run_function(function, unroll_pipeline, error);
    }
    if (find_loop(function, loop, index)) {
        auto failure = std::make_unique<Unprovable>();
        size_t colon = loop.rfind(':');
        if (colon != std::string::npos) {
            failure->file = loop.substr(0, colon);
            failure->line = static_cast<unsigned>(std::stoul(loop.substr(colon + 1)));
        }
        describe_unbounded_loop(prepared.unit, index, *failure);
        context.unprovable = std::move(failure);
        return finish();
    }
    remove_loop_marks(function);
    remove_loop_ranges(bounded);
    Normalizer(context).run();
    drop_unsupported_attributes(function);
    analyses.run_function(function, "dce", error);
    collect_costs(function, prepared.unit);
    if (verifyFunction(function, &errs())) {
        context.fail(nullptr, "internal error: the IR of " + prepared.unit.name + " does not verify after preparing it",
                     "");
    }
    return finish();
}

std::set<const Instruction *> data_cone(const Instruction *root) {
    std::set<const Instruction *> cone;
    std::vector<const Instruction *> work = {root};
    while (!work.empty() && cone.size() < 200000) {
        const Instruction *instruction = work.back();
        work.pop_back();
        if (!cone.insert(instruction).second) continue;
        for (const Value *operand : instruction->operands()) {
            if (auto *dependency = dyn_cast<Instruction>(operand)) work.push_back(dependency);
        }
    }
    return cone;
}

namespace {
const char *kind_name(Kind kind) {
    switch (kind) {
    case Kind::Requires:
        return "smith_requires of a callee, proved at the call";
    case Kind::Ensures:
        return "smith_ensures, proved";
    case Kind::Invariant:
        return "smith_invariant, proved";
    case Kind::Assert:
        return "assert, proved";
    case Kind::Trap:
        return "undefined behaviour, proved absent";
    case Kind::Abort:
        return "abort or trap, proved not reached";
    case Kind::Premise:
        return "smith_requires, assumed";
    }
    return "";
}
} // namespace

std::string program_ir_text(const Prepared &prepared) {
    std::string text;
    raw_string_ostream stream(text);
    stream << "; " << prepared.unit.name << " (" << prepared.unit.file << ":" << prepared.unit.line
           << "), as `smith prove` checks it:\n";
    stream
        << "; callees with a smith_ensures are replaced by their contracts (the smith_requires slice before the call, "
           "the\n; smith_ensures slice after it), the other callees are inlined, loops are unrolled, and each "
           "contract, assert\n; and undefined behaviour is a call of smith_point(id, condition) listed here:\n";
    for (const Point &point : prepared.unit.points) {
        stream << ";   point " << point.id << ": " << kind_name(point.kind) << ": " << point.text << "  (" << point.file
               << ":" << point.line << ")\n";
    }
    if (prepared.unprovable != nullptr) {
        stream << "; cannot be proved: " << prepared.unprovable->what << " (" << prepared.unprovable->file << ":"
               << prepared.unprovable->line << "); this is the IR as far as the prover got\n";
    }
    prepared.function->print(stream);
    stream << "\n";
    stream.flush();
    return text;
}

namespace {

// Removes " #<digits>" (the number of an attribute group, which depends on the other functions of the module).
std::string without_group_numbers(const std::string &text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '#' && i > 0 && text[i - 1] == ' ' && i + 1 < text.size() &&
            isdigit(static_cast<unsigned char>(text[i + 1]))) {
            size_t end = i + 1;
            while (end < text.size() && isdigit(static_cast<unsigned char>(text[end])))
                end++;
            out.pop_back(); // the space before it
            i = end - 1;
            continue;
        }
        out += text[i];
    }
    return out;
}

void attributes_text(raw_string_ostream &stream, const AttributeList &attributes) {
    for (unsigned index : attributes.indexes()) {
        std::string text = attributes.getAsString(index);
        if (!text.empty()) stream << "[" << index << ":" << text << "]";
    }
}

} // namespace

std::string program_proof_key(const Prepared &prepared) {
    std::unique_ptr<Module> copy = CloneModule(*prepared.module);
    StripDebugInfo(*copy);
    Function *function = copy->getFunction(prepared.function->getName());
    std::string text;
    raw_string_ostream stream(text);
    stream << copy->getDataLayoutStr() << "\n" << copy->getTargetTriple().str() << "\n";
    std::set<const GlobalValue *> used;
    std::vector<const Constant *> work;
    std::set<const Constant *> visited;
    for (Instruction &instruction : instructions(*function)) {
        for (const Value *operand : instruction.operands()) {
            if (auto *constant = dyn_cast<Constant>(operand)) work.push_back(constant);
        }
    }
    while (!work.empty()) {
        const Constant *constant = work.back();
        work.pop_back();
        if (!visited.insert(constant).second) continue;
        if (auto *global = dyn_cast<GlobalValue>(constant)) {
            if (global != function) used.insert(global);
            if (auto *variable = dyn_cast<GlobalVariable>(global)) {
                if (variable->hasInitializer()) work.push_back(variable->getInitializer());
            }
            continue;
        }
        for (const Value *operand : constant->operands()) {
            if (auto *inner = dyn_cast<Constant>(operand)) work.push_back(inner);
        }
    }
    std::vector<const GlobalValue *> sorted(used.begin(), used.end());
    // Ordered by name, which the pointers do not decide.
    // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order)
    std::sort(sorted.begin(), sorted.end(),
              [](const GlobalValue *a, const GlobalValue *b) { return a->getName() < b->getName(); });
    std::string body;
    raw_string_ostream body_stream(body);
    for (const GlobalValue *global : sorted) {
        global->print(body_stream);
        if (auto *callee = dyn_cast<Function>(global)) attributes_text(body_stream, callee->getAttributes());
        body_stream << "\n";
    }
    function->print(body_stream);
    attributes_text(body_stream, function->getAttributes());
    for (Instruction &instruction : instructions(*function)) {
        if (auto *call = dyn_cast<CallBase>(&instruction)) attributes_text(body_stream, call->getAttributes());
    }
    body_stream.flush();
    stream << without_group_numbers(body) << "\n";
    for (const Point &point : prepared.unit.points)
        stream << "point " << point.id << " " << static_cast<int>(point.kind) << " " << point.assume << " "
               << point.call_site << "\n";
    for (auto [first, second] : prepared.unit.distinct_pointers)
        stream << "distinct " << first << " " << second << "\n";
    stream.flush();
    return text;
}

} // namespace prove
