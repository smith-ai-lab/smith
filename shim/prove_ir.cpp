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
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <cstring>
#include <fstream>
#include <sstream>

using namespace llvm;

namespace prove {
namespace {

// ---- names the macros and clang use ----

enum class Contract { None, Requires, Ensures, Invariant };

Contract contract_of(const Instruction *instruction) {
    auto *call = dyn_cast_or_null<CallBase>(instruction);
    if (call == nullptr || call->getCalledFunction() == nullptr) return Contract::None;
    StringRef name = call->getCalledFunction()->getName();
    if (name == "smith_contract_requires") return Contract::Requires;
    if (name == "smith_contract_ensures") return Contract::Ensures;
    if (name == "smith_contract_invariant") return Contract::Invariant;
    return Contract::None;
}

bool contract_name(StringRef name) {
    return name == "smith_contract_requires" || name == "smith_contract_ensures" || name == "smith_contract_invariant";
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

// Gives every defined function a number, and every contract call the number of the function it is written in.
void annotate(Module &module, unsigned &counter) {
    for (Function &function : module) {
        if (function.isDeclaration()) continue;
        MDNode *node = id_node(module.getContext(), ++counter);
        function.setMetadata("smith.id", node);
        for (Instruction &instruction : instructions(function)) {
            if (contract_of(&instruction) != Contract::None) instruction.setMetadata("smith.owner", node);
        }
    }
}

bool has_own_contract(const Function &function, Contract kind) {
    for (const Instruction &instruction : instructions(function)) {
        if (contract_of(&instruction) == kind && owner_id(instruction) == function_id(function)) return true;
    }
    return false;
}

bool has_any_own_contract(const Function &function) {
    for (const Instruction &instruction : instructions(function)) {
        if (contract_of(&instruction) != Contract::None && owner_id(instruction) == function_id(function)) return true;
    }
    return false;
}

// A function with an smith_ensures is used through its contract; any other is part of its caller.
bool is_modular(const Function &function) {
    return !function.isDeclaration() && has_own_contract(function, Contract::Ensures);
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

// What a pointer type points to, stripped of typedefs and qualifiers, when objects of other types cannot share its
// memory (C's aliasing rules): "" for void and for the character types, which can.
const DIType *distinct_pointee(const DIType *type) {
    auto strip = [](const DIType *t) {
        while (auto *derived = dyn_cast_or_null<DIDerivedType>(t)) {
            unsigned tag = derived->getTag();
            if (tag != dwarf::DW_TAG_typedef && tag != dwarf::DW_TAG_const_type && tag != dwarf::DW_TAG_volatile_type &&
                tag != dwarf::DW_TAG_restrict_type && tag != dwarf::DW_TAG_atomic_type)
                break;
            t = derived->getBaseType();
        }
        return t;
    };
    type = strip(type);
    auto *pointer = dyn_cast_or_null<DIDerivedType>(type);
    if (pointer == nullptr || pointer->getTag() != dwarf::DW_TAG_pointer_type) return nullptr;
    const DIType *pointee = strip(pointer->getBaseType());
    if (pointee == nullptr) return nullptr;
    if (auto *basic = dyn_cast<DIBasicType>(pointee)) {
        if (basic->getEncoding() == dwarf::DW_ATE_signed_char || basic->getEncoding() == dwarf::DW_ATE_unsigned_char)
            return nullptr;
    }
    return pointee;
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
                recursive.count(callee) != 0)
                continue;
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
}

bool is_snapshot(const Function &function) {
    return function.hasFnAttribute("smith.snapshot");
}

} // namespace

bool program_load(Program &program, const std::vector<std::string> &bitcode, std::string &error) {
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
    if (!analyses.run_module(module, "inferattrs,cgscc(function-attrs)", error)) return false;
    if (verifyModule(module, &errs())) {
        error = "the linked module does not verify";
        return false;
    }
    return true;
}

std::vector<std::string> program_units(const Program &program) {
    std::vector<std::string> names;
    for (const Function &function : *program.module) {
        if (function.isDeclaration() || is_snapshot(function)) continue;
        bool unit = has_any_own_contract(function);
        if (!unit && program.called.count(&function) == 0) {
            for (const Instruction &instruction : instructions(function)) {
                auto *call = dyn_cast<CallBase>(&instruction);
                if (call != nullptr && call->getCalledFunction() != nullptr && is_modular(*call->getCalledFunction()))
                    unit = true;
            }
        }
        if (unit) names.push_back(function.getName().str());
    }
    return names;
}

namespace {
std::string source_name(const Function &function);
}

std::string program_label(const Program &program, const std::string &name) {
    const Function *function = program.module->getFunction(name);
    return function == nullptr ? name : source_name(*function);
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
        return instruction.mayWriteToMemory();
    }

