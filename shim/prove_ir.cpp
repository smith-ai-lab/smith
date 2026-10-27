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
#include <algorithm>
#include <tuple>
#include "prove.hpp"

#include <cctype>
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
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

using namespace llvm;

namespace prove {
namespace {

// ---- names the macros and clang use ----

enum class Contract { None, Requires, Ensures, Invariant, Assert, Trusted, Modifies };

Contract contract_of(const Instruction *instruction) {
    auto *call = dyn_cast_or_null<CallBase>(instruction);
    if (call == nullptr || call->getCalledFunction() == nullptr) return Contract::None;
    StringRef name = call->getCalledFunction()->getName();
    if (name == "smith_contract_requires") return Contract::Requires;
    if (name == "smith_contract_ensures") return Contract::Ensures;
    if (name == "smith_contract_invariant") return Contract::Invariant;
    if (name == "smith_contract_assert") return Contract::Assert;
    if (name == "smith_contract_trusted") return Contract::Trusted;
    if (name == "smith_contract_may_write") return Contract::Modifies;
    return Contract::None;
}

bool contract_name(StringRef name) {
    return name == "smith_contract_requires" || name == "smith_contract_ensures" ||
           name == "smith_contract_invariant" || name == "smith_contract_assert" || name == "smith_contract_trusted" ||
           name == "smith_contract_may_write";
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
    if (name == "smith_failure") // the check of an index or a slice; the contract functions of the library are its own
        return !contract_name(call->getFunction()->getName());
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
    if (function.hasFnAttribute(Attribute::AlwaysInline)) return false; // a generic instance such as contract::modifies
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
    return !function.isDeclaration() && !function.hasFnAttribute("smith.snapshot") &&
           !function.hasFnAttribute("smith.library") && !is_trusted(function) && has_any_own_contract(function);
}

// A function with an smith_ensures, or a trusted one, is used through its contract; any other is part of its caller.
bool is_modular(const Function &function) {
    return !function.isDeclaration() && (has_own_contract(function, Contract::Ensures) ||
                                         has_own_contract(function, Contract::Modifies) || is_trusted(function));
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

// A local nothing has written holds one value, any one: not `undef`, which LLVM may take to be whatever suits, so that
// sroa turns `int x; if (c) x = 7; ... x == 7` into 7 and the proof says an uninitialized read is 7 (issue #64). A
// store of `freeze undef` to each scalar of each local, at the top of the function, keeps it a value the proof cannot
// choose; every real store after it makes the freeze dead. A local of more than 256 scalars is left as it is.
void count_scalars(Type *type, uint64_t &count) {
    if (auto *record = dyn_cast<StructType>(type)) {
        for (Type *element : record->elements())
            count_scalars(element, count);
    } else if (auto *array = dyn_cast<ArrayType>(type)) {
        uint64_t before = count;
        count_scalars(array->getElementType(), count);
        count = before + (count - before) * array->getNumElements();
    } else {
        count++;
    }
}

void freeze_scalars(IRBuilder<> &builder, Type *type, Value *pointer) {
    if (auto *record = dyn_cast<StructType>(type)) {
        for (unsigned i = 0; i < record->getNumElements(); i++)
            freeze_scalars(builder, record->getElementType(i), builder.CreateStructGEP(record, pointer, i));
    } else if (auto *array = dyn_cast<ArrayType>(type)) {
        for (uint64_t i = 0; i < array->getNumElements(); i++)
            freeze_scalars(builder, array->getElementType(), builder.CreateConstInBoundsGEP2_64(array, pointer, 0, i));
    } else {
        builder.CreateAlignedStore(builder.CreateFreeze(UndefValue::get(type)), pointer, Align(1));
    }
}

void freeze_locals(Function &function) {
    std::vector<AllocaInst *> locals;
    for (Instruction &instruction : function.getEntryBlock()) {
        auto *local = dyn_cast<AllocaInst>(&instruction);
        if (local != nullptr && local->isStaticAlloca() && local->getAllocatedType()->isSized())
            locals.push_back(local);
    }
    for (AllocaInst *local : locals) {
        uint64_t count = 0;
        count_scalars(local->getAllocatedType(), count);
        if (count == 0 || count > 256) continue;
        IRBuilder<> builder(local->getNextNode());
        freeze_scalars(builder, local->getAllocatedType(), local);
    }
}

// Locals live in registers, redundant loads go, and the control flow is simple. These only refine the program, so a
// proof of the result holds for the original in every case LLVM keeps defined.
const char *const clean_pipeline = "sroa<modify-cfg>,early-cse,simplifycfg,dce";

// The same without early-cse, which would replace a load by the value an earlier store wrote. A contract is evaluated
// where the caller can read the memory the callee wrote, so the loads of its snapshot must stay loads.
const char *const light_pipeline = "sroa<modify-cfg>,simplifycfg,dce";

// Loops of constant bounds are unrolled, whatever the cost model thinks. instcombine does not verify that it reached
// its fixpoint: on a long chain of float arithmetic after unrolling (game's skeleton_reach) LLVM stops with "did not
// reach a fixpoint", a check of its own idempotence that a result still refining the program does not need (#74).
const char *const unroll_pipeline =
    "loop-simplify,lcssa,loop(indvars),loop-unroll<O3;full-unroll-max=1000000;no-partial;no-runtime;upperbound>,"
    "sroa<modify-cfg>,early-cse,simplifycfg,instcombine<no-verify-fixpoint>,gvn,instcombine<no-verify-fixpoint>,"
    "simplifycfg,dse,dce";

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
    // A function that returns a large struct has the pointer it writes it through first, which the source never names.
    unsigned hidden = function.arg_size() > 0 && function.getArg(0)->hasStructRetAttr() ? 1 : 0;
    if (types.size() != function.arg_size() + 1 - hidden) return;
    for (unsigned i = hidden; i < function.arg_size(); i++) {
        Argument *argument = function.getArg(i);
        if (!argument->getType()->isPointerTy() || argument->hasStructRetAttr() || argument->hasByValAttr()) continue;
        uint64_t bytes = pointee_bytes(types[i - hidden + 1]);
        if (bytes == 0) continue;
        // `T p[static N]` is N objects: clang says so with its own dereferenceable, which the type's size must not
        // shrink.
        bytes = std::max<uint64_t>(bytes, argument->getDereferenceableBytes());
        argument->addAttr(Attribute::getWithDereferenceableBytes(function.getContext(), bytes));
        argument->addAttr(Attribute::NonNull);
    }
}

// The alignment of what a pointer type points to, in bytes, from debug info: what C gives the type; 1 when unknown.
uint64_t type_alignment(const DIType *type, unsigned depth = 0) {
    if (type == nullptr || depth > 8) return 1;
    if (auto *derived = dyn_cast<DIDerivedType>(type)) {
        unsigned tag = derived->getTag();
        if (tag == dwarf::DW_TAG_pointer_type) return 8;
        if (derived->getAlignInBits() != 0) return derived->getAlignInBits() / 8;
        return type_alignment(derived->getBaseType(), depth + 1);
    }
    if (auto *composite = dyn_cast<DICompositeType>(type)) {
        if (composite->getAlignInBits() != 0) return composite->getAlignInBits() / 8;
        if (composite->getTag() == dwarf::DW_TAG_array_type || composite->getTag() == dwarf::DW_TAG_enumeration_type)
            return type_alignment(composite->getBaseType(), depth + 1);
        uint64_t align = 1;
        for (const Metadata *member : composite->getElements())
            align = std::max(align, type_alignment(dyn_cast_or_null<DIType>(member), depth + 1));
        return align;
    }
    uint64_t bytes = (type->getSizeInBits() + 7) / 8;
    return bytes != 0 && (bytes & (bytes - 1)) == 0 ? std::min<uint64_t>(bytes, 16) : 1;
}

uint64_t pointee_alignment(const DIType *type) {
    while (auto *derived = dyn_cast_or_null<DIDerivedType>(type)) {
        unsigned tag = derived->getTag();
        if (tag == dwarf::DW_TAG_pointer_type) return type_alignment(derived->getBaseType());
        if (tag != dwarf::DW_TAG_typedef && tag != dwarf::DW_TAG_const_type && tag != dwarf::DW_TAG_volatile_type &&
            tag != dwarf::DW_TAG_restrict_type && tag != dwarf::DW_TAG_atomic_type)
            break;
        type = derived->getBaseType();
    }
    return 1;
}

// ---- slice parameters ----

// A run-time array is the struct smith_array_<T><dims> { T *data; usize count[k]; }, k the number of `:`, the fixed
// dimensions in its name (`f32[:][2]` is smith_array_f32_n_2, `f32[:][:]` smith_array_f32_n_n). `element` is the size
// of T in bytes (0 when the type is none of these; a typedef or a const of one is peeled), `fixed` the product of the
// fixed dimensions and `runtime` the number of `count`s: the data holds count[0] * ... * count[k-1] * fixed objects. A
// struct of one count is 16 bytes, which arm64 passes as one [2 x i64]; one with more is passed in memory.
struct SliceLayout {
    uint64_t element = 0, align = 1, fixed = 1;
    unsigned runtime = 1;
    SmallVector<uint64_t, 4> dims; // each dimension in order: its size, 0 for a count
};

SliceLayout slice_layout(const DIType *type) {
    while (auto *derived = dyn_cast_or_null<DIDerivedType>(type)) {
        unsigned tag = derived->getTag();
        if (tag != dwarf::DW_TAG_typedef && tag != dwarf::DW_TAG_const_type && tag != dwarf::DW_TAG_volatile_type)
            break;
        type = derived->getBaseType();
    }
    SliceLayout layout;
    auto *composite = dyn_cast_or_null<DICompositeType>(type);
    if (composite == nullptr || composite->getTag() != dwarf::DW_TAG_structure_type ||
        !composite->getName().starts_with("smith_array_") || composite->getSizeInBits() % 64 != 0 ||
        composite->getSizeInBits() < 128 || composite->getSizeInBits() > uint64_t{64} * 9)
        return layout;
    layout.runtime = static_cast<unsigned>(composite->getSizeInBits() / 64 - 1);
    // the dimensions, from the end of the name: `_2` is fixed, `_n` is a count; the last `_n` that makes k of them is
    // the first dimension, so what is before it is the element's name
    StringRef name = composite->getName().drop_front(12);
    unsigned counts = 0;
    SmallVector<uint64_t, 4> reversed;
    while (counts < layout.runtime) {
        size_t at = name.rfind('_');
        if (at == StringRef::npos) return SliceLayout();
        StringRef token = name.drop_front(at + 1);
        uint64_t value = 0;
        if (token == "n")
            counts++, reversed.push_back(0);
        else if (token.empty() || token.getAsInteger(10, value) || value == 0)
            return SliceLayout();
        else
            layout.fixed *= value, reversed.push_back(value);
        name = name.take_front(at);
    }
    layout.dims.assign(reversed.rbegin(), reversed.rend());
    for (const Metadata *member : composite->getElements()) {
        auto *field = dyn_cast<DIDerivedType>(member);
        if (field == nullptr || field->getName() != "data") continue;
        layout.align = pointee_alignment(field->getBaseType());
        layout.element = pointee_bytes(field->getBaseType());
        return layout;
    }
    return SliceLayout();
}

// The size of T in bytes of a slice type with one count (the form arm64 passes as a pair), else 0, and its alignment.
uint64_t slice_element_bytes(const DIType *type, uint64_t &align) {
    SliceLayout layout = slice_layout(type);
    align = layout.align;
    return layout.runtime == 1 ? layout.element * layout.fixed : 0;
}

bool points_to_const(const DIType *type);

// The data of a slice the function writes through, itself or by a callee that names it in a `modifies`: its object is
// writable. Its size is the assumption the unit makes below; the one byte is what Alive2 needs to take the attribute.
void mark_slice_writable(Argument *data) {
    if (data->hasAttribute(Attribute::Writable)) return;
    data->addAttr(Attribute::Writable);
    data->addAttr(Attribute::NonNull);
    if (data->getDereferenceableBytes() == 0)
        data->addAttr(Attribute::getWithDereferenceableBytes(data->getContext(), 1));
}

// Whether `type` is a slice of const elements (`const T xs[:]`): its data pointer points at const.
bool slice_is_const(const DIType *type) {
    while (auto *derived = dyn_cast_or_null<DIDerivedType>(type)) {
        unsigned tag = derived->getTag();
        if (tag != dwarf::DW_TAG_typedef && tag != dwarf::DW_TAG_const_type && tag != dwarf::DW_TAG_volatile_type)
            break;
        type = derived->getBaseType();
    }
    auto *composite = dyn_cast_or_null<DICompositeType>(type);
    if (composite == nullptr) return false;
    for (const Metadata *member : composite->getElements()) {
        auto *field = dyn_cast<DIDerivedType>(member);
        if (field == nullptr || field->getName() != "data") continue;
        return points_to_const(field);
    }
    return false;
}

// The `inbounds` of the address arithmetic on a slice's pointer (the lowering's `data + a`) is dropped: the slice's own
// checks, proved as points, say a <= b <= count, so the arithmetic stays inside the object, and what an element access
// needs is checked at the access itself. Left on, each `inbounds` is a query about the sizes of two blocks, and
// `xs[skip:][n - 1]` takes 16 s instead of 2.
void relax_slice_arithmetic(const std::vector<Value *> &roots) {
    SmallVector<Value *, 8> work(roots.begin(), roots.end());
    SmallPtrSet<Value *, 16> seen(roots.begin(), roots.end());
    while (!work.empty()) {
        Value *value = work.pop_back_val();
        for (User *user : value->users()) {
            if (!isa<GetElementPtrInst>(user) && !isa<PHINode>(user) && !isa<SelectInst>(user)) continue;
            if (auto *gep = dyn_cast<GetElementPtrInst>(user)) gep->setNoWrapFlags(GEPNoWrapFlags::none());
            if (seen.insert(user).second) work.push_back(user);
        }
    }
}

// The body reads the slot of a slice passed in memory with loads at constant offsets (`data`, `count[j]`): each is the
// value the unit was given, so the pointer the body computes with is the parameter itself (what
// `relax_slice_arithmetic` follows, and what an index is checked against). Left as it is when the slot is used in any
// other way (before the unit's own stores into it).
void forward_slot_loads(AllocaInst *slot, Value *data, const std::vector<Value *> &sizes) {
    const DataLayout &layout = slot->getModule()->getDataLayout();
    std::vector<std::pair<LoadInst *, Value *>> forwards;
    SmallVector<std::pair<Value *, uint64_t>, 8> work = {{slot, 0}};
    while (!work.empty()) {
        auto [pointer, offset] = work.pop_back_val();
        for (User *user : pointer->users()) {
            if (auto *gep = dyn_cast<GetElementPtrInst>(user)) {
                APInt step(64, 0);
                if (!gep->accumulateConstantOffset(layout, step)) return;
                work.push_back({gep, offset + step.getZExtValue()});
            } else if (auto *load = dyn_cast<LoadInst>(user)) {
                unsigned index = static_cast<unsigned>(offset / 8);
                bool pointer_slot = index == 0 && load->getType()->isPointerTy();
                if (offset % 8 != 0 || index > sizes.size() || (index != 0 && !load->getType()->isIntegerTy(64)) ||
                    (index == 0 && !pointer_slot) || load->isVolatile())
                    return;
                forwards.emplace_back(load, index == 0 ? data : sizes[index - 1]);
            } else {
                return; // a write or a use of the address
            }
        }
    }
    for (auto &[load, value] : forwards) {
        load->replaceAllUsesWith(value);
        load->eraseFromParent();
    }
}

// What a slice of rows (more than one count) is assumed to hold, and what its index arithmetic is given.
//
// Every dimension and every prefix of them (`P_j`, the product of the first j + 1) is below 2^20 (a million elements:
// the solver's time grows with the bound, 2 s for a read at 2^20, 5 s at 2^28 and 6 s at 2^32; a slice of one count is
// held to 2^32): no object here is bigger, and the products cannot wrap. The solver is slow at a product of two
// variables in an index (`r * count + c` against `count0 * count1`: nothing in 10 s even for counts of 8 bits), so
// `P_j` of a count is a parameter of the unit that is only known by what is true of a product (`given`): it is 0 when a
// factor is, it is at least each factor when none is 0, it is at least `P_(j-1)` when the count is not 0, and each
// multiplication of the body by a count is followed by `x < P_(j-1)` gives `x * count_j + count_j <= P_j`. Every one of
// them holds of the real product (the bounds keep it from wrapping), so a proof from them is a proof for it, and an
// index `x * count_j + i`, i < count_j, is inside the object by additions and comparisons. A product the program takes
// itself (`rows * cols`) is the solver's own. Returns the number of elements, P of the last dimension.
Value *assume_row_products(IRBuilder<> &builder, Function *unit, const SmallVector<uint64_t, 4> &dims,
                           const std::vector<Value *> &sizes, const std::vector<Value *> &given,
                           unsigned &next_product) {
    Type *i64 = builder.getInt64Ty();
    Value *zero = ConstantInt::get(i64, 0);
    auto below = [&](Value *value) {
        builder.CreateAssumption(builder.CreateICmpULT(value, ConstantInt::get(i64, 1ull << 20)));
    };
    auto implies = [&](Value *when, Value *then) {
        builder.CreateAssumption(builder.CreateOr(builder.CreateNot(when), then));
    };
    std::vector<Value *> prefix;    // P_j
    std::vector<Value *> dimension; // the size of dimension j
    unsigned count = 0, next = 0;
    for (uint64_t size : dims) {
        Value *value = size == 0 ? sizes[count++] : static_cast<Value *>(ConstantInt::get(i64, size));
        dimension.push_back(value);
        below(value);
        if (prefix.empty()) {
            prefix.push_back(value);
            continue;
        }
        Value *before = prefix.back();
        if (size != 0) { // a product by a constant is linear
            prefix.push_back(builder.CreateMul(before, value));
            below(prefix.back());
            continue;
        }
        Value *product = given[next++];
        prefix.push_back(product);
        below(product);
        Value *before_zero = builder.CreateICmpEQ(before, zero), *value_zero = builder.CreateICmpEQ(value, zero);
        Value *product_zero = builder.CreateICmpEQ(product, zero);
        implies(before_zero, product_zero);
        implies(value_zero, product_zero);
        implies(builder.CreateAnd(builder.CreateNot(before_zero), builder.CreateNot(value_zero)),
                builder.CreateAnd(builder.CreateICmpUGE(product, value), builder.CreateICmpUGE(product, before)));
    }
    for (size_t j = 1; j < dims.size(); j++) {
        if (dims[j] != 0) continue;
        Value *size = dimension[j];
        SmallVector<BinaryOperator *, 4> sites;
        for (User *user : size->users()) {
            auto *multiply = dyn_cast<BinaryOperator>(user);
            if (multiply != nullptr && multiply->getOpcode() == Instruction::Mul && multiply->getFunction() == unit &&
                !isa<ConstantInt>(multiply->getOperand(0)) && !isa<ConstantInt>(multiply->getOperand(1)) &&
                multiply->getOperand(0) != multiply->getOperand(1) && !multiply->hasNoUnsignedWrap() &&
                !multiply->hasNoSignedWrap()) // a product that can be poison stays what it is
                sites.push_back(multiply);
        }
        std::map<Value *, Value *> known; // the product of x and this count, one value for every site of the same x
        for (BinaryOperator *multiply : sites) {
            if (next_product >= unit->arg_size()) break;
            Value *x = multiply->getOperand(0) == size ? multiply->getOperand(1) : multiply->getOperand(0);
            auto [found, fresh] = known.emplace(x, nullptr);
            if (fresh) found->second = unit->getArg(next_product++);
            Value *product = found->second;
            IRBuilder<> at(multiply);
            Value *x_zero = at.CreateICmpEQ(x, zero), *size_zero = at.CreateICmpEQ(size, zero);
            Value *product_zero = at.CreateICmpEQ(product, zero);
            at.CreateAssumption(at.CreateOr(at.CreateNot(x_zero), product_zero));
            at.CreateAssumption(at.CreateOr(at.CreateNot(size_zero), product_zero));
            Value *inside = at.CreateICmpULT(x, prefix[j - 1]);
            // x * count + count <= P_j, written without the sum that could wrap
            Value *fits = at.CreateAnd(at.CreateICmpULE(size, prefix[j]),
                                       at.CreateICmpULE(product, at.CreateSub(prefix[j], size)));
            at.CreateAssumption(at.CreateOr(at.CreateNot(inside), fits));
            at.CreateAssumption(
                at.CreateOr(at.CreateNot(at.CreateAnd(inside, at.CreateNot(size_zero))), at.CreateICmpUGE(product, x)));
            multiply->replaceAllUsesWith(product);
            multiply->eraseFromParent();
        }
    }
    return prefix.back();
}

// The slice parameters of the function being proved point at `count` elements: its `data` is dereferenceable for
// count * sizeof(T) bytes, which is what makes `xs[i]` with `i < countof(xs)` defined with no other contract. Only
// the unit has it: a slice a callee receives is whatever its caller built, and the caller proves that.
//
// A slice with more than one count (`[:][:]`) is passed in memory: the unit takes (ptr data, i64 count...) and builds
// the struct in a slot of its own, which the body reads as it read the argument.
//
// arm64 passes the struct as one [2 x i64], so the pointer is an integer that the body turns back with inttoptr, a
// pointer with no object that Alive2 can check an index against. The unit gets a function of its own whose slice is
// two parameters, (ptr data, i64 count); the body's `inttoptr(extract 0)` is `data`, and `extract 1` is `count`.
Function *split_slice_parameters(Function *function) {
    DISubprogram *subprogram = function->getSubprogram();
    if (subprogram == nullptr || subprogram->getType() == nullptr) return function;
    auto types = subprogram->getType()->getTypeArray();
    if (types.size() != function->arg_size() + 1) return function; // a hidden sret result moves them; not read
    LLVMContext &context = function->getContext();
    std::vector<uint64_t> elements(function->arg_size(), 0), aligns(function->arg_size(), 1);
    std::vector<unsigned> counts(function->arg_size(), 1);            // the counts of a slice parameter
    std::vector<SmallVector<uint64_t, 4>> dims(function->arg_size()); // and the dimensions of the ones in memory
    std::vector<Type *> parameters;
    bool any = false;
    for (unsigned i = 0; i < function->arg_size(); i++) {
        Type *type = function->getArg(i)->getType();
        auto *pair = dyn_cast<ArrayType>(type);
        if (pair != nullptr && pair->getNumElements() == 2 && pair->getElementType()->isIntegerTy(64))
            elements[i] = slice_element_bytes(types[i + 1], aligns[i]);
        else if (type->isPointerTy()) { // in memory
            SliceLayout layout = slice_layout(types[i + 1]);
            if (layout.runtime > 1) elements[i] = layout.element, aligns[i] = layout.align, dims[i] = layout.dims;
            counts[i] = layout.runtime;
        }
        if (elements[i] == 0) {
            parameters.push_back(type);
            continue;
        }
        any = true;
        parameters.push_back(PointerType::getUnqual(context));
        for (unsigned count = 0; count < counts[i]; count++)
            parameters.push_back(Type::getInt64Ty(context));
    }
    if (!any) return function;
    // more parameters for what the solver is not given as a product (see assume_row_products)
    unsigned extras = 0, rows = 0;
    for (unsigned i = 0; i < function->arg_size(); i++) {
        if (counts[i] > 1 && elements[i] != 0) extras += counts[i] - 1, rows++;
    }
    if (rows != 0) { // and one for each product the body may take of a count with something else
        for (Instruction &instruction : instructions(*function)) {
            if (instruction.getOpcode() == Instruction::Mul && !isa<ConstantInt>(instruction.getOperand(0)) &&
                !isa<ConstantInt>(instruction.getOperand(1)))
                extras++;
        }
    }
    for (unsigned extra = 0; extra < extras; extra++)
        parameters.push_back(Type::getInt64Ty(context));
    auto *type = FunctionType::get(function->getReturnType(), parameters, function->isVarArg());
    Function *unit = Function::Create(type, function->getLinkage(), "", function->getParent());
    unit->copyAttributesFrom(function);
    SmallVector<AttributeSet, 8> attributes;
    for (unsigned i = 0; i < function->arg_size(); i++) {
        attributes.push_back(function->getAttributes().getParamAttrs(i));
        if (elements[i] == 0) continue;
        attributes.back() = AttributeSet();
        for (unsigned count = 0; count < counts[i]; count++)
            attributes.push_back(AttributeSet());
    }
    for (unsigned extra = 0; extra < extras; extra++)
        attributes.push_back(AttributeSet());
    unit->setAttributes(AttributeList::get(context, function->getAttributes().getFnAttrs(),
                                           function->getAttributes().getRetAttrs(), attributes));
    unit->copyMetadata(function, 0);
    unit->setSubprogram(subprogram);
    function->setSubprogram(nullptr);
    std::string name = function->getName().str();
    function->setName("smith.unsplit." + name);
    unit->setName(name);
    unit->splice(unit->end(), function);
    BasicBlock &entry = unit->getEntryBlock();
    IRBuilder<> builder(&*entry.getFirstInsertionPt());
    unsigned next = 0, products = unit->arg_size() - extras;
    std::vector<Value *> slices;
    for (unsigned i = 0; i < function->arg_size(); i++, next++) {
        Argument *old = function->getArg(i);
        if (elements[i] == 0) {
            old->replaceAllUsesWith(unit->getArg(next));
            unit->getArg(next)->takeName(old);
            continue;
        }
        Argument *data = unit->getArg(next);
        std::vector<Value *> sizes;
        for (unsigned count = 0; count < counts[i]; count++)
            sizes.push_back(unit->getArg(++next));
        slices.push_back(data);
        data->setName("xs.data");
        for (Value *size : sizes)
            size->setName("xs.count");
        Type *i64 = Type::getInt64Ty(context);
        if (counts[i] == 1) {
            Value *count = sizes[0];
            for (User *user : make_early_inc_range(old->users())) {
                auto *extract = dyn_cast<ExtractValueInst>(user);
                if (extract == nullptr || extract->getNumIndices() != 1) continue;
                if (extract->getIndices()[0] == 1) {
                    extract->replaceAllUsesWith(count);
                    continue;
                }
                for (User *use : make_early_inc_range(extract->users())) {
                    if (isa<IntToPtrInst>(use)) use->replaceAllUsesWith(data);
                }
            }
            Value *address = builder.CreatePtrToInt(data, i64);
            Value *pair = builder.CreateInsertValue(PoisonValue::get(old->getType()), address, 0);
            old->replaceAllUsesWith(builder.CreateInsertValue(pair, count, 1));
        } else { // the struct in a slot of the unit's own
            AllocaInst *slot = builder.CreateAlloca(ArrayType::get(i64, counts[i] + 1));
            slot->setAlignment(Align(8));
            old->replaceAllUsesWith(slot);
            forward_slot_loads(slot, data, sizes);
            builder.CreateAlignedStore(data, slot, Align(8));
            for (unsigned count = 0; count < counts[i]; count++)
                builder.CreateAlignedStore(sizes[count], builder.CreateConstInBoundsGEP1_64(i64, slot, count + 1),
                                           Align(8));
        }
        Value *bytes = nullptr;
        if (counts[i] == 1) {
            // no object is 2^40 bytes; the bound keeps the solver's sizes small
            builder.CreateAssumption(builder.CreateICmpULE(sizes[0], ConstantInt::get(i64, 1ull << 32)));
            bytes = builder.CreateMul(sizes[0], ConstantInt::get(i64, elements[i]), "", true);
        } else {
            std::vector<Value *> given;
            for (unsigned count = 1; count < counts[i]; count++)
                given.push_back(unit->getArg(products++));
            bytes = builder.CreateMul(assume_row_products(builder, unit, dims[i], sizes, given, products),
                                      ConstantInt::get(i64, elements[i]));
        }
        SmallVector<Value *, 2> sized = {data, bytes};
        SmallVector<Value *, 2> aligned = {data, ConstantInt::get(i64, aligns[i])};
        builder.CreateAssumption({OperandBundleDef("dereferenceable", sized), OperandBundleDef("align", aligned)});
    }
    relax_slice_arithmetic(slices);
    for (unsigned i = 0, slice = 0; i < function->arg_size(); i++) {
        if (elements[i] == 0) continue;
        Value *data = slices[slice++];
        if (slice_is_const(types[i + 1]) || !isModSet(unit->getMemoryEffects().getModRef(IRMemLocation::ArgMem)))
            continue;
        mark_slice_writable(cast<Argument>(data));
    }
    return unit;
}

// A pointer a function returns under an smith_ensures is a whole object of its type or null: proved at each return of
// the function (a return of anything else is undefined behaviour there), assumed by its callers, which still need an
// smith_ensures(result != nullptr) to load through it. A trusted function is assumed, never proved, so it is not
// marked.
void mark_returned_pointer(Function &function) {
    DISubprogram *subprogram = function.getSubprogram();
    if (subprogram == nullptr || subprogram->getType() == nullptr || !function.getReturnType()->isPointerTy()) return;
    auto types = subprogram->getType()->getTypeArray();
    if (types.size() == 0 || !has_own_contract(function, Contract::Ensures)) return;
    uint64_t bytes = pointee_bytes(types[0]);
    if (bytes == 0) return;
    uint64_t align = pointee_alignment(types[0]);
    function.addRetAttr(Attribute::getWithDereferenceableOrNullBytes(function.getContext(), bytes));
    function.addRetAttr(Attribute::getWithAlignment(function.getContext(), Align(align)));
    function.addFnAttr("smith.returned", std::to_string(bytes) + ":" + std::to_string(align)); // for the callers
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
    unsigned hidden = function.arg_size() > 0 && function.getArg(0)->hasStructRetAttr() ? 1 : 0;
    if (types.size() != function.arg_size() + 1 - hidden) return;
    for (unsigned i = hidden; i < function.arg_size(); i++) {
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
        if (stored && i >= hidden && !points_to_const(types[i - hidden + 1])) argument->addAttr(Attribute::Writable);
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
                recursive.count(callee) != 0 || callee->isVarArg() || contract_name(callee->getName()) ||
                is_assert_failure(call))
                continue; // a variable argument list cannot be inlined into a function that has none; a contract and
                          // the failure of a check (smith_failure, whose body a linked trace library has) are points
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

namespace {

// A function of a smith library the program links: read so that its callers use its contract (a trusted one is assumed,
// one with an smith_ensures is used through it, any other is part of its caller), and never proved here, since the
// library's own proof covers it. An edge export (smith::io's) loses its body: it stays the edge, as when only its
// header is read.
void mark_library(Module &module) {
    for (Function &function : module) {
        if (function.isDeclaration()) continue;
        if (function.hasFnAttribute("smith.edge")) {
            function.deleteBody();
            function.setMetadata("smith.id", nullptr);
            function.setSubprogram(nullptr);
            continue;
        }
        function.addFnAttr("smith.library");
    }
}

} // namespace

namespace {
std::string source_name(const Function &function);
std::string subprogram_name(const DISubprogram *subprogram);
void hide_loop_tests(Function &function); // below, with the loops' own code
void check_restrict_calls(Module &module);
} // namespace

bool program_load(Program &program, const std::vector<std::string> &bitcode, std::string &error, size_t libraries) {
    Clock::time_point begin = Clock::now();
    program.context = std::make_unique<LLVMContext>();
    unsigned counter = 0;
    for (size_t index = 0; index < bitcode.size(); index++) {
        const std::string &path = bitcode[index];
        SMDiagnostic diagnostic;
        std::unique_ptr<Module> module = parseIRFile(path, diagnostic, *program.context);
        if (module == nullptr) {
            error = "cannot read " + path + ": " + diagnostic.getMessage().str();
            return false;
        }
        annotate(*module, counter);
        if (index >= bitcode.size() - libraries) mark_library(*module);
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
        if (!function.hasFnAttribute("smith.library") && !function.hasFnAttribute("smith.snapshot") &&
            !function.hasFnAttribute(Attribute::AlwaysInline)) {
            program.coverage.total++;
            if (is_unit(function) || is_trusted(function)) {
                program.coverage.with_contract++;
            } else {
                Without without;
                without.name = source_name(function);
                if (const DISubprogram *subprogram = function.getSubprogram()) {
                    without.file = subprogram->getFilename().str();
                    without.line = subprogram->getLine();
                }
                program.coverage.without.push_back(without);
            }
        }
        if (is_unit(function)) {
            own++;
        } else if (is_trusted(function) && !function.hasFnAttribute("smith.library")) {
            trusted++;
        }
    }
    std::stable_sort(
        program.coverage.without.begin(), program.coverage.without.end(),
        [](const Without &a, const Without &b) { return std::tie(a.file, a.line) < std::tie(b.file, b.line); });
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
        // The marker of a range has an empty body, which LLVM would find to return and touch nothing, and delete the
        // calls.
        if (function.getName() == "smith_contract_may_write" && !function.isDeclaration()) function.deleteBody();
        if (contract_name(function.getName())) {
            function.setMemoryEffects(MemoryEffects::none());
            function.addFnAttr(Attribute::NoUnwind);
            function.addFnAttr(Attribute::NoFree);
        }
        // Neither frees anything, but LLVM's intrinsics do not say it, so no function with a contract or a UBSan check
        // is inferred nofree, and Alive2 then takes a call of one as possibly freeing the objects it was given: every
        // load of one after the call is undefined behaviour (issue #62).
        if (function.getIntrinsicID() == Intrinsic::ubsantrap || function.getIntrinsicID() == Intrinsic::trap)
            function.addFnAttr(Attribute::NoFree);
    }
    // A generic's instance is `weak`, which another definition may replace at link time, so LLVM infers nothing from
    // its body (a pure function would be taken to write any memory). The linker has made it the one definition there
    // is: it is an ordinary function now.
    for (Function &function : module) {
        if (function.hasWeakLinkage() && !function.isDeclaration()) function.setLinkage(GlobalValue::ExternalLinkage);
    }
    Analyses analyses;
    for (Function &function : module) {
        if (function.isDeclaration()) continue;
        hide_loop_tests(function);
        freeze_locals(function);
        if (!analyses.run_function(function, light_pipeline, error)) return false;
        mark_pointer_parameters(function);
        if (!is_trusted(function)) mark_returned_pointer(function);
    }

    check_restrict_calls(module); // issue #65: before inlining loses what the callee says is restrict
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
    // Only what a unit reaches is flattened: a module of 300k instructions, with a handful of units in it, would grow
    // to memory's end if every function in it were inlined into its callers. A trusted function's body is never read,
    // so nothing past it is reached through it.
    DenseSet<const Function *> reached;
    SmallVector<const Function *, 64> work;
    for (const Function &function : module) {
        if (is_unit(function) && reached.insert(&function).second) work.push_back(&function);
    }
    while (!work.empty()) {
        const Function *function = work.pop_back_val();
        if (is_trusted(*function)) continue;
        for (const Instruction &instruction : instructions(*function)) {
            const auto *call = dyn_cast<CallBase>(&instruction);
            if (call == nullptr) continue;
            const Function *callee = call->getCalledFunction();
            if (callee != nullptr && !callee->isDeclaration() && !contract_name(callee->getName()) &&
                !is_assert_failure(call) && reached.insert(callee).second)
                work.push_back(callee);
        }
    }
    llvm::erase_if(order, [&](Function *function) { return reached.count(function) == 0; });
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
        // It frees nothing either: a store through a pointer parameter after the call must not be undefined behaviour.
        function.addFnAttr(Attribute::NoFree);
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
        if (function.isDeclaration() || is_snapshot(function) || function.hasFnAttribute("smith.library") ||
            !is_trusted(function))
            continue;
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
        result += " (in " + subprogram_name(instruction->getDebugLoc()->getScope()->getSubprogram()) + ")";
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
        // A range the function may write is read by a caller before the call, as its smith_requires is.
        if (contract_of(&instruction) == Contract::Requires || contract_of(&instruction) == Contract::Modifies)
            requires_roots.push_back(&instruction);
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
    // smith_modifies (issue #54): the ranges this function names (pointer, size) and the writes to prove inside them.
    bool framed = false;
    std::vector<std::pair<Value *, Value *>> ranges;
    struct Write {
        WeakTrackingVH at; // the instruction that writes, or the call
        Value *address;
        Value *size; // an i64
    };
    std::vector<Write> writes;

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

// The name of a function as the source writes it. A generic's instance is named by clang with C's types
// (`clamp<int>`); in smith it is written with the sized types (`clamp<i32>`), so the arguments of the instance are.
std::string as_written(const std::string &name) {
    static const std::pair<const char *, const char *> types[] = {
        {"unsigned long long", "u64"},
        {"long long", "i64"},
        {"unsigned long", "usize"},
        {"unsigned short", "u16"},
        {"unsigned char", "u8"},
        {"unsigned int", "u32"},
        {"signed char", "i8"},
        {"unsigned", "u32"},
        {"long", "isize"},
        {"short", "i16"},
        {"int", "i32"},
        {"float", "f32"},
        {"double", "f64"},
        {"_Float16", "f16"},
    };
    size_t open = name.find('<');
    if (open == std::string::npos) return name;
    std::string result = name.substr(0, open);
    for (size_t at = open; at < name.size();) { // bounded by the name
        auto is_word = [&](size_t index) {
            return index < name.size() && (isalnum(static_cast<unsigned char>(name[index])) != 0 || name[index] == '_');
        };
        bool replaced = false;
        if (!is_word(at - 1)) {
            for (const auto &[written, sized] : types) {
                size_t length = strlen(written);
                if (name.compare(at, length, written) == 0 && !is_word(at + length)) {
                    result += sized;
                    at += length;
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced) result += name[at++];
    }
    return result;
}

std::string subprogram_name(const DISubprogram *subprogram) {
    return as_written(subprogram->getName().str());
}

std::string source_name(const Function &function) {
    if (const DISubprogram *subprogram = function.getSubprogram()) return subprogram_name(subprogram);
    return function.getName().str();
}

// The function whose body wrote the call, as the source names it: the one a trusted marker would go in.
std::string writer_of(const UnitContext &context, const Instruction *call) {
    if (call != nullptr && call->getDebugLoc()) {
        if (const DISubprogram *subprogram = call->getDebugLoc()->getScope()->getSubprogram())
            return subprogram_name(subprogram);
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
                         Align align, bool unaligned_words = false) {
    IRBuilder<> builder(at);
    builder.SetCurrentDebugLocation(location);
    unsigned widest = align.value() >= 8 || unaligned_words ? 8 : static_cast<unsigned>(align.value());
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

// What the callee was proved to return (see mark_returned_pointer) is assumed of the call's value: an assumption of
// what it points to when it is not null, or of an object of its own when it is null, so that it holds for both. The
// callee's return attributes are its obligation, so a caller's module does not carry them: on a call they would be a
// check of the unknown value the callee returns.
void assume_returned_pointer(CallBase &call, const Function &callee) {
    Attribute attribute = callee.getFnAttribute("smith.returned");
    if (!attribute.isValid() || call.getType() != callee.getReturnType()) return;
    StringRef text = attribute.getValueAsString();
    uint64_t size = 0, align = 1;
    text.split(':').first.getAsInteger(10, size);
    text.split(':').second.getAsInteger(10, align);
    LLVMContext &context = call.getContext();
    Module *module = call.getModule();
    Type *object_type = ArrayType::get(Type::getInt8Ty(context), size);
    // For null the assumption is about an object that is there, so that it holds for null and for what is valid.
    std::string name = "smith.valid." + std::to_string(size) + "." + std::to_string(align);
    auto *object = module->getGlobalVariable(name, true);
    if (object == nullptr)
        object = new GlobalVariable(*module, object_type, false, GlobalValue::PrivateLinkage,
                                    ConstantAggregateZero::get(object_type), name);
    object->setAlignment(Align(align));
    IRBuilder<> builder(call.getNextNode());
    Value *safe = builder.CreateSelect(builder.CreateIsNull(&call), object, &call);
    SmallVector<Value *, 2> inputs = {safe, ConstantInt::get(Type::getInt64Ty(context), size)};
    SmallVector<Value *, 2> aligned = {safe, ConstantInt::get(Type::getInt64Ty(context), align)};
    builder.CreateAssumption({OperandBundleDef("dereferenceable", inputs), OperandBundleDef("align", aligned)});
    call.setMetadata("smith.valid_return",
                     MDNode::get(context, {})); // what it points to holds values, as a parameter's
}

// ---- smith_modifies (issue #54) ----

using Range = std::pair<Value *, Value *>; // a pointer and a size in bytes (an i64)

// The ranges a callee names: its smith_modifies calls, inlined before the call with its smith_requires. They are read
// and removed. Returns whether it names any (the call with no range, `modifies()`, names the empty one).
bool take_ranges(UnitContext &context, int call_site, std::vector<Range> &ranges) {
    SmallVector<CallBase *, 4> found;
    for (Instruction &instruction : instructions(context.function)) {
        MDNode *site = instruction.getMetadata("smith.callsite");
        if (contract_of(&instruction) == Contract::Modifies && site != nullptr &&
            static_cast<int>(id_of(site)) == call_site)
            found.push_back(cast<CallBase>(&instruction));
    }
    for (CallBase *call : found) {
        auto *size = dyn_cast<ConstantInt>(call->getArgOperand(1));
        if (size == nullptr || !size->isZero()) ranges.emplace_back(call->getArgOperand(0), call->getArgOperand(1));
        call->eraseFromParent();
    }
    return !found.empty();
}

bool may_write_pointee(const Function &callee, unsigned index) {
    return !callee.hasParamAttribute(index, Attribute::ReadOnly) &&
           !callee.hasParamAttribute(index, Attribute::ReadNone) && callee.getParamDereferenceableBytes(index) != 0;
}

// The object a pointer points into, as far as its size is known: an object of the function, a global, or a parameter
// with the bytes it is `dereferenceable` for.
bool object_region(UnitContext &context, Value *pointer, Value *&base, uint64_t &bytes, Align &align) {
    const DataLayout &layout = context.module.getDataLayout();
    Value *object = const_cast<Value *>(getUnderlyingObject(pointer));
    bytes = 0;
    if (auto *local = dyn_cast<AllocaInst>(object)) {
        std::optional<TypeSize> size = local->getAllocationSize(layout);
        if (size && !size->isScalable()) bytes = size->getFixedValue();
        align = local->getAlign();
    } else if (auto *global = dyn_cast<GlobalVariable>(object)) {
        if (!global->isConstant() && global->getValueType()->isSized()) {
            bytes = layout.getTypeAllocSize(global->getValueType()).getFixedValue();
            align = global->getAlign().valueOrOne();
        }
    } else if (auto *argument = dyn_cast<Argument>(object)) {
        bytes = argument->getDereferenceableBytes();
        align = argument->getParamAlign().valueOrOne();
    }
    base = object;
    return bytes != 0;
}

// A callee that names its ranges writes nothing else (it was proved to, or is trusted to), so the call itself, which
// only stands for the callee's effect, is given a scratch object in place of each pointer it may write through: Alive2
// would let it change the whole object the pointer points into. What the callee changed is then made arbitrary by hand,
// range by range (havoc_ranges), and nothing else is touched.
void redirect_arguments(UnitContext &context, CallBase &call, Function &callee, IRBuilder<> &entry,
                        SmallPtrSetImpl<const Value *> &copies) {
    for (unsigned i = 0; i < call.arg_size() && i < callee.arg_size(); i++) {
        Value *pointer = call.getArgOperand(i);
        if (!pointer->getType()->isPointerTy() || copies.contains(pointer) || !may_write_pointee(callee, i)) continue;
        uint64_t bytes = callee.getParamDereferenceableBytes(i);
        auto *scratch = entry.CreateAlloca(ArrayType::get(Type::getInt8Ty(context.function.getContext()), bytes));
        scratch->setAlignment(callee.getParamAlign(i).valueOrOne());
        call.setArgOperand(i, scratch);
        copies.insert(scratch);
    }
}

void mark_frame_ok(Instruction *from, Instruction *to) { // [from, to) were written by the prover, not the function
    for (Instruction *at = from; at != nullptr && at != to; at = at->getNextNode())
        at->setMetadata("smith.frame_ok", MDNode::get(at->getContext(), {}));
}

// After the call: each range it may have written holds arbitrary bytes (the bytes of a global of its own that nothing
// initializes, a different one at every call), and the callee's smith_ensures is assumed of them. Returns the last
// instruction it inserted.
Instruction *havoc_ranges(UnitContext &context, CallBase &call, Instruction *last, ArrayRef<Range> ranges) {
    const DataLayout &layout = context.module.getDataLayout();
    LLVMContext &llvm_context = context.module.getContext();
    Type *i64 = Type::getInt64Ty(llvm_context);
    for (const Range &range : ranges) {
        Instruction *anchor = last->getNextNode();
        Instruction *before = anchor->getPrevNode();
        IRBuilder<> builder(anchor);
        builder.SetCurrentDebugLocation(call.getDebugLoc());
        auto *constant = dyn_cast<ConstantInt>(range.second);
        Value *base = nullptr;
        uint64_t object_bytes = 0;
        Align object_align(1);
        bool known = object_region(context, range.first, base, object_bytes, object_align);
        uint64_t bytes = constant != nullptr ? constant->getZExtValue() : object_bytes;
        if (bytes == 0 || (constant == nullptr && !known)) {
            context.fail(&call, "the size of a range the callee names is not known here",
                         "name a range of a constant size, or of an object whose size the prover knows");
            return last;
        }
        auto *type = ArrayType::get(Type::getInt8Ty(llvm_context), bytes);
        auto *source =
            new GlobalVariable(context.module, type, false, GlobalValue::InternalLinkage,
                               ConstantAggregateZero::get(type), "smith.havoc." + std::to_string(context.havocs++));
        if (constant != nullptr) {
            Align align = range.first->getPointerAlignment(layout);
            source->setAlignment(align);
            copy_object(anchor, call.getDebugLoc(), range.first, source, bytes, align,
                        true); // words, even when unaligned
        } else {
            source->setAlignment(Align(1));
            for (uint64_t offset = 0; offset < bytes; offset++) {
                Value *to = builder.CreateConstInBoundsGEP1_64(builder.getInt8Ty(), base, offset);
                Value *from = builder.CreateConstInBoundsGEP1_64(builder.getInt8Ty(), source, offset);
                Value *distance =
                    builder.CreateSub(builder.CreatePtrToInt(to, i64), builder.CreatePtrToInt(range.first, i64));
                Value *in = builder.CreateICmpULT(distance, range.second);
                Value *kept = builder.CreateAlignedLoad(builder.getInt8Ty(), to, Align(1));
                Value *arbitrary = builder.CreateAlignedLoad(builder.getInt8Ty(), from, Align(1));
                builder.CreateAlignedStore(builder.CreateSelect(in, arbitrary, kept), to, Align(1));
            }
        }
        context.function.setMemoryEffects(MemoryEffects::unknown()); // it now reads a global, whatever it inferred
        if (auto *argument = dyn_cast<Argument>(getUnderlyingObject(range.first)); // a slice of the function's own
            argument != nullptr && argument->getType()->isPointerTy() && argument->getName() == "xs.data")
            mark_slice_writable(argument);
        mark_frame_ok(before->getNextNode(), anchor);
        last = anchor->getPrevNode();
    }
    return last;
}

// What `call` may write, for the check of a function that has a smith_modifies of its own (see instrument_writes).
void note_call_writes(UnitContext &context, CallBase &call, Function &callee, bool names_ranges,
                      ArrayRef<Range> ranges) {
    LLVMContext &llvm_context = context.module.getContext();
    Type *i64 = Type::getInt64Ty(llvm_context);
    if (names_ranges) {
        for (const Range &range : ranges)
            context.writes.push_back({&call, range.first, range.second});
        return;
    }
    for (unsigned i = 0; i < call.arg_size() && i < callee.arg_size(); i++) {
        if (!call.getArgOperand(i)->getType()->isPointerTy() || !may_write_pointee(callee, i)) continue;
        context.writes.push_back(
            {&call, call.getArgOperand(i), ConstantInt::get(i64, callee.getParamDereferenceableBytes(i))});
    }
}

// What LLVM found in a body that makes it write memory the function was not given: the first store or call that may
// write something other than a local, as the source names it.
std::string why_it_writes(const Function &callee) {
    for (const Instruction &instruction : instructions(callee)) {
        if (!instruction.mayWriteToMemory()) continue;
        std::string what;
        if (const auto *store = dyn_cast<StoreInst>(&instruction)) {
            const Value *object = getUnderlyingObject(store->getPointerOperand());
            if (isa<AllocaInst>(object)) continue;
            what = isa<GlobalVariable>(object) ? "a store to the global " + object->getName().str()
                                               : "a store through a pointer it was not given";
        } else if (const auto *call = dyn_cast<CallBase>(&instruction)) {
            if (call->doesNotAccessMemory() || call->onlyReadsMemory()) continue;
            Function *called = call->getCalledFunction();
            if (called != nullptr && called->isIntrinsic()) continue; // a trap writes nothing the caller can see
            what = called == nullptr ? "a call through a function pointer" : "a call of " + source_name(*called);
        } else {
            what = "an instruction that writes memory";
        }
        std::string line;
        if (instruction.getDebugLoc() && instruction.getDebugLoc().getLine() != 0)
            line = " at line " + std::to_string(instruction.getDebugLoc().getLine());
        return "LLVM found " + what + line + " in the body";
    }
    return "LLVM could not infer what the body writes, so it takes it as any memory";
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
    std::vector<Range> ranges;
    bool names_ranges = take_ranges(context, call_site, ranges);
    if (context.framed) note_call_writes(context, call, callee, names_ranges, ranges);
    if (names_ranges) {
        redirect_arguments(context, call, callee, entry, copies);
        // The call stands for the callee's effect, which is its ranges (made arbitrary by hand below): it writes
        // nothing else. A slice goes in as two integers, so LLVM infers that the callee may write any memory.
        if (call.getType()->isVoidTy() && isModSet(callee.getMemoryEffects().getModRef(IRMemLocation::Other)))
            call.setMemoryEffects(MemoryEffects::none());
    }
    if (!(names_ranges && call.getType()->isVoidTy()) &&
        isModSet(callee.getMemoryEffects().getModRef(IRMemLocation::Other))) {
        context.unit.notes.push_back("note: " + site.file + ":" + std::to_string(site.line) + ": " +
                                     source_name(callee) + " may write memory it was not given (" +
                                     why_it_writes(callee) +
                                     "); what the caller knows about that memory is lost across the call");
    }
    if (slices.ensures_slice == nullptr && names_ranges) {
        Instruction *after = havoc_locals(context, call, callee, &call, copies);
        havoc_ranges(context, call, after, ranges);
    }
    if (slices.ensures_slice != nullptr) {
        SmallVector<Value *, 8> with_result(arguments.begin(), arguments.end());
        if (slices.has_result) with_result.push_back(&call);
        with_result.append(olds.begin(), olds.end());
        Instruction *after = havoc_locals(context, call, callee, &call, copies);
        if (names_ranges) after = havoc_ranges(context, call, after, ranges);
        auto *ensures_call =
            CallInst::Create(slices.ensures_slice, with_result, "", after->getNextNode()->getIterator());
        ensures_call->setDebugLoc(call.getDebugLoc());
        if (!inline_slice(context, *ensures_call, call_site)) return false;
    }
    assume_returned_pointer(call, callee);
    call.setMetadata("smith.resolved", MDNode::get(function.getContext(), {}));
    return true;
}

// `__memcpy_chk(dst, src, len, dst_size)` is what Apple's headers make of memcpy when the destination's size is known
// (it has no body here). It is memcpy: the check against dst_size only aborts where memcpy would be out of bounds,
// which the proof of the memcpy reports as the undefined behaviour it is.
void lower_memcpy_chk(Function &function) {
    std::vector<CallBase *> calls;
    for (Instruction &instruction : instructions(function)) {
        auto *call = dyn_cast<CallInst>(&instruction);
        Function *callee = call != nullptr ? direct_callee(call) : nullptr;
        if (callee != nullptr && callee->getName() == "__memcpy_chk" && call->arg_size() == 4) calls.push_back(call);
    }
    for (CallBase *call : calls) {
        IRBuilder<> builder(call);
        Value *destination = call->getArgOperand(0);
        builder.CreateMemCpy(destination, MaybeAlign(1), call->getArgOperand(1), MaybeAlign(1), call->getArgOperand(2));
        call->replaceAllUsesWith(destination);
        call->eraseFromParent();
    }
}

// Every call in the function is inlined, replaced by its contract, or an Unprovable.

// The check of a function with a smith_modifies of its own (issue #54): each write it makes lands inside one of the
// ranges it names, or in an object of its own. A proof point `smith.write_check(condition)` before the write says so,
// and the report names the write's line. A call of a callee is a write of what the callee names (or, with no
// smith_modifies there, of every object it may write through the pointers it was given); the restore after such a call
// is the prover's own and is not a write.
void instrument_writes(UnitContext &context) {
    Function &function = context.function;
    const DataLayout &layout = context.module.getDataLayout();
    LLVMContext &llvm_context = context.module.getContext();
    Type *i64 = Type::getInt64Ty(llvm_context);
    for (Instruction &instruction : instructions(function)) {
        if (instruction.getMetadata("smith.frame_ok") != nullptr) continue;
        auto add = [&](Value *address, Value *size) {
            if (isa<AllocaInst>(getUnderlyingObject(address))) return; // an object of the function's own
            context.writes.push_back({&instruction, address, size});
        };
        if (auto *store = dyn_cast<StoreInst>(&instruction)) {
            add(store->getPointerOperand(),
                ConstantInt::get(i64, layout.getTypeStoreSize(store->getValueOperand()->getType()).getFixedValue()));
        } else if (auto *memory = dyn_cast<MemIntrinsic>(&instruction)) {
            add(memory->getRawDest(), memory->getLength()->getType() == i64
                                          ? memory->getLength()
                                          : IRBuilder<>(memory).CreateZExtOrTrunc(memory->getLength(), i64));
        } else if (auto *rmw = dyn_cast<AtomicRMWInst>(&instruction)) {
            add(rmw->getPointerOperand(),
                ConstantInt::get(i64, layout.getTypeStoreSize(rmw->getValOperand()->getType()).getFixedValue()));
        } else if (auto *exchange = dyn_cast<AtomicCmpXchgInst>(&instruction)) {
            add(exchange->getPointerOperand(),
                ConstantInt::get(i64,
                                 layout.getTypeStoreSize(exchange->getNewValOperand()->getType()).getFixedValue()));
        } else if (auto *call = dyn_cast<CallBase>(&instruction)) {
            Function *callee = direct_callee(call);
            if (callee == nullptr || callee->isIntrinsic() || contract_of(call) != Contract::None ||
                callee->getName() == "smith_point" || callee->getName() == "smith.opaque" ||
                callee->getName() == "smith.write_check" || callee->hasFnAttribute(Attribute::NoReturn) ||
                callee->onlyReadsMemory() || is_modular(*callee))
                continue;
            StringRef name = callee->getName();
            if ((name == "memcpy" || name == "memmove" || name == "memset") && call->arg_size() == 3) {
                add(call->getArgOperand(0), call->getArgOperand(2));
                continue;
            }
            for (unsigned i = 0; i < call->arg_size() && i < callee->arg_size(); i++) {
                if (!call->getArgOperand(i)->getType()->isPointerTy() || !may_write_pointee(*callee, i)) continue;
                add(call->getArgOperand(i), ConstantInt::get(i64, callee->getParamDereferenceableBytes(i)));
            }
        }
    }
    DominatorTree dominators(function);
    FunctionCallee check = context.module.getOrInsertFunction(
        "smith.write_check", FunctionType::get(Type::getVoidTy(llvm_context), {Type::getInt1Ty(llvm_context)}, false));
    if (auto *declaration = dyn_cast<Function>(check.getCallee())) {
        declaration->setDoesNotAccessMemory();
        declaration
            ->setDoesNotThrow(); // not willreturn: the clean-up would delete a call that does nothing and returns
    }
    for (UnitContext::Write &write : context.writes) {
        auto *at = dyn_cast_or_null<Instruction>(write.at);
        if (at == nullptr) continue;
        IRBuilder<> builder(at);
        builder.SetCurrentDebugLocation(at->getDebugLoc());
        Value *inside = builder.getFalse();
        Value *address = builder.CreatePtrToInt(write.address, i64);
        for (const auto &[low, size] : context.ranges) {
            for (Value *value : {low, size}) {
                auto *definition = dyn_cast<Instruction>(value);
                if (definition != nullptr && !dominators.dominates(definition, at)) {
                    context.fail(at, "smith_modifies names a range after the function's first write",
                                 "put every smith_modifies at the start of the function, after its smith_requires");
                    return;
                }
            }
            Value *fits =
                builder.CreateAnd(builder.CreateICmpUGE(size, write.size),
                                  builder.CreateICmpULE(builder.CreateSub(address, builder.CreatePtrToInt(low, i64)),
                                                        builder.CreateSub(size, write.size)));
            inside = builder.CreateOr(inside, fits);
        }
        builder.CreateCall(check, {inside});
    }
}

// ---- restrict at the call (issue #65) ----

// The type a pointer type points to, without typedefs and qualifiers.
const DIType *pointee_type(const DIType *type) {
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
    auto *pointer = dyn_cast_or_null<DIDerivedType>(strip(type));
    if (pointer == nullptr || pointer->getTag() != dwarf::DW_TAG_pointer_type) return nullptr;
    return strip(pointer->getBaseType());
}

// The source's type of the parameter `index` of `callee` (the sret result, which the source never names, not counted).
const DIType *parameter_type(const Function &callee, unsigned index) {
    const DISubprogram *subprogram = callee.getSubprogram();
    if (subprogram == nullptr || subprogram->getType() == nullptr) return nullptr;
    auto types = subprogram->getType()->getTypeArray();
    unsigned hidden = callee.arg_size() > 0 && callee.getArg(0)->hasStructRetAttr() ? 1 : 0;
    if (index < hidden || types.size() != callee.arg_size() + 1 - hidden) return nullptr;
    return types[index - hidden + 1];
}

std::string parameter_name(const Function &callee, unsigned index) {
    if (const DISubprogram *subprogram = callee.getSubprogram()) {
        for (const MDNode *node : subprogram->getRetainedNodes()) {
            auto *variable = dyn_cast<DILocalVariable>(node);
            if (variable != nullptr && variable->getArg() == index + 1) return variable->getName().str();
        }
    }
    for (const Instruction &instruction : instructions(callee)) {
        for (const DbgRecord &record : instruction.getDbgRecordRange()) {
            auto *variable = dyn_cast<DbgVariableRecord>(&record);
            if (variable != nullptr && variable->getVariable()->getArg() == index + 1 &&
                variable->getVariable()->getScope()->getSubprogram() == callee.getSubprogram())
                return variable->getVariable()->getName().str();
        }
    }
    return "argument " + std::to_string(index + 1);
}

// Whether the body of `callee` uses the pointer parameter `index` as an array: arithmetic on it, an index that is not
// the object itself, or a use the check cannot follow. A struct pointer handed on to another function stays one object.
bool used_as_array(const Function &callee, unsigned index, bool is_struct) {
    SmallVector<const Value *, 8> work{callee.getArg(index)};
    SmallPtrSet<const Value *, 8> seen, members; // members: addresses inside the object, whatever is done with them
    while (!work.empty()) {
        const Value *value = work.pop_back_val();
        if (!seen.insert(value).second) continue;
        bool inside = members.contains(value);
        for (const User *user : value->users()) {
            if (auto *load = dyn_cast<LoadInst>(user)) {
                if (isa<AllocaInst>(value) && load->getType()->isPointerTy()) work.push_back(load);
            } else if (auto *store = dyn_cast<StoreInst>(user)) {
                if (store->getValueOperand() != value) continue; // written through it
                auto *slot = dyn_cast<AllocaInst>(store->getPointerOperand());
                if (slot == nullptr) {
                    if (inside) continue;
                    return true;
                }
                work.push_back(slot);
            } else if (auto *gep = dyn_cast<GetElementPtrInst>(user)) {
                auto *first = dyn_cast<ConstantInt>(gep->getOperand(1));
                if (!inside && (first == nullptr || !first->isZero() || !is_struct)) return true;
                members.insert(gep);
                work.push_back(gep);
            } else if (auto *call = dyn_cast<CallBase>(user)) {
                const Function *called = call->getCalledFunction();
                if (!inside && !is_struct && (called == nullptr || !called->isIntrinsic())) return true;
            } else if (!inside && (isa<AllocaInst>(value) || isa<PtrToIntInst>(user) || !isa<CastInst>(user))) {
                return true;
            } else if (isa<CastInst>(user)) {
                if (inside) members.insert(user);
                work.push_back(user);
            }
        }
    }
    return false;
}

// The bytes the callee may reach through its pointer parameter `index`: one object of the type it points to. 0 when
// that is not what the body does (an array, or a type with no size), with the reason in `why`.
uint64_t accessed_bytes(const Function &callee, unsigned index, std::string &why) {
    const DIType *type = pointee_type(parameter_type(callee, index));
    uint64_t bytes = type != nullptr ? (type->getSizeInBits() + 7) / 8 : callee.getParamDereferenceableBytes(index);
    if (bytes == 0) {
        why = "its type has no size the prover knows";
        return 0;
    }
    bool is_struct = isa_and_nonnull<DICompositeType>(type) && type->getTag() != dwarf::DW_TAG_array_type;
    if (used_as_array(callee, index, is_struct)) {
        why = "the function uses it as an array";
        return 0;
    }
    return bytes;
}

// Whether the parameter is a `const T *`: the attribute (inferred late) or the source's type.
bool only_read(const Function &callee, unsigned index) {
    if (callee.hasParamAttribute(index, Attribute::ReadOnly) || callee.hasParamAttribute(index, Attribute::ReadNone))
        return true;
    auto *derived = dyn_cast_or_null<DIDerivedType>(parameter_type(callee, index));
    while (derived != nullptr && derived->getTag() != dwarf::DW_TAG_pointer_type) {
        if (derived->getTag() != dwarf::DW_TAG_typedef && derived->getTag() != dwarf::DW_TAG_restrict_type &&
            derived->getTag() != dwarf::DW_TAG_const_type && derived->getTag() != dwarf::DW_TAG_volatile_type)
            return false;
        derived = dyn_cast_or_null<DIDerivedType>(derived->getBaseType());
    }
    if (derived == nullptr) return false;
    for (auto *base = dyn_cast_or_null<DIDerivedType>(derived->getBaseType()); base != nullptr;
         base = dyn_cast_or_null<DIDerivedType>(base->getBaseType())) {
        if (base->getTag() == dwarf::DW_TAG_const_type) return true;
        if (base->getTag() != dwarf::DW_TAG_typedef && base->getTag() != dwarf::DW_TAG_volatile_type) return false;
    }
    return false;
}

// A slice parameter (`T xs[:]`, `T xs[:][2]`, `T xs[:][:]`): the bytes one step of each count holds (T times the fixed
// dimensions), 0 when the parameter is not one; `counts` is how many counts it has (more than one is passed in memory:
// its argument points at the struct) and `restricted` whether its data pointer is `restrict` (`T xs[restrict :]`,
// which the debug info keeps as DW_TAG_restrict_type on `data`; only the outermost bracket carries it).
uint64_t slice_parameter(const Function &callee, unsigned index, bool &restricted, unsigned &counts) {
    Type *argument = callee.getArg(index)->getType();
    auto *pair = dyn_cast<ArrayType>(argument);
    const DIType *type = parameter_type(callee, index);
    if (type == nullptr) return 0;
    SliceLayout layout = slice_layout(type);
    counts = layout.runtime;
    bool in_registers = pair != nullptr && pair->getNumElements() == 2 && pair->getElementType()->isIntegerTy(64);
    if (counts == 1 ? !in_registers : !argument->isPointerTy()) return 0;
    uint64_t bytes = layout.element * layout.fixed;
    restricted = false;
    while (auto *derived = dyn_cast_or_null<DIDerivedType>(type)) {
        unsigned tag = derived->getTag();
        if (tag != dwarf::DW_TAG_typedef && tag != dwarf::DW_TAG_const_type && tag != dwarf::DW_TAG_volatile_type)
            break;
        type = derived->getBaseType();
    }
    auto *composite = dyn_cast_or_null<DICompositeType>(type);
    if (bytes == 0 || composite == nullptr) return 0;
    for (const Metadata *member : composite->getElements()) {
        auto *field = dyn_cast<DIDerivedType>(member);
        if (field == nullptr || field->getName() != "data") continue;
        for (const DIType *t = field->getBaseType(); auto *derived = dyn_cast_or_null<DIDerivedType>(t);
             t = derived->getBaseType()) {
            if (derived->getTag() == dwarf::DW_TAG_restrict_type) restricted = true;
            if (derived->getTag() != dwarf::DW_TAG_typedef && derived->getTag() != dwarf::DW_TAG_const_type &&
                derived->getTag() != dwarf::DW_TAG_volatile_type && derived->getTag() != dwarf::DW_TAG_restrict_type)
                break;
        }
    }
    return bytes;
}

// At a call of a function with a `restrict` parameter: the object it reaches through that argument is not the object of
// any other pointer argument (unless both are only read), a point that names both and the line of the call. The size
// of each object is what its type says; where the function uses a pointer as an array the range is not known, which is
// an error at the function's declaration (a call of `smith.restrict_unknown`, which resolve_calls reports). A slice
// (`u8 dst[restrict :]`) carries its count: its object is the range of its pointer and count at the call, so a slice is
// never that error. A trusted function's calls are not checked. Done before anything is inlined, which would lose the
// attribute.
void check_restrict(Module &module, CallBase &call, Function &callee) {
    if (is_trusted(callee)) return;
    LLVMContext &llvm_context = module.getContext();
    Type *i64 = Type::getInt64Ty(llvm_context);
    FunctionCallee check = module.getOrInsertFunction(
        "smith.restrict_check",
        FunctionType::get(Type::getVoidTy(llvm_context),
                          {PointerType::getUnqual(llvm_context), PointerType::getUnqual(llvm_context), i64, i64},
                          false));
    FunctionCallee unknown = module.getOrInsertFunction("smith.restrict_unknown",
                                                        FunctionType::get(Type::getVoidTy(llvm_context), {}, false));
    for (FunctionCallee function : {check, unknown}) {
        if (auto *declaration = dyn_cast<Function>(function.getCallee())) {
            declaration->setDoesNotAccessMemory();
            declaration->setDoesNotThrow();
        }
    }
    auto text = [&](CallBase *marker, const std::string &first, const std::string &second) {
        marker->setMetadata("smith.restrict_text", MDNode::get(llvm_context, {MDString::get(llvm_context, first),
                                                                              MDString::get(llvm_context, second)}));
    };
    unsigned count = std::min<unsigned>(call.arg_size(), callee.arg_size());
    std::vector<uint64_t> slice(count, 0);  // the element size of a slice argument
    std::vector<unsigned> counts(count, 1); // and its counts
    std::vector<bool> restricted(count, false);
    for (unsigned i = 0; i < count; i++) {
        bool by_slice = false;
        slice[i] = slice_parameter(callee, i, by_slice, counts[i]);
        restricted[i] = slice[i] != 0 ? by_slice
                                      : callee.hasParamAttribute(i, Attribute::NoAlias) &&
                                            !callee.hasParamAttribute(i, Attribute::StructRet) &&
                                            call.getArgOperand(i)->getType()->isPointerTy();
        // the hidden result pointer of an sret is noalias in the IR, never a `restrict` of the source
    }
    auto reaches = [&](unsigned i) { return slice[i] != 0 || call.getArgOperand(i)->getType()->isPointerTy(); };
    auto read_only = [&](unsigned i) {
        return slice[i] != 0 ? slice_is_const(parameter_type(callee, i)) : only_read(callee, i);
    };
    std::set<std::pair<unsigned, unsigned>> done;
    for (unsigned i = 0; i < count; i++) {
        if (!restricted[i]) continue;
        for (unsigned j = 0; j < count; j++) {
            if (j == i || callee.hasParamAttribute(j, Attribute::StructRet) || !reaches(j)) continue;
            if ((read_only(i) && read_only(j)) || !done.insert({std::min(i, j), std::max(i, j)}).second) continue;
            std::string why;
            unsigned bad = i;
            uint64_t size_i = slice[i] != 0 ? 1 : accessed_bytes(callee, i, why);
            uint64_t size_j = size_i != 0 ? (bad = j, slice[j] != 0 ? 1 : accessed_bytes(callee, j, why)) : 0;
            IRBuilder<> builder(&call);
            builder.SetCurrentDebugLocation(call.getDebugLoc());
            if (size_i == 0 || size_j == 0) {
                auto *marker = builder.CreateCall(unknown, {});
                std::string name = parameter_name(callee, bad);
                text(marker,
                     "cannot check that the arguments of restrict `" + name + "` of " + source_name(callee) +
                         " do not overlap: " + why,
                     "state the range the function accesses: smith_modifies(" + name +
                         ", size) or a smith_requires on its size, or take a slice (`T xs[restrict :]`); the call is "
                         "never assumed fine");
                if (const DISubprogram *subprogram = callee.getSubprogram())
                    marker->setMetadata(
                        "smith.restrict_at",
                        MDNode::get(llvm_context, {MDString::get(llvm_context, subprogram->getFilename()),
                                                   ConstantAsMetadata::get(ConstantInt::get(
                                                       Type::getInt32Ty(llvm_context), subprogram->getLine()))}));
                continue;
            }
            // The pointer and the bytes of an argument: a slice is its data and count * the element size.
            auto object = [&](unsigned k, uint64_t constant) -> std::pair<Value *, Value *> {
                if (slice[k] == 0) return {call.getArgOperand(k), builder.getInt64(constant)};
                Value *pair = call.getArgOperand(k);
                if (counts[k] > 1) { // in memory: the struct the argument points at
                    Value *data = builder.CreateAlignedLoad(builder.getPtrTy(), pair, Align(8));
                    Value *bytes = builder.getInt64(slice[k]);
                    for (unsigned at = 0; at < counts[k]; at++) {
                        Value *address = builder.CreateConstInBoundsGEP1_64(builder.getInt64Ty(), pair, at + 1);
                        bytes = builder.CreateMul(
                            bytes, builder.CreateAlignedLoad(builder.getInt64Ty(), address, Align(8)), "", true);
                    }
                    return {data, bytes};
                }
                Value *data = builder.CreateIntToPtr(builder.CreateExtractValue(pair, 0), builder.getPtrTy());
                Value *bytes =
                    builder.CreateMul(builder.CreateExtractValue(pair, 1), builder.getInt64(slice[k]), "", true);
                return {data, bytes};
            };
            auto [first, first_bytes] = object(i, size_i);
            auto [second, second_bytes] = object(j, size_j);
            auto *marker = builder.CreateCall(check, {first, second, first_bytes, second_bytes});
            marker->setMetadata("smith.resolved", MDNode::get(llvm_context, {}));
            text(marker,
                 "the call of " + source_name(callee) + " passes `" + parameter_name(callee, i) + "` (restrict) and `" +
                     parameter_name(callee, j) + "` " +
                     (slice[i] != 0 || slice[j] != 0 ? "slices or ranges that may overlap"
                                                     : "objects that may be the same or overlap"),
                 "");
        }
    }
}

// The calls of every function that is read (a trusted one's body never is) are checked.
void check_restrict_calls(Module &module) {
    SmallVector<CallBase *, 16> calls;
    for (Function &function : module) {
        if (function.isDeclaration() || is_trusted(function)) continue;
        for (Instruction &instruction : instructions(function)) {
            auto *call = dyn_cast<CallBase>(&instruction);
            Function *callee = call != nullptr ? call->getCalledFunction() : nullptr;
            if (callee == nullptr || callee->isDeclaration() || callee->isIntrinsic() || callee->arg_size() == 0)
                continue;
            for (unsigned i = 0; i < callee->arg_size(); i++) {
                bool restricted_slice = false;
                unsigned counts = 1;
                if (callee->hasParamAttribute(i, Attribute::NoAlias) ||
                    (slice_parameter(*callee, i, restricted_slice, counts) != 0 && restricted_slice)) {
                    calls.push_back(call);
                    break;
                }
            }
        }
    }
    for (CallBase *call : calls)
        check_restrict(module, *call, *call->getCalledFunction());
}

bool resolve_calls(UnitContext &context) {
    Function &function = context.function;
    lower_memcpy_chk(function);
    for (Instruction &instruction : instructions(function)) {
        if (contract_of(&instruction) != Contract::Modifies || owner_id(instruction) != function_id(function)) continue;
        context.framed = true;
        auto *size = dyn_cast<ConstantInt>(instruction.getOperand(1));
        if (size == nullptr || !size->isZero())
            context.ranges.emplace_back(instruction.getOperand(0), instruction.getOperand(1));
    }
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
            if (name == "smith.restrict_unknown") {
                MDNode *words = call->getMetadata("smith.restrict_text");
                context.fail(call, cast<MDString>(words->getOperand(0))->getString().str(),
                             cast<MDString>(words->getOperand(1))->getString().str());
                if (MDNode *at = call->getMetadata("smith.restrict_at")) {
                    context.unprovable->file = cast<MDString>(at->getOperand(0))->getString().str();
                    context.unprovable->line =
                        static_cast<unsigned>(mdconst::extract<ConstantInt>(at->getOperand(1))->getZExtValue());
                }
                return false;
            }
            if (name.starts_with("objc_msgSend")) {
                context.fail(call,
                             "an Objective-C message send in " + writer_of(context, call) +
                                 ": the method it reaches is not known",
                             boundary_fix(context, call, "call a plain C function"));
                return false;
            }
            if (callee->isIntrinsic() || contract_of(call) != Contract::None || is_assert_failure(call) ||
                is_abort(call) || name == "smith_point" || name == "smith.opaque") {
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
        if (target == nullptr) {
            if (context.framed) instrument_writes(context);
            return context.unprovable == nullptr;
        }
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
            if (kind == Contract::Modifies) { // read by instrument_writes, and by a caller's slice
                instruction->eraseFromParent();
                continue;
            }
            if (auto *call = dyn_cast<CallBase>(instruction);
                call != nullptr && call->getCalledFunction() != nullptr &&
                call->getCalledFunction()->getName() == "smith.restrict_check") {
                point(instruction, Kind::Restrict, nullptr, false);
                continue;
            }
            if (auto *call = dyn_cast<CallBase>(instruction);
                call != nullptr && call->getCalledFunction() != nullptr &&
                call->getCalledFunction()->getName() == "smith.write_check") {
                point(instruction, Kind::Write, instruction->getOperand(0), true);
                continue;
            }
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
            instruction.setMetadata("smith.induction", nullptr);
            instruction.setMetadata("smith.frame_ok", nullptr);
            instruction.setMetadata("smith.restrict_text", nullptr);
            instruction.setMetadata("smith.restrict_at", nullptr);
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
        info.contract_check =
            kind == Kind::Ensures || kind == Kind::Invariant || (kind == Kind::Assert && condition != nullptr);
        if (kind == Kind::Requires || kind == Kind::Ensures || kind == Kind::Invariant || kind == Kind::Premise) {
            if (MDNode *site = call->getMetadata("smith.callsite")) info.call_site = static_cast<int>(id_of(site));
        } else if (kind == Kind::Assert && condition != nullptr) {
            // smith_assert(condition) of <smith/contract.h>: the text is the line's, below
        } else if (kind == Kind::Assert && base->getCalledFunction()->getName() == "smith_failure") {
            // The check of an index or a slice of a `[:]` (and any other check of a library): reaching it is the
            // failure.
            info.text = "an index or a slice is in range";
        } else if (kind == Kind::Assert) {
            // __assert_rtn(function, file, line, expression) on macOS, __assert_fail(expression, file, line, function)
            bool apple = base->getCalledFunction()->getName() == "__assert_rtn";
            info.text = string_of(base->getArgOperand(apple ? 3 : 0));
            info.file = string_of(base->getArgOperand(1));
            if (auto *line = dyn_cast<ConstantInt>(base->getArgOperand(2)))
                info.line = static_cast<unsigned>(line->getZExtValue());
        } else if (kind == Kind::Restrict) {
            if (MDNode *text = call->getMetadata("smith.restrict_text"))
                info.text = cast<MDString>(text->getOperand(0))->getString().str();
        } else if (kind == Kind::Write) {
            info.text = "the function writes here, outside the ranges its smith_modifies names";
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
                info.inlined.push_back(subprogram_name(outer->getScope()->getSubprogram()) + " at " +
                                       outer->getFilename().str() + ":" + std::to_string(outer->getLine()));
            }
        }
        if (info.kind == Kind::Requires || info.kind == Kind::Ensures || info.kind == Kind::Invariant ||
            info.kind == Kind::Premise || (info.kind == Kind::Assert && condition != nullptr))
            info.text = source_line(info.file, info.line);
        IRBuilder<> builder(call);
        Value *value = condition != nullptr ? condition : builder.getFalse();
        CallInst *marker = nullptr;
        if (kind == Kind::Restrict) { // smith_restrict(id, first, second, first bytes, second bytes)
            FunctionCallee restrict_point = module.getOrInsertFunction(
                "smith_restrict",
                FunctionType::get(Type::getVoidTy(module.getContext()),
                                  {Type::getInt32Ty(module.getContext()), base->getArgOperand(0)->getType(),
                                   base->getArgOperand(1)->getType(), base->getArgOperand(2)->getType(),
                                   base->getArgOperand(3)->getType()},
                                  false));
            if (auto *declaration = dyn_cast<Function>(restrict_point.getCallee())) {
                declaration->setDoesNotAccessMemory();
                declaration->setDoesNotThrow();
            }
            marker = builder.CreateCall(restrict_point,
                                        {builder.getInt32(info.id), base->getArgOperand(0), base->getArgOperand(1),
                                         base->getArgOperand(2), base->getArgOperand(3)});
        } else {
            marker = builder.CreateCall(point_function, {builder.getInt32(info.id), value});
        }
        marker->setDebugLoc(call->getDebugLoc());
        info.assume = assume_after && condition != nullptr;
        if (kind == Kind::Invariant) {
            if (MDNode *induction = call->getMetadata("smith.induction")) {
                info.induction_loop = static_cast<int>(id_of(induction) / 2) - 1;
                info.induction_step = id_of(induction) % 2 == 1;
            }
        }
        info.where = marker;
        context_points.push_back(info);
        call->eraseFromParent();
    }
};

// Attributes LLVM 23 infers that the Alive2 this was built with does not know. Dropping nofree only loses what it said.
// Alive2 cannot translate `sret(T)`, the pointer a function that returns a large struct writes its result through. It
// becomes a pointer that is `dereferenceable` for T: the same object, which the caller owns. What `sret` also says,
// that nothing else points to it, was recorded as the `noalias` assumption before this (issue #74).
template <typename Callee> void strip_sret(Callee &callee, unsigned index) {
    Type *type = callee.getParamStructRetType(index);
    if (type == nullptr) return;
    uint64_t size = callee.getDataLayout().getTypeAllocSize(type).getFixedValue();
    callee.removeParamAttr(index, Attribute::StructRet);
    callee.addDereferenceableParamAttr(index, size);
}

void drop_unsupported_attributes(Function &function) {
    // `readnone` on a pointer a callee never touches (inferred too) makes Alive2 report the call as undefined behaviour
    // when the caller's pointer is `readonly`, an access it does not make.
    // `readonly` goes too (the promise is kept by hand where a contract is used, see pass_copies_of_readonly), and
    // `initializes`, which LLVM infers from a body, makes Alive2 check that the function stores those bytes and report
    // a load of what a call left there as undefined behaviour; what the bytes hold after a call is the callee's
    // contract.
    // `sret` goes (see strip_sret).
    for (unsigned i = 0; i < function.arg_size(); i++) {
        function.removeParamAttr(i, Attribute::NoFree);
        function.removeParamAttr(i, Attribute::Initializes);
        strip_sret(function, i);
    }
    for (Function &callee : *function.getParent()) {
        if (&callee != &function) {
            callee.removeRetAttr(Attribute::DereferenceableOrNull);
            callee.removeRetAttr(Attribute::Alignment);
        }
        for (unsigned i = 0; i < callee.arg_size(); i++) {
            callee.removeParamAttr(i, Attribute::NoFree);
            callee.removeParamAttr(i, Attribute::Initializes);
            strip_sret(callee, i);
            callee.removeParamAttr(i, Attribute::ReadNone);
            callee.removeParamAttr(i, Attribute::ReadOnly);
        }
    }
    for (Instruction &instruction : instructions(function)) {
        auto *call = dyn_cast<CallBase>(&instruction);
        if (call == nullptr) continue;
        call->removeRetAttr(Attribute::DereferenceableOrNull);
        call->removeRetAttr(Attribute::Alignment);
        for (unsigned i = 0; i < call->arg_size(); i++) {
            call->removeParamAttr(i, Attribute::NoFree);
            call->removeParamAttr(i, Attribute::Initializes);
            strip_sret(*call, i);
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
    if (info != nullptr && !info->not_cut.empty()) {
        failure.what =
            "the loop has no bound the prover can see, and its smith_invariant cannot be proved by induction: " +
            info->not_cut;
        failure.fix =
            "give the loop a bound (contract::requires(count <= ...)), or write what the loop changes so that "
            "the prover can name it";
    } else if (info != nullptr && info->counted && !info->controls.empty()) {
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
// the unrolling. The attribute is removed before the proof, and the optimizer has by then folded the premise itself to
// true (a fact the proof needs later, such as an index in range, would be gone), so the range is put back as an
// llvm.assume at the entry: what is proved is the function as it was, unrolled, with the premise kept (issue #117).
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

void remove_loop_ranges(Function &function, const std::vector<Argument *> &bounded) {
    if (bounded.empty()) return;
    IRBuilder<> builder(&*function.getEntryBlock().getFirstInsertionPt());
    for (Argument *argument : bounded) {
        ConstantRange range = argument->getAttribute(Attribute::Range).getValueAsConstantRange();
        argument->removeAttr(Attribute::Range);
        // argument in [lower, upper), also when the set wraps: (argument - lower) < (upper - lower) as unsigned numbers
        Type *type = argument->getType();
        Value *offset = builder.CreateSub(argument, ConstantInt::get(type, range.getLower()));
        Value *inside = builder.CreateICmpULT(offset, ConstantInt::get(type, range.getUpper() - range.getLower()));
        builder.CreateAssumption(inside);
    }
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

// ---- the tests of a loop that has an invariant, hidden from the optimizer ----

// An optimizer knows in a loop's body what the loop's test says (`i <= n` where `i < n` is true), and so writes an
// invariant `i <= n && total == 3 * i` as `total == 3 * i`. Proving it by induction evaluates it where the test is,
// with no such knowledge (it must hold when the loop exits too), so while the passes run, each test that decides
// whether the body is reached is read through a call of smith.opaque, which cut_loops removes.
FunctionCallee opaque_function(Module &module) {
    Type *boolean = Type::getInt1Ty(module.getContext());
    FunctionCallee callee = module.getOrInsertFunction("smith.opaque", boolean, boolean);
    auto *function = cast<Function>(callee.getCallee());
    function->setMemoryEffects(MemoryEffects::none());
    function->addFnAttr(Attribute::NoUnwind);
    function->addFnAttr(Attribute::NoFree);
    function->addFnAttr(Attribute::WillReturn);
    return callee;
}

void hide_loop_tests(Function &function) {
    bool has_invariant = false;
    for (const Instruction &instruction : instructions(function))
        has_invariant = has_invariant || contract_of(&instruction) == Contract::Invariant;
    if (!has_invariant) return;
    DominatorTree dominators(function);
    LoopInfo info(dominators);
    FunctionCallee opaque = opaque_function(*function.getParent());
    SmallPtrSet<BasicBlock *, 8> done;
    for (Loop *loop : info.getLoopsInPreorder()) {
        for (BasicBlock *block : loop->blocks()) {
            if (info.getLoopFor(block) != loop) continue;
            for (Instruction &instruction : *block) {
                if (contract_of(&instruction) != Contract::Invariant) continue;
                for (BasicBlock *test = block;; test = dominators.getNode(test)->getIDom()->getBlock()) {
                    auto *branch = dyn_cast<CondBrInst>(test->getTerminator());
                    if (branch != nullptr && done.insert(test).second)
                        branch->setCondition(
                            CallInst::Create(opaque, {branch->getCondition()}, "", branch->getIterator()));
                    if (test == loop->getHeader()) break;
                }
            }
        }
    }
}

void reveal_loop_tests(Function &function) {
    std::vector<CallInst *> calls;
    for (Instruction &instruction : instructions(function)) {
        auto *call = dyn_cast<CallInst>(&instruction);
        if (call != nullptr && call->getCalledFunction() != nullptr &&
            call->getCalledFunction()->getName() == "smith.opaque")
            calls.push_back(call);
    }
    for (CallInst *call : calls) {
        call->replaceAllUsesWith(call->getArgOperand(0));
        call->eraseFromParent();
    }
}

// ---- a loop with smith_invariant, proved by induction instead of unrolled ----
//
// The loop is cut in the IR, before Alive2 reads it. With the header H (its test), the preheader P and the one latch:
//   P:  the invariant `c`, evaluated for the values the loop starts with, is a proof point (it holds on entry);
//   H:  every value a pass changes is an arbitrary one: the header's phis become loads of a global nothing writes, and
//       each object the loop writes (found from its stores and calls) is overwritten with the bytes of one. `c`,
//       evaluated here, is assumed. What follows is the loop as it was: its test, one pass of the body (its own checks
//       are proof points as everywhere), and the exits, which see `c` and the failed test;
//   the back edge: goes to code that evaluates `c` for the values the next pass would start with, as a proof point,
//       and returns (a path that ends; nothing is compared with what it returns).
// So the loop costs one pass, whatever its bound. `c` is evaluated where the loop's test is, by a copy of the blocks
// between the header and the invariant (the test itself left out, its traps kept), so it must hold when the loop exits
// too. What a pass changes that `c` does not mention is arbitrary at its start: a weak invariant fails at the back
// edge, and the report says what the body writes.

bool is_bookkeeping(const Instruction &instruction) {
    if (contract_of(&instruction) != Contract::None) return true;
    auto *call = dyn_cast<CallBase>(&instruction);
    if (call == nullptr || call->getCalledFunction() == nullptr) return false;
    if (call->getCalledFunction()->getName() == "smith_loop" ||
        call->getCalledFunction()->getName() == "smith.write_check" ||
        call->getCalledFunction()->getName() == "smith.restrict_check")
        return true;
    switch (call->getCalledFunction()->getIntrinsicID()) {
    case Intrinsic::assume:
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::experimental_noalias_scope_decl:
        return true;
    default:
        return false;
    }
}

// The objects a loop writes, each traced from the pointer that is written; false (and why) when one cannot be.
bool add_written(Value *pointer, std::vector<Value *> &objects, std::string &why) {
    Value *object = const_cast<Value *>(getUnderlyingObject(pointer));
    auto *global = dyn_cast<GlobalVariable>(object);
    auto *local = dyn_cast<AllocaInst>(object);
    if ((local == nullptr || !local->isStaticAlloca()) && !isa<Argument>(object) &&
        (global == nullptr || global->isConstant())) {
        why = "it writes through a pointer that is not traced to a local, a parameter or a global";
        return false;
    }
    if (std::find(objects.begin(), objects.end(), object) == objects.end()) objects.push_back(object);
    return true;
}

bool loop_writes(Loop *loop, std::vector<Value *> &objects, std::string &why) {
    for (BasicBlock *block : loop->blocks()) {
        for (Instruction &instruction : *block) {
            if (is_bookkeeping(instruction)) continue;
            if (auto *store = dyn_cast<StoreInst>(&instruction)) {
                if (!store->isUnordered()) {
                    why = "it has a volatile or atomic store";
                    return false;
                }
                if (!add_written(store->getPointerOperand(), objects, why)) return false;
                continue;
            }
            if (!instruction.mayWriteToMemory()) continue;
            if (auto *intrinsic = dyn_cast<MemIntrinsic>(&instruction)) {
                if (intrinsic->isVolatile() || !add_written(intrinsic->getDest(), objects, why)) return false;
                continue;
            }
            auto *call = dyn_cast<CallBase>(&instruction);
            if (call == nullptr) {
                why = "it has an atomic operation";
                return false;
            }
            MemoryEffects effects = call->getMemoryEffects();
            // what only a call can see (inaccessible memory: a trap's state, a library's) is not an object of this
            // function, and a call's own results are arbitrary anyway
            if (isModSet(effects.getModRef(IRMemLocation::Other))) {
                why = "it calls " +
                      (call->getCalledFunction() != nullptr ? source_name(*call->getCalledFunction())
                                                            : std::string("a function")) +
                      ", which may write memory the prover cannot name";
                return false;
            }
            for (unsigned i = 0; i < call->arg_size(); i++) {
                if (!call->getArgOperand(i)->getType()->isPointerTy() || call->paramHasAttr(i, Attribute::ReadOnly) ||
                    call->paramHasAttr(i, Attribute::ReadNone))
                    continue;
                if (!add_written(call->getArgOperand(i), objects, why)) return false;
            }
        }
    }
    return true;
}

// How many bytes an object that is havoced has (and how it is aligned); 0 when that is not a constant.
uint64_t havoc_bytes(Function &function, Value *object, Align &align) {
    const DataLayout &layout = function.getParent()->getDataLayout();
    if (auto *local = dyn_cast<AllocaInst>(object)) {
        std::optional<TypeSize> size = local->getAllocationSize(layout);
        align = local->getAlign();
        return size && !size->isScalable() ? size->getFixedValue() : 0;
    }
    if (auto *argument = dyn_cast<Argument>(object)) {
        align = argument->getParamAlign().value_or(Align(1));
        return argument->getDereferenceableBytes();
    }
    auto *global = cast<GlobalVariable>(object);
    align = global->getAlign().value_or(layout.getABITypeAlign(global->getValueType()));
    return layout.getTypeAllocSize(global->getValueType()).getFixedValue();
}

// The invariants of a loop: the smith_invariant calls written in the loop itself, not in an inner one.
std::vector<CallInst *> loop_invariants(Loop *loop, LoopInfo &info) {
    std::vector<CallInst *> invariants;
    for (BasicBlock *block : loop->blocks()) {
        if (info.getLoopFor(block) != loop) continue;
        for (Instruction &instruction : *block) {
            if (contract_of(&instruction) == Contract::Invariant) invariants.push_back(cast<CallInst>(&instruction));
        }
    }
    return invariants;
}

// A block that ends the program: a check that traps, the failure of an assert.
bool is_terminal(const BasicBlock *block) {
    return isa<UnreachableInst>(block->getTerminator());
}

// The blocks that lead from the loop's header to an invariant (header first, the invariant's block last), when copies
// of them can evaluate it elsewhere: nothing in them writes memory, no inner loop is among them, and the only branches
// that leave them go to a block that ends the program (a check that traps) or are the loop's test.
bool invariant_region(Function &function, Loop *loop, LoopInfo &info, CallInst *call, std::vector<BasicBlock *> &region,
                      std::string &why) {
    BasicBlock *header = loop->getHeader();
    BasicBlock *last = call->getParent();
    SmallPtrSet<BasicBlock *, 16> after, before;
    std::vector<BasicBlock *> work = {header};
    while (!work.empty()) { // reachable from the header in the loop, not round the back edge
        BasicBlock *block = work.back();
        work.pop_back();
        if (!after.insert(block).second) continue;
        for (BasicBlock *next : successors(block)) {
            if (loop->contains(next) && next != header) work.push_back(next);
        }
    }
    work = {last};
    while (!work.empty()) { // that reach the invariant
        BasicBlock *block = work.back();
        work.pop_back();
        if (!before.insert(block).second || block == header) continue;
        for (BasicBlock *previous : predecessors(block)) {
            if (loop->contains(previous)) work.push_back(previous);
        }
    }
    for (BasicBlock &block : function) {
        if (after.count(&block) != 0 && before.count(&block) != 0) region.push_back(&block);
    }
    SmallPtrSet<BasicBlock *, 16> inside(region.begin(), region.end());
    for (BasicBlock *block : region) {
        if (info.getLoopFor(block) != loop) {
            why = "an inner loop comes before its invariant";
            return false;
        }
        for (Instruction &instruction : *block) {
            if (&instruction == call) break;
            if (!is_bookkeeping(instruction) && instruction.mayWriteToMemory()) {
                why = "its invariant is not the first statement of the body: something before it writes memory";
                return false;
            }
        }
        if (block == last) continue;
        auto *branch = dyn_cast<CondBrInst>(block->getTerminator());
        if (!isa<UncondBrInst>(block->getTerminator()) && branch == nullptr) {
            why = "something other than a branch comes before its invariant";
            return false;
        }
        unsigned outside = 0;
        for (BasicBlock *next : successors(block)) {
            if (inside.count(next) == 0 && !is_terminal(next)) outside++;
        }
        if (outside > 1 || (outside == 1 && branch == nullptr)) {
            why = "something that leaves the loop comes before its invariant";
            return false;
        }
    }
    return true;
}

// A copy of a region that ends at a copy of the invariant's call: where the header's phis stand, the values
// `phi_values` gives. The loop's test is not part of it (a branch to a block outside the region that does not end the
// program becomes a jump to the other one), and its end has no terminator yet. The checks that trap are kept, as proof
// points; with `trust_checks` (the invariant is assumed) they are assumed to pass instead.
struct Evaluation {
    BasicBlock *entry = nullptr;
    BasicBlock *tail = nullptr;
    CallInst *call = nullptr;
};

Evaluation clone_region(Function &function, const std::vector<BasicBlock *> &region, CallInst *call,
                        const DenseMap<PHINode *, Value *> &phi_values, bool trust_checks) {
    BasicBlock *header = region.front();
    SmallPtrSet<BasicBlock *, 16> inside(region.begin(), region.end());
    ValueToValueMapTy mapped;
    SmallVector<BasicBlock *, 16> made;
    auto copy = [&](BasicBlock *block) {
        BasicBlock *clone = CloneBasicBlock(block, mapped, ".ind", &function);
        mapped[block] = clone;
        made.push_back(clone);
    };
    for (BasicBlock *block : region)
        copy(block);
    for (BasicBlock *block : region) { // the checks that trap (assumed to pass, when the invariant is assumed)
        if (block == call->getParent() || trust_checks) continue;
        for (BasicBlock *next : successors(block)) {
            if (inside.count(next) == 0 && is_terminal(next) && mapped.count(next) == 0) copy(next);
        }
    }
    for (PHINode &phi : header->phis()) {
        cast<PHINode>(mapped[&phi])->eraseFromParent();
        mapped[&phi] = phi_values.lookup(&phi);
    }
    for (BasicBlock *block : region) { // the test: only the way that stays
        auto *branch = block == call->getParent() ? nullptr : dyn_cast<CondBrInst>(block->getTerminator());
        if (branch == nullptr) continue;
        for (unsigned i = 0; i < 2; i++) {
            BasicBlock *stay = branch->getSuccessor(i);
            BasicBlock *leave = branch->getSuccessor(1 - i);
            bool test = inside.count(stay) != 0 && inside.count(leave) == 0 && !is_terminal(leave);
            bool check = trust_checks && inside.count(stay) != 0 && is_terminal(leave);
            if (!test && !check) continue;
            auto *clone = cast<BasicBlock>(mapped[block]);
            IRBuilder<> builder(clone->getTerminator());
            builder.CreateBr(stay);
            clone->getTerminator()->eraseFromParent();
            break;
        }
    }
    remapInstructionsInBlocks(made, mapped);
    Evaluation result;
    result.entry = cast<BasicBlock>(mapped[header]);
    result.tail = cast<BasicBlock>(mapped[call->getParent()]);
    result.call = cast<CallInst>(mapped[call]);
    while (&result.tail->back() != result.call)
        result.tail->back().eraseFromParent();
    return result;
}

// The names of what a pass changes, as the source writes them: loop variables whose next value is not their own, and
// the objects it writes.
std::vector<std::string> written_names(Loop *loop, const std::vector<Value *> &objects) {
    std::vector<std::string> names;
    auto add = [&](Value *value) {
        std::string name = source_name_of(value);
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    };
    for (PHINode &phi : loop->getHeader()->phis()) {
        if (phi.getIncomingValueForBlock(loop->getLoopLatch()) != &phi) add(&phi);
    }
    for (Value *object : objects)
        add(object);
    return names;
}

// The number of a loop in the unit, from the mark in its header; -1 without one.
int marker_index(BasicBlock *header) {
    for (Instruction &instruction : *header) {
        auto *call = dyn_cast<CallBase>(&instruction);
        if (call != nullptr && call->getCalledFunction() != nullptr &&
            call->getCalledFunction()->getName() == "smith_loop")
            return static_cast<int>(cast<ConstantInt>(call->getArgOperand(0))->getZExtValue());
    }
    return -1;
}

enum class Cut { Skipped, Done, Refused };

// What cutting a loop has worked out before it changes anything.
struct Plan {
    std::vector<CallInst *> invariants;
    std::vector<std::vector<BasicBlock *>> regions;
    std::vector<Value *> objects;
    std::vector<uint64_t> sizes;
    std::vector<Align> alignments;
};

bool plan_cut(UnitContext &context, Loop *loop, LoopInfo &info, Plan &plan, std::string &why) {
    BasicBlock *latch = loop->getLoopLatch();
    if (loop->getLoopPreheader() == nullptr || latch == nullptr ||
        !isa<UncondBrInst>(loop->getLoopPreheader()->getTerminator())) {
        why = "the loop has no preheader or no single back edge";
        return false;
    }
    for (CallInst *call : plan.invariants) {
        plan.regions.emplace_back();
        if (!invariant_region(context.function, loop, info, call, plan.regions.back(), why)) return false;
    }
    if (!loop_writes(loop, plan.objects, why)) return false;
    for (PHINode &phi : loop->getHeader()->phis()) {
        Type *type = phi.getType();
        if (!type->isIntegerTy() && !type->isFloatingPointTy() && phi.getIncomingValueForBlock(latch) != &phi) {
            why = "a loop variable is a pointer or a vector";
            return false;
        }
    }
    for (Value *object : plan.objects) {
        Align align(1);
        uint64_t bytes = havoc_bytes(context.function, object, align);
        if (bytes == 0) {
            why = "it writes an object whose size the prover does not know (a slice or a pointer to an array)";
            return false;
        }
        plan.sizes.push_back(bytes);
        plan.alignments.push_back(align);
    }
    return true;
}

// Evaluations of every invariant one after the other; each call is given to `use`. Returns the first block and the
// last, which has no terminator yet.
std::pair<BasicBlock *, BasicBlock *> chain_evaluations(Function &function, const Plan &plan,
                                                        const DenseMap<PHINode *, Value *> &phi_values,
                                                        bool trust_checks,
                                                        const std::function<void(size_t, CallInst *)> &use) {
    std::vector<Evaluation> all;
    for (size_t i = 0; i < plan.invariants.size(); i++) {
        all.push_back(clone_region(function, plan.regions[i], plan.invariants[i], phi_values, trust_checks));
        use(i, all.back().call);
    }
    for (size_t i = 0; i + 1 < all.size(); i++) {
        IRBuilder<> builder(all[i].tail);
        builder.CreateBr(all[i + 1].entry);
    }
    return {all.front().entry, all.back().tail};
}

// Cuts one loop as described above. Skipped: it has no invariant, or it is better unrolled. Refused: it has one and the
// loop cannot be cut, `why`.
Cut cut_loop(UnitContext &context, const Options &options, Loop *loop, LoopInfo &info, std::string &why) {
    Function &function = context.function;
    Module &module = context.module;
    LLVMContext &llvm_context = module.getContext();
    BasicBlock *header = loop->getHeader();
    Plan plan;
    plan.invariants = loop_invariants(loop, info);
    if (plan.invariants.empty()) return Cut::Skipped;
    int index = marker_index(header);
    if (index < 0) {
        why = "the loop has no mark";
        return Cut::Refused;
    }
    UnrolledLoop &record = context.unit.loops[static_cast<size_t>(index)];
    // Both are possible when the loop has a bound: by default the loop is unrolled when it has few passes.
    if (options.loops == Options::Loops::Unroll ||
        (options.loops == Options::Loops::Auto && record.passes != 0 && record.passes <= options.induction_from_passes))
        return Cut::Skipped;
    if (!plan_cut(context, loop, info, plan, why)) return Cut::Refused;
    record.writes = written_names(loop, plan.objects);
    BasicBlock *preheader = loop->getLoopPreheader();
    BasicBlock *latch = loop->getLoopLatch();
    for (Instruction &instruction : *header) { // the mark is for the unroller
        auto *call = dyn_cast<CallBase>(&instruction);
        if (call != nullptr && call->getCalledFunction() != nullptr &&
            call->getCalledFunction()->getName() == "smith_loop") {
            call->eraseFromParent();
            break;
        }
    }
    FunctionCallee requires_function = module.getOrInsertFunction(
        "smith_contract_requires", plan.invariants.front()->getCalledFunction()->getFunctionType(),
        plan.invariants.front()->getCalledFunction()->getAttributes());
    auto tag = [&](CallInst *call, bool step) {
        call->setMetadata("smith.induction", id_node(llvm_context, (static_cast<unsigned>(index) + 1) * 2 + step));
    };
    DenseMap<PHINode *, Value *> from_preheader, from_latch, as_they_are;
    for (PHINode &phi : header->phis()) {
        from_preheader[&phi] = phi.getIncomingValueForBlock(preheader);
        from_latch[&phi] = phi.getIncomingValueForBlock(latch);
        as_they_are[&phi] = &phi;
    }
    // on entry, at the back edge, and at the start of an arbitrary pass (assumed, not proved)
    auto [entry_first, entry_last] =
        chain_evaluations(function, plan, from_preheader, false, [&](size_t, CallInst *call) { tag(call, false); });
    auto [back_first, back_last] =
        chain_evaluations(function, plan, from_latch, false, [&](size_t, CallInst *call) { tag(call, true); });
    auto assume = [&](size_t i, CallInst *call) {
        auto *assumption = CallInst::Create(requires_function, {call->getArgOperand(0)}, "", call->getIterator());
        assumption->setDebugLoc(plan.invariants[i]->getDebugLoc());
        assumption->setMetadata("smith.owner", id_node(llvm_context, function_id(function)));
        call->eraseFromParent();
    };
    auto [test_first, test_last] = chain_evaluations(function, plan, as_they_are, true, assume);
    std::vector<PHINode *> phis;
    for (PHINode &phi : header->phis())
        phis.push_back(&phi);
    IRBuilder<> wiring(entry_last);
    wiring.CreateBr(header);
    preheader->getTerminator()->replaceSuccessorWith(header, entry_first);
    Type *result = function.getReturnType();
    ReturnInst::Create(llvm_context, result->isVoidTy() ? nullptr : Constant::getNullValue(result), back_last);
    latch->getTerminator()->replaceSuccessorWith(header, back_first);
    BasicBlock *rest = header->splitBasicBlock(header->getFirstNonPHIIt(), "smith.test");
    Instruction *jump = header->getTerminator();
    IRBuilder<> assuming(test_last);
    assuming.CreateBr(rest);
    jump->replaceSuccessorWith(rest, test_first);
    // arbitrary values in the phis and in what the loop writes
    function.setMemoryEffects(MemoryEffects::unknown()); // it now reads globals, whatever it inferred
    const DataLayout &layout = module.getDataLayout();
    IRBuilder<> builder(jump);
    builder.SetCurrentDebugLocation(loop->getStartLoc());
    std::vector<std::pair<PHINode *, Value *>> replaced;
    for (PHINode *phi : phis) {
        Value *value = phi->getIncomingValueForBlock(latch) == phi ? phi->getIncomingValueForBlock(preheader) : nullptr;
        if (value == nullptr) {
            auto *source = new GlobalVariable(module, phi->getType(), false, GlobalValue::InternalLinkage,
                                              Constant::getNullValue(phi->getType()),
                                              "smith.havoc." + std::to_string(context.havocs++));
            source->setAlignment(layout.getABITypeAlign(phi->getType()));
            auto *load = builder.CreateAlignedLoad(phi->getType(), source, source->getAlign());
            load->takeName(phi);
            value = load;
        }
        replaced.push_back({phi, value});
    }
    for (size_t i = 0; i < plan.objects.size(); i++) {
        auto *type = ArrayType::get(Type::getInt8Ty(llvm_context), plan.sizes[i]);
        auto *source =
            new GlobalVariable(module, type, false, GlobalValue::InternalLinkage, ConstantAggregateZero::get(type),
                               "smith.havoc." + std::to_string(context.havocs++));
        source->setAlignment(plan.alignments[i]);
        if (auto *local = dyn_cast<AllocaInst>(plan.objects[i]))
            local->setMetadata("smith.havoced", MDNode::get(llvm_context, {}));
        copy_object(jump, loop->getStartLoc(), plan.objects[i], source, plan.sizes[i], plan.alignments[i], true);
    }
    for (CallInst *call : plan.invariants)
        call->eraseFromParent();
    for (auto &[phi, value] : replaced) {
        phi->replaceAllUsesWith(value);
        phi->eraseFromParent();
    }
    record.induction = true;
    return Cut::Done;
}

// The loops with an invariant that are proved by induction (innermost first, the loop information read again after each
// cut), and the others left to the unroller; a loop that has an invariant and could not be cut says why.
void cut_loops(UnitContext &context, Analyses &analyses, const Options &options) {
    Function &function = context.function;
    std::string error;
    analyses.run_function(function, "loop-simplify", error);
    std::set<const BasicBlock *> left;
    for (bool changed = true; changed;) {
        changed = false;
        DominatorTree dominators(function);
        LoopInfo info(dominators);
        SmallVector<Loop *, 8> loops = info.getLoopsInPreorder();
        for (auto at = loops.rbegin(); at != loops.rend() && !changed; ++at) {
            if (left.count((*at)->getHeader()) != 0) continue;
            std::string why;
            Cut result = cut_loop(context, options, *at, info, why);
            if (result == Cut::Done) {
                changed = true;
                continue;
            }
            left.insert((*at)->getHeader());
            int index = marker_index((*at)->getHeader());
            if (result == Cut::Refused && index >= 0) context.unit.loops[static_cast<size_t>(index)].not_cut = why;
        }
        if (changed) analyses.run_function(function, "simplifycfg,dce", error);
    }
    analyses.functions.invalidate(function, PreservedAnalyses::none());
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
        if (loop.induction) {
            cost.what = "a loop proved by induction: one pass of its body, whatever its bound";
        } else if (!loop.not_cut.empty()) {
            cost.what = "a loop unrolled into " + std::to_string(loop.passes) +
                        " passes, its invariant checked in each (" + loop.not_cut + ", so not proved by induction)";
        } else
            cost.what = loop.passes != 0
                            ? "a loop unrolled into " + std::to_string(loop.passes) + " passes"
                            : "a loop unrolled (its pass count is not a constant LLVM reads before unrolling)";
        cost.file = loop.file;
        cost.line = loop.line;
        cost.count = loop.induction ? 1 : loop.passes;
        cost.weight = 1;
        found.push_back(cost);
    }
    std::stable_sort(found.begin(), found.end(), [](const Cost &a, const Cost &b) { return a.weight > b.weight; });
    unit.costs = std::move(found);
}

} // namespace

// The files `root` reads functions and globals from, through everything it calls or uses (issue #221): a check removed
// on this proof is sound only while each of them is as the proof read it. Called before any callee is replaced.
void collect_dependencies(const Function &root, std::set<std::string> &files) {
    std::set<const Function *> seen;
    std::set<const Value *> visited;
    std::vector<const Function *> pending{&root};
    auto add = [&](const DIFile *file) {
        if (file != nullptr) files.insert(file->getDirectory().str() + "\n" + file->getFilename().str());
    };
    std::function<void(const Value *)> use = [&](const Value *value) {
        if (!visited.insert(value).second) return;
        if (const auto *function = dyn_cast<Function>(value)) {
            if (!function->isDeclaration()) pending.push_back(function);
            return;
        }
        if (const auto *global = dyn_cast<GlobalVariable>(value)) {
            SmallVector<DIGlobalVariableExpression *, 1> expressions;
            global->getDebugInfo(expressions);
            for (const DIGlobalVariableExpression *expression : expressions)
                add(expression->getVariable()->getFile());
            if (global->hasInitializer()) use(global->getInitializer());
            return;
        }
        if (const auto *constant = dyn_cast<Constant>(value)) {
            for (const Value *operand : constant->operands())
                use(operand);
        }
    };
    while (!pending.empty()) {
        const Function *function = pending.back();
        pending.pop_back();
        if (!seen.insert(function).second) continue;
        if (const DISubprogram *subprogram = function->getSubprogram()) add(subprogram->getFile());
        for (const Instruction &instruction : instructions(*function)) {
            for (const DILocation *at = instruction.getDebugLoc().get(); at != nullptr; at = at->getInlinedAt())
                add(at->getFile());
            for (const Value *operand : instruction.operands())
                use(operand);
        }
    }
}

Prepared program_prepare(Program &program, const std::string &name, const Options &options) {
    Prepared prepared;
    prepared.module = CloneModule(*program.module);
    collect_dependencies(*prepared.module->getFunction(name), prepared.unit.dependencies);
    prepared.function = split_slice_parameters(prepared.module->getFunction(name));
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
    // two that are not both only read (issue #57), and the `sret` result of a function, which is its caller's own
    // object. Alive2 does not take the attribute; what it says is assumed below.
    for (unsigned i = 0; i < function.arg_size(); i++) {
        bool distinct = function.hasParamAttribute(i, Attribute::NoAlias) || function.getArg(i)->hasStructRetAttr();
        if (!function.getArg(i)->getType()->isPointerTy() || !distinct) continue;
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
        if (&other != &function) { // its callers assume it (assume_returned_pointer), and LLVM must not use it first
            other.removeRetAttr(Attribute::DereferenceableOrNull);
            other.removeRetAttr(Attribute::Alignment);
        }
        for (unsigned i = 0; i < other.arg_size(); i++)
            other.removeParamAttr(i, Attribute::NoAlias);
        for (Instruction &instruction : instructions(other)) {
            if (auto *call = dyn_cast<CallBase>(&instruction)) {
                call->removeRetAttr(Attribute::DereferenceableOrNull);
                call->removeRetAttr(Attribute::Alignment);
                for (unsigned i = 0; i < call->arg_size(); i++)
                    call->removeParamAttr(i, Attribute::NoAlias);
            }
        }
    }
    std::set<std::string> recursive;
    for (const Function *function : program.recursive)
        recursive.insert(function->getName().str());
    UnitContext context{*prepared.module, function, prepared.unit, recursive, {}, nullptr, 0, {}, false, {}, {}};
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
    reveal_loop_tests(function);
    std::string loop;
    int index = -1;
    std::vector<Argument *> bounded = assume_requires(function);
    analyses.functions.invalidate(function, PreservedAnalyses::none());
    mark_loops(analyses, function, prepared.unit);
    cut_loops(context, analyses, options);
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
    remove_loop_ranges(function, bounded);
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
    case Kind::Write:
        return "a write inside the ranges smith_modifies names, proved";
    case Kind::Restrict:
        return "the objects of a restrict call do not overlap, proved";
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
