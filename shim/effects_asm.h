// Inline assembly read for its effects (issue #396, skill smith-lang-effects). The template of a GCC-style `asm`
// statement is parsed with LLVM's own assembler (the MC layer smith already links for `-c` on a .s file), the operands
// replaced first by registers (and the constants they hold by their values), and the instructions are read:
//
//   - no system-call instruction and no call or jump out of the block: the block has no effect (SIMD, atomics,
//   counters,
//     a compiler barrier);
//   - a system call (`svc` on ARM, `syscall` on x86-64): the call number is a constant when the block, a register
//     variable (`register long x8 asm("x8") = 64;`) or an operand ("a"(SYS_write)) puts one in the register the system
//     calls read it from; the number names a libc function, whose effects the table gives; an unknown number is `c`;
//   - a call or a jump to a named symbol: that symbol's effects; through a register or memory: `c`;
//   - memory made executable (a call of mmap or mprotect with a constant protection that has PROT_EXEC) is `code`.
//
// A block the assembler cannot read (an unsupported target, an unknown instruction) stays `c`, as before. Only the
// targets the LLVM smith links has an assembler for are read (the host's), so a cross-compile to another is `c`.
#pragma once

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace smith_effects {

// One system-call instruction of a block: the number and the protection argument, when they are constants.
struct AsmSyscall {
    std::optional<int64_t> number;
    std::optional<int64_t> protection; // the third argument, what mmap and mprotect read as PROT_*
};

// What a block does that the effects care about.
struct AsmReading {
    bool parsed = false;              // the assembler read the block
    std::vector<AsmSyscall> syscalls; // the system-call instructions
    std::vector<std::string> calls;   // the symbols called or jumped to out of the block
    bool indirect = false;            // a call or jump through a register or memory
};

// The libc function a system call number is, for the targets with a table; "" for a number the table does not have.
struct SyscallRow {
    int64_t number;
    const char *name;
};

// Linux x86-64.
inline constexpr SyscallRow linux_x86_64[] = {
    {0, "read"},
    {1, "write"},
    {2, "open"},
    {3, "close"},
    {4, "stat"},
    {5, "fstat"},
    {6, "lstat"},
    {8, "lseek"},
    {9, "mmap"},
    {10, "mprotect"},
    {11, "munmap"},
    {12, "brk"},
    {16, "ioctl"},
    {17, "pread"},
    {18, "pwrite"},
    {19, "readv"},
    {20, "writev"},
    {21, "access"},
    {22, "pipe"},
    {35, "nanosleep"},
    {39, "getpid"},
    {41, "socket"},
    {42, "connect"},
    {43, "accept"},
    {44, "sendto"},
    {45, "recvfrom"},
    {46, "sendmsg"},
    {47, "recvmsg"},
    {48, "shutdown"},
    {49, "bind"},
    {50, "listen"},
    {57, "fork"},
    {59, "execve"},
    {60, "_exit"},
    {61, "wait4"},
    {62, "kill"},
    {63, "uname"},
    {79, "getcwd"},
    {80, "chdir"},
    {82, "rename"},
    {83, "mkdir"},
    {84, "rmdir"},
    {87, "unlink"},
    {89, "readlink"},
    {96, "gettimeofday"},
    {201, "time"},
    {228, "clock_gettime"},
    {230, "clock_nanosleep"},
    {231, "_exit"},
    {257, "openat"},
    {318, "getrandom"},
};

// Linux AArch64 (the generic table).
inline constexpr SyscallRow linux_aarch64[] = {
    {17, "getcwd"},     {29, "ioctl"},         {34, "mkdirat"},    {35, "unlinkat"},       {49, "chdir"},
    {56, "openat"},     {57, "close"},         {62, "lseek"},      {63, "read"},           {64, "write"},
    {65, "readv"},      {66, "writev"},        {78, "readlinkat"}, {79, "fstatat"},        {80, "fstat"},
    {93, "_exit"},      {94, "_exit"},         {101, "nanosleep"}, {113, "clock_gettime"}, {129, "kill"},
    {160, "uname"},     {169, "gettimeofday"}, {172, "getpid"},    {198, "socket"},        {200, "bind"},
    {201, "listen"},    {202, "accept"},       {203, "connect"},   {206, "sendto"},        {207, "recvfrom"},
    {210, "shutdown"},  {211, "sendmsg"},      {212, "recvmsg"},   {214, "brk"},           {215, "munmap"},
    {220, "fork"},      {221, "execve"},       {222, "mmap"},      {226, "mprotect"},      {242, "accept"},
    {278, "getrandom"},
};

// macOS arm64 (the BSD numbers, in x16).
inline constexpr SyscallRow darwin_arm64[] = {
    {1, "_exit"},     {2, "fork"},     {3, "read"},     {4, "write"},     {5, "open"},      {6, "close"},
    {7, "wait4"},     {10, "unlink"},  {12, "chdir"},   {20, "getpid"},   {29, "recvfrom"}, {30, "accept"},
    {33, "access"},   {37, "kill"},    {54, "ioctl"},   {58, "readlink"}, {59, "execve"},   {73, "munmap"},
    {74, "mprotect"}, {97, "socket"},  {98, "connect"}, {104, "bind"},    {106, "listen"},  {116, "gettimeofday"},
    {128, "rename"},  {133, "sendto"}, {136, "mkdir"},  {137, "rmdir"},   {188, "stat"},    {197, "mmap"},
    {199, "lseek"},   {338, "stat"},   {339, "fstat"},  {340, "lstat"},   {463, "openat"},
};

// The name of a system call on a target, "" when the table has no row.
inline std::string syscallName(const llvm::Triple &triple, int64_t number) {
    const SyscallRow *rows = nullptr;
    size_t count = 0;
    if (triple.isOSLinux() && triple.getArch() == llvm::Triple::x86_64) {
        rows = linux_x86_64;
        count = sizeof linux_x86_64 / sizeof linux_x86_64[0];
    } else if (triple.isOSLinux() && triple.isAArch64()) {
        rows = linux_aarch64;
        count = sizeof linux_aarch64 / sizeof linux_aarch64[0];
    } else if (triple.isOSDarwin() && triple.isAArch64()) {
        rows = darwin_arm64;
        count = sizeof darwin_arm64 / sizeof darwin_arm64[0];
    }
    for (size_t i = 0; i < count; i++)
        if (rows[i].number == number) return rows[i].name;
    return "";
}

// The register a target's system call reads its number from, and the one of its third argument.
inline const char *syscallNumberRegister(const llvm::Triple &triple) {
    if (triple.getArch() == llvm::Triple::x86_64) return "rax";
    return triple.isOSDarwin() ? "x16" : "x8";
}
inline const char *syscallProtectionRegister(const llvm::Triple &triple) {
    return triple.getArch() == llvm::Triple::x86_64 ? "rdx" : "x2";
}

namespace detail {

using namespace llvm;

// A streamer that keeps the instructions and nothing else.
class Collect final : public MCStreamer {
  public:
    std::vector<MCInst> instructions;
    explicit Collect(MCContext &context) : MCStreamer(context) {}
    void emitInstruction(const MCInst &instruction, const MCSubtargetInfo &) override {
        instructions.push_back(instruction);
    }
    bool emitSymbolAttribute(MCSymbol *, MCSymbolAttr) override {
        return true;
    }
    void emitCommonSymbol(MCSymbol *, uint64_t, Align) override {}
};

// A register an operand of the block stands for, and what is known of it.
struct Slot {
    std::string reg;              // "x9", "rax", "r8"
    std::optional<int64_t> value; // a constant the register holds on entry
};

struct Operands {
    std::vector<Slot> slots;
    std::vector<std::string> text; // what `%N` is replaced by, with no modifier
};

inline bool hasAny(const std::string &letters, const char *wanted) {
    return letters.find_first_of(wanted) != std::string::npos;
}

// The register of an operand that is a variable declared `register T x asm("x8")`, "" for any other expression.
inline std::string registerVariable(const clang::Expr *expr) {
    const auto *ref = llvm::dyn_cast<clang::DeclRefExpr>(expr->IgnoreParenImpCasts());
    if (ref == nullptr) return "";
    const auto *var = llvm::dyn_cast<clang::VarDecl>(ref->getDecl());
    if (var == nullptr) return "";
    const auto *label = var->getAttr<clang::AsmLabelAttr>();
    return label == nullptr ? "" : label->getLabel().str();
}

inline std::optional<int64_t> constantOf(const clang::Expr *expr, const clang::ASTContext &context) {
    clang::Expr::EvalResult result;
    if (expr == nullptr || expr->isValueDependent() || !expr->EvaluateAsInt(result, context)) return std::nullopt;
    return result.Val.getInt().getExtValue();
}

// The constant a register variable was initialised with, or the expression's own value.
inline std::optional<int64_t> valueOf(const clang::Expr *expr, const clang::ASTContext &context) {
    if (auto direct = constantOf(expr, context)) return direct;
    const auto *ref = llvm::dyn_cast<clang::DeclRefExpr>(expr->IgnoreParenImpCasts());
    const auto *var = ref == nullptr ? nullptr : llvm::dyn_cast<clang::VarDecl>(ref->getDecl());
    if (var != nullptr && var->hasInit() && !registerVariable(expr).empty()) return constantOf(var->getInit(), context);
    return std::nullopt;
}

// AArch64: the register `reg` ("x9" or "w9"; a vector one by its letter) at the width the modifier or the size says.
inline std::string armRegister(const std::string &reg, char modifier, unsigned size) {
    if (reg.size() < 2 || (reg[0] != 'x' && reg[0] != 'w')) return reg;
    char prefix = modifier == 'w' ? 'w' : modifier == 'x' ? 'x' : size <= 4 ? 'w' : 'x';
    return std::string(1, prefix) + reg.substr(1);
}

// x86-64 (AT&T): the register by its size, 8 bytes "rax", 4 "eax", 2 "ax", 1 "al"; numbered ones "r8", "r8d"...
inline std::string x86Register(const std::string &reg, char modifier, unsigned size) {
    unsigned bytes = modifier == 'q' ? 8 : modifier == 'k' ? 4 : modifier == 'w' ? 2 : modifier == 'b' ? 1 : size;
    std::string base = reg;
    if (!base.empty() && base[0] == 'r' && base.size() > 1 && std::isdigit(static_cast<unsigned char>(base[1]))) {
        static const char *suffixes[] = {"", "b", "w", "", "d", "", "", "", ""};
        return "%" + base + (bytes >= 8 ? "" : suffixes[bytes > 8 ? 8 : bytes]);
    }
    struct Names {
        const char *r64, *r32, *r16, *r8;
    };
    static const Names table[] = {{"rax", "eax", "ax", "al"}, {"rbx", "ebx", "bx", "bl"},  {"rcx", "ecx", "cx", "cl"},
                                  {"rdx", "edx", "dx", "dl"}, {"rsi", "esi", "si", "sil"}, {"rdi", "edi", "di", "dil"}};
    for (const Names &names : table) {
        if (base != names.r64) continue;
        return std::string("%") + (bytes >= 8 ? names.r64 : bytes == 4 ? names.r32 : bytes == 2 ? names.r16 : names.r8);
    }
    return "%" + base;
}

// The registers the substitution hands out when an operand names none: ones a block seldom names itself.
inline const char *armScratch[] = {"x9",  "x10", "x11", "x12", "x13", "x14", "x15",
                                   "x19", "x20", "x21", "x22", "x23", "x24", "x25"};
inline const char *x86Scratch[] = {"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};

// The text of the template with each operand replaced, the registers they stand for and their constants. False when the
// statement is one this reading does not handle (an `asm goto`, an unknown target, a template clang cannot split).
inline bool substitute(const clang::GCCAsmStmt *statement, clang::ASTContext &context, const Triple &triple,
                       std::string &out, std::vector<Slot> &seeds) {
    bool arm = triple.isAArch64();
    bool x86 = triple.getArch() == Triple::x86_64;
    if ((!arm && !x86) || statement->getNumLabels() != 0) return false;
    unsigned outputs = statement->getNumOutputs(), inputs = statement->getNumInputs();
    unsigned total = outputs + inputs, scratch = 0;
    std::vector<std::string> text(total), regs(total);
    std::vector<unsigned> sizes(total);
    std::vector<std::string> constraints(total);
    std::vector<const clang::Expr *> exprs(total);
    for (unsigned i = 0; i < total; i++) {
        bool isOutput = i < outputs;
        exprs[i] = isOutput ? statement->getOutputExpr(i) : statement->getInputExpr(i - outputs);
        std::string raw = (isOutput ? statement->getOutputConstraint(i) : statement->getInputConstraint(i - outputs));
        for (char c : raw)
            if (std::string("=+&%*#!?").find(c) == std::string::npos) constraints[i] += c;
        sizes[i] = static_cast<unsigned>(context.getTypeSize(exprs[i]->getType()) / 8);
    }
    for (unsigned i = 0; i < total; i++) {
        const std::string &letters = constraints[i];
        const clang::Type *type = exprs[i]->getType().getTypePtr();
        bool vector = type->isFloatingType() || type->isVectorType();
        std::string fixed = registerVariable(exprs[i]);
        if (fixed.empty() && x86) {
            static const std::pair<char, const char *> explicitRegs[] = {{'a', "rax"}, {'b', "rbx"}, {'c', "rcx"},
                                                                         {'d', "rdx"}, {'S', "rsi"}, {'D', "rdi"}};
            for (const auto &[letter, name] : explicitRegs)
                if (letters.size() == 1 && letters[0] == letter) fixed = name;
        }
        if (!letters.empty() && std::isdigit(static_cast<unsigned char>(letters[0]))) {
            unsigned tied = static_cast<unsigned>(letters[0] - '0');
            if (tied >= total || tied >= i) return false;
            regs[i] = regs[tied];
            text[i] = text[tied];
            continue;
        }
        bool wantsVector = arm ? hasAny(letters, "wxy") : hasAny(letters, "xvY");
        bool wantsRegister = arm ? hasAny(letters, "r") : hasAny(letters, "rgqQRl");
        bool wantsMemory = arm ? hasAny(letters, "QmoVUp") : hasAny(letters, "mo");
        bool wantsImmediate = hasAny(letters, "inIJKLMNEFGHSZDCe") && !wantsRegister;
        if (!fixed.empty() || wantsRegister || (!wantsVector && !wantsMemory && !wantsImmediate)) {
            regs[i] = fixed.empty() ? (arm ? armScratch[scratch % 14] : x86Scratch[scratch % 8]) : fixed;
            if (fixed.empty()) scratch++;
            text[i] = arm ? armRegister(regs[i], 0, sizes[i]) : x86Register(regs[i], 0, sizes[i]);
        } else if (wantsVector) {
            regs[i] = arm ? "v" + std::to_string(scratch) : "xmm" + std::to_string(scratch + 8);
            scratch++;
            if (arm) {
                bool scalar = !type->isVectorType() && sizes[i] < 16;
                text[i] = scalar ? std::string(sizes[i] <= 2   ? "h"
                                               : sizes[i] <= 4 ? "s"
                                                               : "d") +
                                       regs[i].substr(1)
                                 : regs[i];
            } else {
                text[i] = "%" + regs[i];
            }
        } else if (wantsMemory) {
            regs[i] = arm ? armScratch[scratch % 14] : x86Scratch[scratch % 8];
            scratch++;
            text[i] = arm ? "[" + regs[i] + "]" : "(%" + regs[i] + ")";
            regs[i].clear();
        } else {
            auto value = valueOf(exprs[i], context);
            text[i] = std::string(x86 ? "$" : "") + std::to_string(value.value_or(0));
        }
        (void)vector;
    }
    // The constants the registers hold on entry: an input that is a constant, in the register it was given.
    for (unsigned i = outputs; i < total; i++) {
        if (regs[i].empty()) continue;
        if (auto value = valueOf(exprs[i], context)) seeds.push_back({regs[i], value});
    }
    for (unsigned i = 0; i < outputs; i++)
        if (!regs[i].empty() && !registerVariable(exprs[i]).empty() && statement->isOutputPlusConstraint(i))
            if (auto value = valueOf(exprs[i], context)) seeds.push_back({regs[i], value});
    llvm::SmallVector<clang::GCCAsmStmt::AsmStringPiece, 8> pieces;
    unsigned offset = 0;
    if (statement->AnalyzeAsmString(pieces, context, offset) != 0) return false;
    for (const auto &piece : pieces) {
        if (piece.isString()) {
            const std::string &literal = piece.getString();
            for (size_t i = 0; i < literal.size(); i++) {
                out += literal[i];
                if (literal[i] == '$' && i + 1 < literal.size() && literal[i + 1] == '$') i++; // ".ll" form's `$$`
            }
            continue;
        }
        unsigned number = piece.getOperandNo();
        if (number >= total) return false;
        char modifier = piece.getModifier();
        if (modifier == 'c' || modifier == 'P') {
            auto value = valueOf(exprs[number], context);
            out += std::to_string(value.value_or(0));
        } else if (!regs[number].empty() && modifier != 0) {
            out += arm ? armRegister(regs[number], modifier, sizes[number])
                       : x86Register(regs[number], modifier, sizes[number]);
        } else {
            out += text[number];
        }
    }
    return true;
}

inline void collectSymbols(const MCExpr *expr, std::vector<const MCSymbol *> &found) {
    if (expr == nullptr) return;
    switch (expr->getKind()) {
    case MCExpr::SymbolRef:
        found.push_back(&llvm::cast<MCSymbolRefExpr>(expr)->getSymbol());
        break;
    case MCExpr::Binary:
        collectSymbols(llvm::cast<MCBinaryExpr>(expr)->getLHS(), found);
        collectSymbols(llvm::cast<MCBinaryExpr>(expr)->getRHS(), found);
        break;
    case MCExpr::Unary:
        collectSymbols(llvm::cast<MCUnaryExpr>(expr)->getSubExpr(), found);
        break;
    case MCExpr::Specifier:
        collectSymbols(llvm::cast<MCSpecifierExpr>(expr)->getSubExpr(), found);
        break;
    default:
        break;
    }
}

// Which constant each register holds as the instructions run (a few moves are understood, every other write forgets).
class Registers {
  public:
    explicit Registers(const MCRegisterInfo &info) : info(info) {}
    MCRegister named(StringRef name) const {
        for (unsigned reg = 1; reg < info.getNumRegs(); reg++)
            if (StringRef(info.getName(reg)).equals_insensitive(name)) return MCRegister(reg);
        return MCRegister();
    }
    void set(MCRegister reg, int64_t value) {
        forget(reg);
        known.push_back({reg, value});
    }
    void forget(MCRegister reg) {
        llvm::erase_if(known, [&](const Known &entry) { return overlaps(entry.reg, reg); });
    }
    void clear() {
        known.clear();
    }
    std::optional<int64_t> get(MCRegister reg) const {
        if (!reg) return std::nullopt;
        for (const Known &entry : known)
            if (overlaps(entry.reg, reg)) return entry.value;
        return std::nullopt;
    }

  private:
    struct Known {
        MCRegister reg;
        int64_t value;
    };
    const MCRegisterInfo &info;
    std::vector<Known> known;
    bool overlaps(MCRegister a, MCRegister b) const {
        return a == b || info.isSubRegisterEq(a, b) || info.isSubRegisterEq(b, a);
    }
};

// Follows the instruction through the registers' constants (the moves a system call's number comes from).
inline void follow(const MCInst &inst, StringRef name, const MCInstrDesc &desc, const MCRegisterInfo &info,
                   Registers &known) {
    auto reg = [&](unsigned at) {
        return at < inst.getNumOperands() && inst.getOperand(at).isReg() ? MCRegister(inst.getOperand(at).getReg())
                                                                         : MCRegister();
    };
    auto imm = [&](unsigned at) {
        return at < inst.getNumOperands() && inst.getOperand(at).isImm() ? inst.getOperand(at).getImm() : 0;
    };
    auto zero = [&](MCRegister r) {
        return r && (StringRef(info.getName(r)) == "XZR" || StringRef(info.getName(r)) == "WZR");
    };
    MCRegister target = reg(0);
    if ((name == "MOVZXi" || name == "MOVZWi") && target) {
        known.set(target, static_cast<int64_t>(static_cast<uint64_t>(imm(1)) << imm(2)));
    } else if ((name == "MOVNXi" || name == "MOVNWi") && target) {
        known.set(target, ~static_cast<int64_t>(static_cast<uint64_t>(imm(1)) << imm(2)));
    } else if ((name == "ORRXrs" || name == "ORRWrs") && target && zero(reg(1)) && imm(3) == 0) {
        auto value = zero(reg(2)) ? std::optional<int64_t>(0) : known.get(reg(2));
        known.forget(target);
        if (value) known.set(target, *value);
    } else if ((name == "MOV32ri" || name == "MOV32ri64" || name == "MOV64ri32" || name == "MOV64ri") && target) {
        known.set(target, imm(1));
    } else if (name == "XOR32rr" && target && reg(1) == reg(2)) {
        known.set(target, 0);
    } else if ((name == "MOV64rr" || name == "MOV32rr") && target) {
        auto value = known.get(reg(1));
        known.forget(target);
        if (value) known.set(target, *value);
    } else {
        for (unsigned def = 0; def < desc.getNumDefs(); def++)
            if (reg(def)) known.forget(reg(def));
    }
    if (desc.isCall()) known.clear();
}

} // namespace detail

// Reads one `asm` statement. `parsed` is false when the block cannot be read, and the caller keeps it `c`.
inline AsmReading readAsm(const clang::AsmStmt *statement, clang::ASTContext &context) {
    using namespace llvm;
    AsmReading reading;
    const auto *gcc = dyn_cast<clang::GCCAsmStmt>(statement);
    if (gcc == nullptr) return reading;
    const clang::TargetInfo &target = context.getTargetInfo();
    Triple triple = target.getTriple();
    std::string text;
    std::vector<detail::Slot> seeds;
    if (!detail::substitute(gcc, context, triple, text, seeds)) return reading;
    static bool initialized = [] {
        InitializeAllTargetInfos();
        InitializeAllTargetMCs();
        InitializeAllAsmParsers();
        return true;
    }();
    (void)initialized;
    std::string error;
    const Target *found = TargetRegistry::lookupTarget(triple, error);
    if (found == nullptr) return reading;
    std::string features;
    for (const std::string &feature : target.getTargetOpts().Features)
        features += (features.empty() ? "" : ",") + feature;
    std::unique_ptr<MCRegisterInfo> registers(found->createMCRegInfo(triple));
    MCTargetOptions options;
    std::unique_ptr<MCAsmInfo> asmInfo(registers ? found->createMCAsmInfo(*registers, triple, options) : nullptr);
    std::unique_ptr<MCInstrInfo> instructionInfo(found->createMCInstrInfo());
    std::unique_ptr<MCSubtargetInfo> subtarget(
        found->createMCSubtargetInfo(triple, target.getTargetOpts().CPU, features));
    if (!registers || !asmInfo || !instructionInfo || !subtarget) return reading;
    SourceMgr sources;
    bool failed = false;
    sources.setDiagHandler(
        [](const SMDiagnostic &diagnostic, void *flag) {
            if (diagnostic.getKind() == SourceMgr::DK_Error) *static_cast<bool *>(flag) = true;
        },
        &failed);
    sources.AddNewSourceBuffer(MemoryBuffer::getMemBufferCopy(text, "asm"), SMLoc());
    MCContext mc(triple, *asmInfo, *registers, *subtarget, &sources);
    MCObjectFileInfo objectInfo;
    objectInfo.initMCObjectFileInfo(mc, false);
    mc.setObjectFileInfo(&objectInfo);
    detail::Collect streamer(mc);
    streamer.initSections(*subtarget);
    std::unique_ptr<MCAsmParser> parser(createMCAsmParser(sources, mc, streamer, *asmInfo));
    std::unique_ptr<MCTargetAsmParser> targetParser(found->createMCAsmParser(*subtarget, *parser, *instructionInfo));
    if (!parser || !targetParser) return reading;
    parser->setTargetParser(*targetParser);
    if (parser->Run(true) || failed) return reading;

    detail::Registers known(*registers);
    for (const detail::Slot &seed : seeds)
        if (MCRegister reg = known.named(seed.reg); reg && seed.value) known.set(reg, *seed.value);
    MCRegister numberRegister = known.named(syscallNumberRegister(triple));
    MCRegister protectionRegister = known.named(syscallProtectionRegister(triple));
    for (const MCInst &inst : streamer.instructions) {
        const MCInstrDesc &desc = instructionInfo->get(inst.getOpcode());
        StringRef name = instructionInfo->getName(inst.getOpcode());
        if (name == "SVC" || name == "SYSCALL") {
            reading.syscalls.push_back({known.get(numberRegister), known.get(protectionRegister)});
            known.clear();
            continue;
        }
        if (desc.isCall() || desc.isBranch()) {
            std::vector<const MCSymbol *> symbols;
            bool viaRegister = false;
            for (unsigned at = 0; at < inst.getNumOperands(); at++) {
                const MCOperand &operand = inst.getOperand(at);
                if (operand.isExpr())
                    detail::collectSymbols(operand.getExpr(), symbols);
                else if (operand.isReg() && (desc.isIndirectBranch() || desc.isCall()))
                    viaRegister = true;
            }
            for (const MCSymbol *symbol : symbols) {
                if (symbol->isTemporary() || symbol->isDefined()) continue;
                std::string called = symbol->getName().str();
                if (triple.isOSDarwin() && !called.empty() && called[0] == '_') called.erase(0, 1);
                reading.calls.push_back(called);
            }
            // A conditional branch reads a register that is a condition, not a target: only a call, an indirect
            // branch or a branch with no symbol at all is a way out through a register.
            bool nothingNamed = symbols.empty() && (desc.isIndirectBranch() || (desc.isCall() && viaRegister));
            if (nothingNamed || (desc.isIndirectBranch() && !desc.isReturn())) reading.indirect = true;
        }
        detail::follow(inst, name, desc, *registers, known);
    }
    reading.parsed = true;
    return reading;
}

} // namespace smith_effects