    bool written_before(const Instruction &load) const {
        for (const Instruction *writer : writers) {
            if (writer != &load && isPotentiallyReachable(writer, &load, nullptr, &dominators)) return true;
        }
        return false;
    }

    bool written_after(const Instruction &load) const {
        for (const Instruction *writer : writers) {
            if (writer != &load && isPotentiallyReachable(&load, writer, nullptr, &dominators)) return true;
        }
        return false;
    }
};

std::string location_of(const Instruction *instruction) {
    if (instruction == nullptr || !instruction->getDebugLoc()) return "";
    return instruction->getDebugLoc()->getFilename().str() + ":" + std::to_string(instruction->getDebugLoc().getLine());
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
                cone.old_loads.push_back(const_cast<LoadInst *>(load));
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
            if (load != nullptr && analysis.written_before(*load)) {
                slices.error = "its smith_requires reads memory after the function has written (" + location_of(load) +
                               "); put every smith_requires first";
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
            if (!is_old && analysis.written_after(*load)) {
                slices.error = "its smith_ensures reads memory before the function's last write (" + location_of(load) +
                               "); put every smith_ensures just before the return";
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
        auto *ensures_call = CallInst::Create(slices.ensures_slice, with_result, "", call.getNextNode()->getIterator());
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
                context.fail(
                    call, "inline assembly cannot be proved",
                    "move it into a function with a body that is plain C, or into one that a smith_ensures describes");
                return false;
            }
            if (callee == nullptr) {
                context.fail(call, "a call through a function pointer: the target is not known",
                             "call the function directly; a contract on the pointer's type (SMITH_CONTRACT_FOR) is not "
                             "read yet");
                return false;
            }
            StringRef name = callee->getName();
            if (name.starts_with("objc_msgSend")) {
                context.fail(call, "an Objective-C message send: the method it reaches is not known",
                             "call a C function that wraps it and has a smith_ensures, or a plain C function");
                return false;
            }
            if (callee->isIntrinsic() || contract_of(call) != Contract::None || is_assert_failure(call) ||
                is_abort(call) || name == "smith_point") {
                call->setMetadata("smith.resolved", MDNode::get(function.getContext(), {}));
                target = nullptr;
                continue;
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
            if (known_library_function(name)) {
                call->setMetadata("smith.resolved", MDNode::get(function.getContext(), {}));
                target = nullptr;
                continue;
            }
            context.fail(call, "a call of " + name.str() + ", which has neither a body nor a contract",
                         "define " + name.str() +
                             " in a file given to smith prove, or give the call to a function with a smith_ensures");
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
            if (kind == Contract::Requires && own) {
                point(instruction, Kind::Premise, instruction->getOperand(0), true);
            } else if (kind == Contract::Ensures && !own) {
                point(instruction, Kind::Premise, instruction->getOperand(0), true);
            } else if (kind != Contract::None) {
                Kind point_kind = kind == Contract::Requires  ? Kind::Requires
                                  : kind == Contract::Ensures ? Kind::Ensures
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
            if (kind != Kind::Trap && scope.starts_with("smith_") && location->getInlinedAt() != nullptr) {
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
            info.kind == Kind::Premise)
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
    for (unsigned i = 0; i < function.arg_size(); i++)
        function.removeParamAttr(i, Attribute::NoFree);
    for (Function &callee : *function.getParent()) {
        for (unsigned i = 0; i < callee.arg_size(); i++)
            callee.removeParamAttr(i, Attribute::NoFree);
    }
    for (Instruction &instruction : instructions(function)) {
        auto *call = dyn_cast<CallBase>(&instruction);
        if (call == nullptr) continue;
        for (unsigned i = 0; i < call->arg_size(); i++)
            call->removeParamAttr(i, Attribute::NoFree);
    }
}

// LLVM deletes a loop it can solve (n counted down to 0) and the proof would then say nothing of it, so each loop gets
// a call the optimizer cannot remove: when unrolling is done, a loop that is left is a loop with no constant bound.
void mark_loops(Analyses &analyses, Function &function) {
    Module &module = *function.getParent();
    FunctionCallee marker = module.getOrInsertFunction("smith_loop", Type::getVoidTy(module.getContext()));
    cast<Function>(marker.getCallee())->setMemoryEffects(MemoryEffects::inaccessibleMemOnly());
    auto &loops = analyses.functions.getResult<LoopAnalysis>(function);
    for (Loop *loop : loops.getLoopsInPreorder()) {
        BasicBlock *header = loop->getHeader();
        CallInst::Create(marker, "", header->getFirstInsertionPt());
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

// A loop that is still there after unrolling: where it is (a source line, if there is one).
bool find_loop(Function &function, std::string &where) {
    DominatorTree dominators(function);
    LoopInfo loops(dominators);
    if (loops.empty()) return false;
    Loop *loop = *loops.begin();
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

} // namespace

Prepared program_prepare(Program &program, const std::string &name) {
    Prepared prepared;
    prepared.module = CloneModule(*program.module);
    prepared.function = prepared.module->getFunction(name);
    Function &function = *prepared.function;
    prepared.unit.name = source_name(function);
    if (const DISubprogram *subprogram = function.getSubprogram()) {
        prepared.unit.file = subprogram->getFilename().str();
        prepared.unit.line = subprogram->getLine();
    }
    if (DISubprogram *subprogram = function.getSubprogram();
        subprogram != nullptr && subprogram->getType() != nullptr) {
        auto types = subprogram->getType()->getTypeArray();
        if (types.size() == function.arg_size() + 1) {
            for (unsigned i = 0; i < function.arg_size(); i++) {
                for (unsigned j = i + 1; j < function.arg_size(); j++) {
                    const DIType *first = distinct_pointee(types[i + 1]);
                    const DIType *second = distinct_pointee(types[j + 1]);
                    if (first != nullptr && second != nullptr && first != second)
                        prepared.unit.distinct_pointers.emplace_back(i, j);
                }
            }
        }
    }
    std::set<std::string> recursive;
    for (const Function *function : program.recursive)
        recursive.insert(function->getName().str());
    UnitContext context{*prepared.module, function, prepared.unit, recursive, {}, nullptr};
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
    mark_loops(analyses, function);
    for (int round = 0; round < 4 && find_loop(function, loop); round++) {
        analyses.run_function(function, unroll_pipeline, error);
    }
    if (find_loop(function, loop)) {
        auto failure = std::make_unique<Unprovable>();
        size_t colon = loop.rfind(':');
        if (colon != std::string::npos) {
            failure->file = loop.substr(0, colon);
            failure->line = static_cast<unsigned>(std::stoul(loop.substr(colon + 1)));
        }
        failure->what = "a loop with no constant bound that LLVM can unroll";
        failure->fix = "give it a constant bound (for (int i = 0; i < 8; i++)); smith_invariant for loops without one "
                       "is not checked yet";
        context.unprovable = std::move(failure);
        return finish();
    }
    remove_loop_marks(function);
    Normalizer(context).run();
    drop_unsupported_attributes(function);
    analyses.run_function(function, "dce", error);
    if (verifyFunction(function, &errs())) {
        context.fail(nullptr, "internal error: the IR of " + prepared.unit.name + " does not verify after preparing it",
                     "");
    }
    return finish();
}

} // namespace prove
